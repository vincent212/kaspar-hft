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
| `Queue.hpp` | Mailbox interface (`push` / `pop` / `pop_batch`) |
| `BQueue.hpp` | Default mailbox: blocking mutex + condvar, ring + overflow |
| `ShardedBQueue.hpp` | Sharded (N-lane) mailbox for high-fan-in actors — cuts tail jitter |
| `LockFreeMPSC.hpp` | Lock-free (Vyukov) mailbox for few-producer, latency-critical actors |
| `MemoryPool.hpp` | Per-message-type pool allocator |
| `ActorRef.hpp` | Lightweight actor reference handle |

**Choosing a mailbox per actor:** an actor defaults to `BQueue`, but can install a
`ShardedBQueue` or `LockFreeMPSC` via the `Actor(Queue*)` constructor. Which to use
depends on producer count — see **[`cpp/MAILBOXES.md`](cpp/MAILBOXES.md)** for the
selection matrix, measurements, and code.

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

## Writeups

Design notes and measurements behind the mailbox internals:

- [How Message Batching More Than Doubles Actor Model Throughput](https://vincentmayeski.substack.com/p/how-message-batching-more-than-doubles) — batch drain + the message pool (2.46×).
- [Medians Lie, Tails Kill: Why Kaspar Shards Mailbox Locks](https://vincentmayeski.substack.com/p/medians-lie-tails-kill-why-kaspar) — per-actor mailboxes and tail-latency jitter.
- [The Lock-Free Illusion: Why CAS Storms Kill Actor Queues Under Contention](https://vincentmayeski.substack.com/p/the-lock-free-illusion-why-cas-storms) — when lock-free wins, and when it backfires.
