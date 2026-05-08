<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# C++ Actors - AI Agent Technical Reference

## Quick Context for AI Agents

This is a high-performance actor framework for concurrent programming in C++20. It provides:
- Actor-based concurrency (actors communicate via messages, not shared state)
- Each actor processes messages sequentially in its own thread
- Both local and remote actor communication
- Cross-language support via JSON/ZMQ (interops with Dart, Java, Python, Rust)
- Compile-time handler registration via macros
- CPU affinity and thread priority control
- Deterministic coordination mode for simulation

## File Map

| Area | Key Files |
|------|-----------|
| **Core** | `include/actors/Actor.hpp`, `src/Actor.cpp`, `include/actors/Message.hpp` |
| **ActorRef** | `include/actors/ActorRef.hpp` |
| **Queue** | `include/actors/Queue.hpp`, `include/actors/BQueue.hpp` |
| **Manager** | `include/actors/act/Manager.hpp`, `src/Manager.cpp` |
| **Group** | `include/actors/act/Group.hpp`, `src/Group.cpp` |
| **Timer** | `include/actors/act/Timer.hpp` |
| **Built-in Msgs** | `include/actors/msg/Start.hpp`, `Shutdown.hpp`, `Continue.hpp`, `Timeout.hpp` |
| **Remote** | `include/actors/remote/ZmqSender.hpp`, `ZmqReceiver.hpp`, `Serialization.hpp` |
| **Registry** | `include/actors/registry/RegistryClient.hpp`, `GlobalRegistry.hpp` |
| **Coordination** | `include/actors/coordination/GroupManager.hpp` |
| **Memory** | `include/actors/MemoryPool.hpp`, `HybridBuffer.hpp` |
| **Examples** | `examples/ping_pong.cpp`, `remote_ping.cpp`, `remote_pong.cpp` |
| **Tests** | `tests/test_message.cpp`, `test_queue.cpp`, `test_registry_messages.cpp` |

## Architecture Overview

```
Manager (one per process)
  |
  ├─ Actor 1 (own std::thread, BQueue mailbox)
  ├─ Actor 2
  └─ Actor N

Each Actor:
  - Has a BQueue<Message*> (blocking queue) as mailbox
  - Processes messages via handler_cache[msg_id] dispatch
  - Can send/reply/fast_send to other actors
  - Runs operator()() as main loop in its thread
```

- **Manager**: Registers actors, manages threads, CPU affinity, registry integration
- **Actor**: Base class with handler dispatch, send/reply/fast_send
- **ActorRef**: `std::variant<LocalActorRef, RemoteActorRef, RustActorRef>` — location-transparent
- **Message**: `Message_N<ID>` template with integer IDs for O(1) dispatch
- **BQueue**: Blocking queue (mutex + condition_variable) for actor mailbox
- **Group**: Multiple actors on single thread (lightweight)

## Code Patterns

### Adding a New Message Type

1. Define struct extending `Message_N<ID>` with a unique integer ID
2. IDs 1-9 are reserved for system messages; use >= 100 for user messages
3. For remote use, register with `REGISTER_REMOTE_MESSAGE_N()` macro

```cpp
#include "actors/Message.hpp"

struct OrderMessage : public Message_N<100> {
    std::string order_id;
    double price;
    int quantity;

    OrderMessage(std::string id = "", double p = 0.0, int q = 0)
        : order_id(std::move(id)), price(p), quantity(q) {}
};

// For remote serialization (if needed):
REGISTER_REMOTE_MESSAGE_3(OrderMessage, order_id, std::string, price, double, quantity, int)
```

### Adding a Message Handler

Use `MESSAGE_HANDLER` macro in the constructor:

```cpp
class MyActor : public Actor {
public:
    MyActor() : Actor("my_actor") {
        MESSAGE_HANDLER(OrderMessage, on_order);
        MESSAGE_HANDLER(msg::Start, on_start);
        MESSAGE_HANDLER(msg::Shutdown, on_shutdown);
    }

private:
    void on_start(const msg::Start*) {
        // Called when manager starts
    }

    void on_order(const OrderMessage* msg) {
        std::cout << "Order: " << msg->order_id << std::endl;
        reply(new OrderConfirmation(msg->order_id, true));
    }

    void on_shutdown(const msg::Shutdown*) {
        // Cleanup
    }
};
```

**Rules**:
- Handler methods take `const MessageType*` parameter
- Register in constructor with `MESSAGE_HANDLER(Type, method_name)`
- Handler names are arbitrary (no naming convention enforced)
- Each message type can have only one handler per actor

### Handler Dispatch Mechanism

The dispatch is a two-level cache system:

1. **Fast path**: `handler_cache[msg->get_message_id()]` — O(1) vector lookup by message ID
2. **Slow path**: RTTI `std::type_index(typeid(*m))` → `handlers` map lookup, then cache result
3. **Miss tracking**: `dont_have_handler[id]` avoids repeated RTTI for unhandled types

```
MESSAGE_HANDLER macro
  → registers handler in handlers map (by type_index)
  → first call: RTTI lookup → cache in handler_cache[id]
  → subsequent calls: direct vector[id] access (O(1))
```

### Sending Messages

**Async send** (message queued, returns immediately):
```cpp
target->send(new PingMessage(42), this);  // this = sender
```

**Sync fast_send** (executes handler in CALLER's thread, blocks):
```cpp
auto reply = target->fast_send(new RequestMessage(), this);
if (reply) {
    auto* resp = dynamic_cast<const ResponseMessage*>(reply.get());
    // process response
}
```

**Reply** (send message back to sender):
```cpp
void on_ping(const PingMessage* msg) {
    reply(new PongMessage(msg->count + 1));
}
```

## Critical Implementation Details

### reply() Implementation

```cpp
void Actor::reply(const Message *m) noexcept {
    if (using_fast_send) {
        // Synchronous: store reply for fast_send caller
        m->sender = this;
        reply_message = m;
    } else {
        // Asynchronous: send to original sender's queue
        assert(reply_to != nullptr && "no return address");
        reply_to->send(m, this);
    }
}
```

- `using_fast_send` flag is set by `fast_send()` before calling handler
- `reply_message` pointer is read by `fast_send()` after handler returns
- `reply_to` is set from `msg->sender` during async processing

### fast_send Flow

```cpp
std::unique_ptr<const Message> Actor::fast_send(const Message *m, Actor *sender) noexcept {
    std::lock_guard<std::mutex> lock(fast_send_mutex);
    m->sender = sender;
    m->is_fast = true;
    reply_message = nullptr;
    using_fast_send = true;

    bool called = call_handler(m);
    if (!called) process_message(m);

    return std::unique_ptr<const Message>(reply_message);
}
```

**Key points**:
- `fast_send_mutex` protects against concurrent async sends to same actor
- Handler runs in CALLER's thread (not actor's thread)
- Reply stored in `reply_message`, returned as `unique_ptr`
- Message is NOT queued — runs synchronously

### Message Lifecycle & Memory

- Messages are `new`-allocated by sender
- After async `send()`, the framework owns the pointer
- After handler returns, message is deleted by the processing loop
- For `fast_send()`, caller owns the reply via `unique_ptr`
- `MemoryPool` available for high-frequency message allocation

### Actor Main Loop

```cpp
void Actor::operator()() {
    while (!terminated) {
        const Message* m = queue->pop();  // Blocks
        if (m->last) {
            call_handler(m);
            delete m;
            break;
        }
        call_handler(m);
        delete m;
    }
}
```

## Thread Safety

- **One thread per actor**: Each actor runs `operator()()` in its own `std::thread`
- **BQueue**: Thread-safe blocking queue (mutex + condition_variable)
- **fast_send_mutex**: Protects handler dispatch during sync calls
- **No shared state**: Actors communicate only via messages
- **Message ownership**: Transferred on send, deleted after handling

## Remote Communication (ZMQ)

### ZmqSender

Actor that sends messages via ZMQ PUSH sockets:
```cpp
auto sender = std::make_shared<ZmqSender>("tcp://localhost:5002");
manager.manage(sender.get());

// Create remote reference
ActorRef remote_pong = sender->remote_ref("pong", "tcp://localhost:5001");
actor->send(remote_pong, new PingMessage(1));
```

### ZmqReceiver

Actor that receives via ZMQ PULL socket:
```cpp
auto receiver = new ZmqReceiver("tcp://0.0.0.0:5001", sender);
receiver->register_actor("pong", pong_actor);
manager.manage(receiver);
```

### JSON Wire Format

```json
{
    "sender_actor": "ping",
    "sender_endpoint": "tcp://localhost:5002",
    "receiver": "pong",
    "message_type": "Ping",
    "message": { "count": 42 }
}
```

### Remote Message Registration

```cpp
// No fields
REGISTER_REMOTE_MESSAGE_0(HeartbeatMessage)

// 1 field
REGISTER_REMOTE_MESSAGE_1(PingMessage, count, int)

// 2 fields
REGISTER_REMOTE_MESSAGE_2(OrderMessage, order_id, std::string, price, double)

// Up to 10 fields supported
REGISTER_REMOTE_MESSAGE_10(...)

// Custom serialization
REGISTER_REMOTE_MESSAGE(ComplexMessage,
    /* serialize */   { j["data"] = m->data; j["items"] = m->items; },
    /* deserialize */ { return new ComplexMessage(j["data"], j["items"]); }
)
```

## Registry & Coordination

### RegistryClient (GlobalRegistry)

```cpp
auto registry = RegistryClient("tcp://registry-host:7000");
registry.connect();
registry.register_manager("my_manager", "tcp://localhost:5001", {"actor1", "actor2"});
registry.start_heartbeat_thread("my_manager");

// Lookup
auto result = registry.lookup_actor("remote_actor");
if (result.found && !result.ambiguous) {
    auto ref = sender->remote_ref(result.actor_id, result.endpoint);
}
```

### GroupManager (Deterministic Coordination)

- Permission-based protocol: TOKEN → REQUEST → GRANT → DONE
- FIFO queue for ordering
- Binary wire format (1 byte type + length-prefixed strings)
- Debug features: pause, resume, breakpoints, queue inspection

## Build System

```bash
# Optimized library
make opt       # → lib/libactors.a

# Debug library
make debug     # → lib/libactorsg.a

# Examples
make examples  # → bin/ping_pong, bin/remote_ping, bin/remote_pong

# Tests
make test      # Build and run unit tests

# Coordination server
make coordination  # → bin/group_manager

# Registry server
make registry      # → bin/global_registry
```

**Dependencies**:
- C++20 compiler (g++)
- Boost 1.88 (circular_buffer, thread)
- ZeroMQ (libzmq)
- nlohmann/json (header-only)
- Google Test (for tests)

## Testing Patterns

```cpp
#include <gtest/gtest.h>

TEST(ActorTest, HandlesPingMessage) {
    PongActor pong;
    Manager mgr;
    mgr.manage(&pong);

    // fast_send for synchronous testing
    auto reply = pong.fast_send(new PingMessage(42), nullptr);
    ASSERT_NE(reply, nullptr);

    auto* pong_msg = dynamic_cast<const PongMessage*>(reply.get());
    ASSERT_NE(pong_msg, nullptr);
    EXPECT_EQ(pong_msg->count, 42);
}

TEST(SerializationTest, RoundTrip) {
    PingMessage msg(42);
    auto json = serialization::serialize(&msg);
    auto* deserialized = serialization::deserialize("PingMessage", json["message"]);
    auto* ping = dynamic_cast<PingMessage*>(deserialized);
    EXPECT_EQ(ping->count, 42);
    delete deserialized;
}
```

## Debugging Tips

### Handler Not Called
1. Verify `MESSAGE_HANDLER(Type, method)` is in constructor
2. Check message ID is unique (no collisions with other message types)
3. Verify message ID is within cache range (< 2048, or adjust cache size)
4. Use debugger to check `handler_cache[msg_id]` is not null

### Message Not Delivered
1. Check `send()` target is valid (not null pointer)
2. Verify actor is managed by Manager and thread is running
3. For remote: check ZMQ endpoints match, receiver is bound before sender connects
4. Check `REGISTER_REMOTE_MESSAGE` for correct field count and types

### fast_send Issues
1. Never `fast_send` to self (assertion failure)
2. `fast_send_mutex` can deadlock if handler calls `fast_send` on same actor
3. Remote actors don't support `fast_send` (throws)

### Memory Issues
1. Always `new` messages for `send()` — framework deletes them
2. Don't access message pointer after `send()` — ownership transferred
3. `fast_send` reply is `unique_ptr` — no manual delete needed

## Common Pitfalls

1. **Message ID collision**: Two message types with same ID → wrong handler called
2. **Self fast_send**: Causes assertion failure — use `send()` to self instead
3. **Dangling message pointer**: Accessing message after `send()` is undefined behavior
4. **Missing handler registration**: Forgetting `MESSAGE_HANDLER` in constructor → message silently dropped
5. **Wrong serialization macro**: Field count mismatch → crash or corrupt data
6. **Thread affinity without privileges**: `sched_setaffinity` needs CAP_SYS_NICE
7. **Shutdown ordering**: Always call `manager.end()` before destroying actors
8. **Reply without sender**: `reply()` asserts `reply_to != nullptr` for async messages

## Message ID Scheme

| Range | Purpose |
|-------|---------|
| 1 | Continue |
| 2 | Timeout |
| 5 | Shutdown |
| 6 | Start |
| 7 | Set |
| 8 | Subscribe |
| 9 | Reject |
| 10-99 | Framework reserved |
| >= 100 | User-defined messages |

## Cross-Language Reference

| Feature | C++ | Dart | Python | Rust |
|---------|-----|------|--------|------|
| Handler discovery | `MESSAGE_HANDLER` macro | Code generation | Reflection (`getattr`) | `handle_messages!` macro |
| Message IDs | Integer template param | String type name | Class name | `Any` downcast |
| Dispatch | Vector cache + RTTI | `if (msg is T)` | `on_<type>()` lookup | Pattern matching |
| Threading | `std::thread` | Event loop | `threading.Thread` | `std::thread::spawn` |
| Memory | Manual/smart pointers | GC | GC | Ownership/Box |
| fast_send | Runs in caller thread | Future/Completer | `Queue.get()` block | Channel recv block |

## Quick Reference Commands

```bash
# Build optimized
make opt

# Build debug
make debug

# Build and run tests
make test

# Build examples
make examples

# Run local example
./bin/ping_pong

# Run remote example (two terminals)
./bin/remote_pong --bind tcp://*:5001
./bin/remote_ping --target tcp://localhost:5001

# Clean
make clean
```

## Related Documentation

- `include/actors/act/README.md` — Manager, Group, Timer docs
- `include/actors/msg/README.md` — Built-in message types
- `CPP26_ROADMAP.md` — Performance optimization roadmap
- `include/actors/coordination/COORDINATION_PROTOCOL.md` — Deterministic coordination protocol
- `actors/dart/CLAUDE_AGENT_GUIDE.md` — Dart implementation reference
- `actors/rust/CLAUDE_AGENT_GUIDE.md` — Rust implementation reference
- `actors/python/CLAUDE_AGENT_GUIDE.md` — Python implementation reference
- `docs/CROSS_LANGUAGE_EXAMPLE.md` — Multi-language examples
- `docs/GLOBAL_REGISTRY.md` — Actor discovery service
- `docs/COORDINATOR.md` — GroupManager protocol
