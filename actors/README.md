<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# actors — Actor Framework

A low-latency actor framework. Each actor owns its state and communicates by
**in-process** async message passing (plus an on-stack `fast_send` fast path).

Two implementations live here, plus an in-process bridge between them:

| Directory | What |
|-----------|------|
| [`cpp/`](cpp) | The C++ actor framework (the reference implementation). |
| [`rust/`](rust) | A performance-first Rust port (crate `actors`) — see [`rust/README.md`](rust/README.md). |
| [`rust/interop/`](rust/interop) | **C++/Rust FFI interop** — C++ and Rust actors talking in one process over a C ABI. See [`rust/interop/README.md`](rust/interop/README.md). |

## C++ core (`cpp/include/actors/`)

| File | Purpose |
|------|---------|
| `Actor.hpp` | Base actor class with message dispatch |
| `Message.hpp` | Base message class with type-safe routing |
| `Queue.hpp` / `BQueue.hpp` | Message queues (mailbox) |
| `MemoryPool.hpp` | Per-message-type pool allocator |
| `ActorRef.hpp` | Lightweight actor reference handle |

## Actor types (`cpp/include/actors/act/`)

| Actor | Purpose |
|-------|---------|
| `Manager` | Lifecycle management — init, start, shutdown |
| `Group` | Runs N actors on one thread for deterministic in-process simulation |

## Scope

This framework is **in-process only**: actors and the C++/Rust interop all run in
a single process. There is no remote/cross-process actor transport.

## Build

```bash
cd actors/cpp && KSPRPROJ=~/m2_kaspar make      # C++
cd actors/rust && cargo test                     # Rust port
```
