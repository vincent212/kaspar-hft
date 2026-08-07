<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# C++ Actors - AI Agent Technical Reference

## Quick Context for AI Agents

This is a high-performance, **in-process** actor framework for concurrent programming in C++20. It provides:
- Actor-based concurrency (actors communicate via messages, not shared state)
- Each actor processes messages sequentially in its own thread
- In-process C++/Rust interop (C++ and Rust actors in one process over a C-ABI FFI bridge)
- Compile-time handler registration via macros
- CPU affinity and thread priority control
- Groups: N actors on one thread for deterministic in-process simulation

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
| **Memory** | `include/actors/MemoryPool.hpp`, `HybridBuffer.hpp` |
| **C++/Rust interop** | `../rust/interop/` (see `../rust/interop/README.md`) |
| **Examples** | `examples/ping_pong.cpp` |
| **Tests** | `tests/test_message.cpp`, `test_queue.cpp` |

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

- **Manager**: Registers actors, manages threads, CPU affinity
- **Actor**: Base class with handler dispatch, send/reply/fast_send
- **ActorRef**: `std::variant<LocalActorRef, RustActorRef>` — a local C++ actor or a Rust actor over the in-process FFI bridge (same `send`/`fast_send` API)
- **Message**: `Message_N<ID>` template with integer IDs for O(1) dispatch
- **BQueue**: Blocking queue (mutex + condition_variable) for actor mailbox
- **Group**: Multiple actors on single thread (lightweight)

## Code Patterns

### Adding a New Message Type

1. Define struct extending `Message_N<ID>` with a unique integer ID
2. IDs 1-9 are reserved for system messages; use >= 100 for user messages

```cpp
#include "actors/Message.hpp"

struct OrderMessage : public Message_N<100> {
    std::string order_id;
    double price;
    int quantity;

    OrderMessage(std::string id = "", double p = 0.0, int q = 0)
        : order_id(std::move(id)), price(p), quantity(q) {}
};
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

## C++/Rust Interop (in-process)

C++ and Rust actors can run in the **same process** and message each other over a C-ABI FFI bridge,
location-transparently — you call `send` / `fast_send` on an `ActorRef` and never learn whether the
peer is C++ or Rust. There is **no remote / cross-process actor transport**.

The message set is declared once in `../rust/interop/messages/interop_messages.h` and a generator
emits the matching C++ and Rust structs, marshalling, and dispatch. See
`../rust/interop/README.md` for the full design and build.

## Build System

```bash
# Optimized library
make opt       # → lib/libactors.a

# Debug library
make debug     # → lib/libactorsg.a

# Examples
make examples  # → bin/ping_pong

# Tests
make test      # Build and run unit tests
```

**Dependencies**:
- C++20 compiler (g++)
- Boost 1.88 (circular_buffer, thread)
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

### fast_send Issues
1. Never `fast_send` to self (assertion failure)
2. `fast_send_mutex` can deadlock if handler calls `fast_send` on same actor

### Memory Issues
1. Always `new` messages for `send()` — framework deletes them
2. Don't access message pointer after `send()` — ownership transferred
3. `fast_send` reply is `unique_ptr` — no manual delete needed

## Common Pitfalls

1. **Message ID collision**: Two message types with same ID → wrong handler called
2. **Self fast_send**: Causes assertion failure — use `send()` to self instead
3. **Dangling message pointer**: Accessing message after `send()` is undefined behavior
4. **Missing handler registration**: Forgetting `MESSAGE_HANDLER` in constructor → message silently dropped
5. **Thread affinity without privileges**: `sched_setaffinity` needs CAP_SYS_NICE
6. **Shutdown ordering**: Always call `manager.end()` before destroying actors
7. **Reply without sender**: `reply()` asserts `reply_to != nullptr` for async messages

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

## C++ vs Rust Reference

The framework has two implementations that interoperate in-process (see `../rust/interop/README.md`):

| Feature | C++ | Rust |
|---------|-----|------|
| Handler discovery | `MESSAGE_HANDLER` macro | `handle_messages!` macro |
| Message IDs | Integer template param | Integer via `define_message!` |
| Dispatch | Vector cache + RTTI | Integer-id table + `Any` downcast |
| Threading | `std::thread` | `std::thread::spawn` |
| Memory | Manual/smart pointers | Ownership/Box + object pool |
| fast_send | Runs in caller thread | Runs in caller thread |

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

# Clean
make clean
```

## Related Documentation

- `include/actors/act/README.md` — Manager, Group, Timer docs
- `include/actors/msg/README.md` — Built-in message types
- `../rust/README.md` — the Rust port of the actor framework
- `../rust/interop/README.md` — in-process C++/Rust FFI interop
