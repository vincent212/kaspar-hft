# actors2 — a performance-first Rust actor framework

`actors2` is a Rust port of the C++ **Kaspar** actor framework (`../cpp`), tuned for
low-latency / HFT-style workloads. It is **in-process and shared-nothing**: actors own their
state and communicate only by messages, with two message paths that let you tune latency by
configuration rather than by rewriting code.

> **Looking for the matching engine?** It ships as an example:
> [`examples/matching_engine.rs`](examples/matching_engine.rs). See
> [`MATCHING_ENGINE.md`](MATCHING_ENGINE.md).

## Quickstart

```sh
# from this directory (actors/rust2)
cargo test                                   # framework unit/integration tests
cargo run   --example matching_engine        # matching-engine demo (build book, cross, cancel, replace)
cargo test  --example matching_engine        # the engine's 15 tests
cargo run --release --example matching_engine -- bench   # engine throughput benchmark
cargo run   --example ping_pong              # async actor round-trip
cargo run --release --example bench_fast_send   # fast_send latency
cargo run --release --example bench_pool        # pool vs heap allocation
```

Needs a stable Rust toolchain (`rustc`/`cargo`, e.g. via [rustup](https://rustup.rs)). No
server, container, or database — everything is in-memory.

## The core idea: two message paths

| | `fast_send` (hot path) | `send` (cold path) |
|---|---|---|
| Semantics | runs the target's handler **inline on the caller's thread**, message **on the stack** | **enqueues** into the target's mailbox; the target's own thread runs it |
| Cost | ~a function call (~20 ns) | ~microseconds (thread hop + condvar wakeup) |
| Use for | synchronous request/reply on the latency-critical chain | async, decoupled fan-out |

Collapse the hot chain onto one (optionally pinned) thread with `fast_send`; push everything
off-path (fan-out, logging, I/O) onto other threads with `send`.

## What's in the box (and how it maps to the C++)

| Primitive | What it does | C++ origin |
|---|---|---|
| `fast_send` | on-stack, inline, any actor → any actor (per-actor lock) | `Actor::fast_send` |
| integer message IDs + `handle_messages!` | O(1) dispatch via a flat table | `handler_cache` |
| `BQueue` | preallocated ring + deque overflow + condvar mailbox | `BQueue` |
| object pool (`define_pooled_message!`, `MsgBox::pooled`) | per-type block recycling, no malloc on the hot path | `MemoryPool` |
| `Manager` | thread-per-actor + Linux CPU affinity / `SCHED_FIFO` | `Manager` |

Release profile is tuned (`lto`, `codegen-units=1`, `panic=abort`); add
`RUSTFLAGS="-C target-cpu=native"` for the last few percent.

## What is **not** ported (be aware)

This is a fresh, focused port — it is **not** the full C++/`m2_kspr` framework:

- **No remote transport / registry / coordination** — `actors2` is **in-process only** (no ZMQ,
  no `GlobalRegistry`, no cross-process actor lookup). Those live in the private multi-language
  build, not here.
- **No Groups** (many actors sharing one thread), **no timer subsystem**, **no console/monitoring**
  — deferred.
- **No lock-free queue yet** — the mailbox is `Mutex`+`Condvar` (as in C++); a lock-free ring
  (`crossbeam`) is a possible future step.

## Layout

```
actors/rust2
├── src/          # the framework crate (actors2)
│   ├── actor.rs      # Actor trait, ActorContext, fast_send, dispatch (handle_messages!)
│   ├── message.rs    # Message trait + define_message! (integer ids)
│   ├── pool.rs       # per-type object pool + define_pooled_message!
│   ├── owner.rs      # MsgBox (heap-or-pooled message owner)
│   ├── queue.rs      # BQueue mailbox
│   ├── manager.rs    # thread-per-actor Manager, affinity/priority
│   └── messages.rs   # framework messages (Start, Shutdown)
├── examples/
│   ├── matching_engine.rs   # <-- the order-book matching engine
│   ├── ping_pong.rs / bench_fast_send.rs / bench_pool.rs
└── tests/        # queue / pool / owner / manager integration tests
```

See [`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md) to write your own actor, and
[`MATCHING_ENGINE.md`](MATCHING_ENGINE.md) for the engine design.

## Further reading

Background on the design this port is based on (author's write-ups):

- **The Actor Model for Low-Latency Software** —
  <https://vincentmayeski.substack.com/p/the-actor-model-for-low-latency-software>
- **A High-Performance Mailbox in the Kaspar Actor Framework** (the `BQueue` design) —
  <https://vincentmayeski.substack.com/p/high-performance-mailbox-in-the-kaspar>
- **A Custom Memory Allocator for the Kaspar Actor Framework** (the object pool) —
  <https://vincentmayeski.substack.com/p/a-custom-memory-allocator-for-the>

Licensed MIT (see repo root).
