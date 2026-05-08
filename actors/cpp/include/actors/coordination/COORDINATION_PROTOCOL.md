<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Coordination Protocol Deep Dive

## Overview

The Coordination Protocol provides **deterministic, serialized execution** across distributed actor groups. It ensures that when multiple Groups (potentially in different processes or languages) need to communicate, messages are processed in a globally consistent order.

## Why Coordination?

In a single-threaded Group, message ordering is naturally deterministic. But when you have:
- Multiple Groups in separate processes
- Cross-language actors (C++, Python, Rust)
- External actors sending messages into a coordinated group

...you need explicit coordination to maintain determinism. Without it, network latency and scheduling differences cause non-deterministic message interleaving.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         GroupManager                                 │
│                        (Central Coordinator)                         │
│                         tcp://*:5555                                 │
│                                                                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐     │
│  │ Permission Queue│  │ Active Permission│  │ Global Sequence │     │
│  │  (FIFO order)   │  │  (one at a time) │  │   (monotonic)   │     │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘     │
└─────────────────────────────────────────────────────────────────────┘
           ▲                    ▲                    ▲
           │ ZMQ DEALER         │                    │
           │                    │                    │
    ┌──────┴──────┐      ┌──────┴──────┐     ┌──────┴──────┐
    │   Group A   │      │   Group B   │     │   Group C   │
    │  (Process 1)│      │  (Process 2)│     │  (Python)   │
    │             │      │             │     │             │
    │ ┌─────────┐ │      │ ┌─────────┐ │     │ ┌─────────┐ │
    │ │ Actor 1 │ │      │ │ Actor 3 │ │     │ │ Actor 5 │ │
    │ │ Actor 2 │ │      │ │ Actor 4 │ │     │ │ Actor 6 │ │
    │ └─────────┘ │      │ └─────────┘ │     │ └─────────┘ │
    └─────────────┘      └─────────────┘     └─────────────┘
```

## Message Types

### Registration Phase

| Message | Direction | Purpose |
|---------|-----------|---------|
| `REGISTER_GROUP` | Group → GM | Register a new group |
| `REGISTER_ACTOR` | Group → GM | Register an actor within a group |
| `REGISTER_ACK` | GM → Group | Confirm successful registration |
| `REGISTER_NACK` | GM → Group | Reject registration (duplicate, etc.) |

### Permission Protocol

| Message | Direction | Purpose |
|---------|-----------|---------|
| `PERMISSION_TOKEN` | Group → GM | Announce a pending message (adds to queue) |
| `PERMISSION_REQUEST` | Group → GM | Request permission to process |
| `PERMISSION_GRANT` | GM → Group | Permission granted, proceed |
| `PERMISSION_WAIT` | GM → Group | Not your turn, wait |
| `PERMISSION_DONE` | Group → GM | Finished processing, release permission |

### Shutdown Protocol

| Message | Direction | Purpose |
|---------|-----------|---------|
| `SHUTDOWN_REQUEST` | Group → GM | Request coordinated shutdown |
| `SHUTDOWN` | GM → Groups | Command all groups to shut down |
| `SHUTDOWN_ACK` | Group → GM | Confirm shutdown complete |

## Permission Protocol Flow

### Basic Flow (Single Message)

```
Actor A sends message to Actor B (both in same Group):

    Group                              GroupManager
      │                                     │
      │  1. TOKEN(recipient=B, sender=A)   │
      │────────────────────────────────────>│  → Adds to queue
      │                                     │
      │  2. REQUEST(actor=B)               │
      │────────────────────────────────────>│  → Checks if B is at head
      │                                     │
      │  3. GRANT(actor=B, seq=42)         │
      │<────────────────────────────────────│  ← Position 0, no active
      │                                     │
      │  [B.process_message() executes]    │
      │                                     │
      │  4. DONE(actor=B, seq=42)          │
      │────────────────────────────────────>│  → Clears active, grants next
      │                                     │
```

### Nested Send (Message Handler Sends Another Message)

When a message handler sends another message, it creates a nested coordination:

```
A sends to B, and B's handler sends to C:

    Group                              GroupManager
      │                                     │
      │  TOKEN(B, A, ts=1)                 │
      │────────────────────────────────────>│  queue: [B]
      │                                     │
      │  REQUEST(B)                        │
      │────────────────────────────────────>│
      │                                     │
      │  GRANT(B, seq=1)                   │
      │<────────────────────────────────────│  active=B
      │                                     │
      │  [B.process_message() starts]      │
      │    │                               │
      │    │  B sends to C:                │
      │    │  TOKEN(C, B, ts=2)            │
      │    │──────────────────────────────>│  queue: [C]
      │    │                               │
      │    │  REQUEST(C)                   │
      │    │──────────────────────────────>│
      │    │                               │
      │    │  WAIT(C, pos=0)               │  ← B still active!
      │    │<──────────────────────────────│
      │    │                               │
      │    │  [C waits...]                 │
      │                                     │
      │  DONE(B, seq=1)                    │
      │────────────────────────────────────>│  active=none
      │                                     │
      │                                     │  → try_grant_next()
      │  GRANT(C, seq=2)                   │
      │<────────────────────────────────────│  active=C
      │                                     │
      │  [C.process_message() executes]    │
      │                                     │
      │  DONE(C, seq=2)                    │
      │────────────────────────────────────>│
```

**Key Insight**: The nested send to C blocks until B completes. This ensures causal ordering: C processes its message only after B finishes.

### Multiple Groups

When groups are in different processes:

```
Process 1 (Group A)          GroupManager          Process 2 (Group B)
        │                         │                        │
        │  TOKEN(A.X, A.Y, ts=1) │                        │
        │────────────────────────>│                        │
        │                         │                        │
        │                         │  TOKEN(B.Z, B.W, ts=2)│
        │                         │<───────────────────────│
        │                         │                        │
        │  REQUEST(A.X)          │                        │
        │────────────────────────>│                        │
        │                         │                        │
        │  GRANT(A.X, seq=1)     │                        │
        │<────────────────────────│                        │
        │                         │                        │
        │                         │  REQUEST(B.Z)         │
        │                         │<───────────────────────│
        │                         │                        │
        │                         │  WAIT(B.Z, pos=1)     │
        │                         │───────────────────────>│
        │                         │                        │
        │  [A.X processes]       │                        │
        │                         │                        │
        │  DONE(A.X, seq=1)      │                        │
        │────────────────────────>│                        │
        │                         │                        │
        │                         │  GRANT(B.Z, seq=2)    │
        │                         │───────────────────────>│
        │                         │                        │
```

## Queue Management

### Token Queue (FIFO)

The GroupManager maintains a FIFO queue of permission tokens:

```cpp
struct QueueEntry {
    std::string recipient_id;   // Who will process: "group.actor"
    std::string sender_id;      // Who sent: "group.actor"
    uint64_t timestamp;         // Logical clock for ordering
};

std::deque<QueueEntry> permission_queue_;
```

### Active Permission

Only one actor may process at a time:

```cpp
std::optional<uint64_t> active_permission_;  // Current grant sequence
std::string active_actor_;                    // Who has permission
```

### Grant Logic

```cpp
void handle_permission_request(const PermissionRequest& req) {
    // Find position in queue
    uint32_t position = find_position(req.actor_id);

    if (position == 0 && !active_permission_.has_value()) {
        // At head and no active → GRANT
        active_permission_ = ++global_sequence_;
        active_actor_ = req.actor_id;
        send(PERMISSION_GRANT, req.actor_id, *active_permission_);
    } else {
        // Not at head or someone active → WAIT
        send(PERMISSION_WAIT, req.actor_id, position);
    }
}
```

### Done Handling

```cpp
void handle_permission_done(const PermissionDone& done) {
    // Verify sequence matches
    if (active_permission_ != done.sequence) return;

    // Clear active permission
    active_permission_.reset();
    active_actor_.clear();

    // Try to grant next waiting actor
    try_grant_next();
}
```

### Proactive Grant

After DONE, the GroupManager proactively grants the next pending request:

```cpp
void try_grant_next() {
    if (active_permission_.has_value()) return;  // Already active
    if (permission_queue_.empty()) return;        // Nothing pending

    const auto& next = permission_queue_.front();

    // Check if actor has a pending request
    if (pending_requests_.contains(next.recipient_id)) {
        // Grant immediately
        active_permission_ = ++global_sequence_;
        send(PERMISSION_GRANT, next.recipient_id, *active_permission_);
        permission_queue_.pop_front();
    }
    // Otherwise, actor will get GRANT when they REQUEST
}
```

## External Senders

When a message comes from an actor **outside** the coordinated group (e.g., Python MarketMaker sending to C++ PositionManager):

```cpp
void Group::forward(const Message* m) {
    // Check if sender is in this group
    bool sender_in_group = name_to_actor.contains(m->sender->get_name());

    if (mode_ == COORDINATED && registered_ && sender_in_group) {
        // Full coordination: TOKEN → REQUEST → GRANT → process → DONE
        send_permission_token(sender_id, recipient_id);
        request_permission(recipient_id);
        m->destination->process_message_internal(m);
        send_permission_done(recipient_id, sequence);
    } else {
        // External sender: process immediately (no coordination)
        m->destination->process_message_internal(m);
    }
}
```

**Implication**: External messages bypass coordination and may interleave non-deterministically with coordinated messages.

## Debugging Features

The GroupManager supports debugging via special messages:

| Feature | Messages | Description |
|---------|----------|-------------|
| **Pause** | `DEBUG_STOP` | Stop granting permissions |
| **Resume** | `DEBUG_CONTINUE` | Resume granting |
| **Breakpoints** | `DEBUG_ADD_BREAKPOINT` | Stop when specific actor gets permission |
| **Inspect Queue** | `DEBUG_GET_QUEUE` | View pending tokens |
| **Status** | `DEBUG_STATUS` | Check current state |

## Wire Format

Messages use binary serialization:

```
┌─────────────┬─────────────────────────────────────┐
│ MsgType (1B)│           Payload                   │
└─────────────┴─────────────────────────────────────┘

Strings: [length:4B][data:N bytes]
uint64:  [8 bytes, little-endian]
uint32:  [4 bytes, little-endian]
bool:    [1 byte: 0=false, 1=true]
```

Example TOKEN message:
```
┌────┬────────────────┬────────────────┬──────────┐
│ 10 │ recipient_id   │ sender_id      │ timestamp│
│ 1B │ len + string   │ len + string   │ 8 bytes  │
└────┴────────────────┴────────────────┴──────────┘
```

## Performance Considerations

1. **Latency**: Each message requires 4 round-trips (TOKEN, REQUEST, GRANT, DONE)
2. **Throughput**: Limited by GroupManager's single-threaded processing
3. **Batching**: Not supported; each message coordinated individually

For high-throughput scenarios, consider:
- Running without coordinator for non-critical paths
- Using local Groups (single-process) where possible
- Reserving coordination for cross-process/cross-language communication

## Files

| File | Purpose |
|------|---------|
| `include/actors/coordination/messages.hpp` | Message definitions and serialization |
| `include/actors/coordination/GroupManager.hpp` | GroupManager class declaration |
| `src/coordination/GroupManager.cpp` | GroupManager implementation |
| `src/coordination/group_manager_main.cpp` | Standalone GroupManager executable |
| `Group.cpp` | Group-side coordination logic |

## Example Usage

### Starting GroupManager

```bash
./group_manager --port 5555 --debug
```

### Enabling Coordination in a Group

```cpp
Group* group = new Group("sim_group");
group->set_group_manager("tcp://localhost:5555");
group->add(actor1);
group->add(actor2);
// ... add all actors
manager->manage(group);
```

### Command Line

```bash
# Run simulation with coordinator
./sim_kaspr --datafile data.bin --coordinator tcp://localhost:5555
```
