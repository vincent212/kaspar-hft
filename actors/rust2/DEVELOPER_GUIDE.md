# actors2 — Developer Guide

How to build actors with `actors2`. This documents the **actual `rust2` API** (which differs
from the older channel-based Rust actor crate). See [`README.md`](README.md) for the big picture.

## 1. Define messages (with integer IDs)

Every message carries a **compile-time integer id** used for O(1) dispatch. IDs must be
`< 1024`; ids `< 16` are reserved by the framework (`Start = 4`, `Shutdown = 5`).

```rust
use actors2::define_message;

struct Ping { count: i32 }
define_message!(Ping, 10);

struct Pong { count: i32 }
define_message!(Pong, 11);
```

For hot-path messages sent **async** at high rates, make them **pooled** so delivery recycles
fixed-size blocks instead of calling `malloc` (the third arg is the per-thread cache size):

```rust
use actors2::define_pooled_message;

struct MarketData { /* ... */ }
define_pooled_message!(MarketData, 20, 128);
```

(Pooling only matters for the async `send` path; `fast_send` passes the message on the stack.)

## 2. Define an actor + its handlers

An actor is a plain struct. Write one handler method per message it accepts, then wire them up
with `handle_messages!`. Handlers take `&mut self`, the typed message, and an `ActorContext`.

```rust
use actors2::{handle_messages, ActorContext, ActorRef, Start};

struct PongActor;

impl PongActor {
    fn on_ping(&mut self, msg: &Ping, ctx: &mut ActorContext) {
        // reply goes back to the sender (or, under fast_send, is returned to the caller)
        ctx.reply(Box::new(Pong { count: msg.count }));
    }
}

handle_messages!(PongActor,
    Ping => on_ping,
);
```

Optional lifecycle hooks (from the `Actor` trait): `fn init(&mut self, ctx: &mut ActorContext)`
(runs once before messages) and `fn end(&mut self)` (runs once at shutdown). Unhandled message
ids are silently ignored (so handling `Start` is optional).

## 3. Send messages: `fast_send` vs `send`

```rust
// async, fire-and-forget: enqueue into the target's mailbox (its thread runs it).
target.send(Box::new(Ping { count: 1 }), ctx.self_ref());

// synchronous, on the stack: run the target's handler inline on THIS thread; get the reply.
let reply: Option<actors2::MsgBox> = target.fast_send(&Ping { count: 1 }, ctx.self_ref());

// pooled async send (recommended for hot-path messages):
target.send(actors2::MsgBox::pooled(MarketData { /* ... */ }), ctx.self_ref());
```

- `send(msg, sender)` — `msg` is `impl Into<MsgBox>`, so `Box::new(x)` (heap) or
  `MsgBox::pooled(x)` both work. `sender` is who to reply to (usually `ctx.self_ref()`).
- `fast_send(&msg, sender)` — borrows the message on the stack, runs the handler inline, returns
  the reply (the handler's `ctx.reply(...)`).

**Reentrancy caveat:** the per-actor lock behind `fast_send` is non-reentrant. Do **not** make a
`fast_send` chain that cycles back into an actor already executing on the same thread (A→A, or
A→B→A) — it deadlocks. Keep `fast_send` chains acyclic.

## 4. Inside a handler: `ActorContext`

- `ctx.reply(msg)` — reply to the current message (returned to the caller under `fast_send`;
  delivered to the sender's mailbox under `send`).
- `ctx.self_ref() -> Option<ActorRef>` — this actor's own handle (pass as the sender of outgoing
  messages).
- `ctx.sender() -> Option<ActorRef>` — who sent the current message.

## 5. Run actors with the `Manager`

The `Manager` gives each actor its own OS thread (with optional Linux CPU pinning / RT priority
via `ThreadConfig`).

```rust
use actors2::{Manager, ThreadConfig};

fn main() {
    let mut mgr = Manager::new();
    let handle = mgr.get_handle();               // for orderly shutdown from inside an actor

    let pong = mgr.manage("Pong", Box::new(PongActor), ThreadConfig::default());
    let ping = mgr.manage("Ping", Box::new(PingActor { pong, handle }), ThreadConfig::default());

    mgr.init();   // spawn one thread per actor, deliver each a `Start`
    mgr.run();    // block until an actor calls ManagerHandle::terminate()
    mgr.end();    // Shutdown all actors and join their threads
}
```

- `manage(name, Box::new(actor), cfg) -> ActorRef` — register an actor; the returned `ActorRef`
  is usable immediately (you can `fast_send` to it before `init`).
- `ThreadConfig { affinity: Vec<usize>, priority: i32 }` — core pinning + `SCHED_FIFO` priority
  (Linux only; a no-op elsewhere).
- `ManagerHandle::terminate()` — from inside any actor, broadcast Shutdown and unblock `run()`.
- `Manager` also cleans up on drop, so a forgotten `end()` won't leak threads.

## 6. A minimal complete program

See [`examples/ping_pong.rs`](examples/ping_pong.rs) for a runnable two-actor version, and
[`examples/matching_engine.rs`](examples/matching_engine.rs) for a substantial one (an order-book
matching engine — its core is a plain, framework-agnostic struct, wrapped by a one-actor
adapter; see [`MATCHING_ENGINE.md`](MATCHING_ENGINE.md)).

## Gotchas / limits

- Message ids: unique per type, `< 1024`, `< 16` reserved. A duplicate id routes to the wrong
  handler (debug builds assert; release drops the message).
- `actors2` is **in-process only** — there is no remote/ZMQ transport or actor registry here.
- The mailbox is blocking (`Mutex`+`Condvar`); the fast path is `fast_send` (no queue).
