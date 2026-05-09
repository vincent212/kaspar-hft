<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# actors — C++ Actor Framework

Lock-free actor framework for low-latency trading systems. Each actor runs in its own thread and communicates via async message passing.

## Core

| File | Purpose |
|------|---------|
| `Actor.hpp` | Base actor class with message dispatch |
| `Message.hpp` | Base message class with type-safe routing |
| `Queue.hpp` | Lock-free SPSC message queue |
| `MemoryPool.hpp` | Per-message-type pool allocator |
| `ActorRef.hpp` | Lightweight actor reference handle |

## Actor Types (`act/`)

| Actor | Purpose |
|-------|---------|
| `Manager` | Lifecycle management — init, start, shutdown |
| `Group` | Groups actors for bulk operations |

## Remote Messaging (`remote/`)

| Actor | Purpose |
|-------|---------|
| `ZmqSender` | Serializes and sends messages over ZMQ |
| `ZmqReceiver` | Receives ZMQ messages and routes to local actors |
| `RemoteReplyProxy` | Proxy for request-reply across processes |

## Registry (`registry/`)

| Actor | Purpose |
|-------|---------|
| `RegistryActor` | Name → address mapping for actor discovery |
| `RegistryQueryActor` | Lookup actors by name across processes |

## Build

```bash
cd actors/cpp && KSPRPROJ=~/m2_kaspar make
```
