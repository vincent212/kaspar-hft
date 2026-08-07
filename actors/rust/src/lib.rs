// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! `actors` — a performance-first Rust port of the C++ Kaspar actor framework.
//!
//! Design goals (ported from `actors/cpp`):
//! - **On-stack `fast_send`**: synchronous, inline, any actor → any actor.
//! - **Integer-id O(1) dispatch** via `handle_messages!`.
//! - **`BQueue`** preallocated ring + overflow mailbox for async `send`.
//! - Pool allocator (Phase 4) and CPU pinning (Linux) for latency.

pub mod actor;
#[cfg(feature = "interop")]
pub mod interop;
pub mod manager;
pub mod message;
pub mod messages;
pub mod owner;
pub mod pool;
pub mod queue;

pub use actor::{Actor, ActorContext, ActorRef, HandlerFn, ACTOR_BQUEUE_SIZE, HANDLER_CACHE_SIZE};
pub use manager::{Manager, ManagerHandle, ThreadConfig};
pub use message::{Message, MsgId};
pub use messages::{Shutdown, Start};
pub use owner::MsgBox;
pub use pool::PooledMessage;
pub use queue::BQueue;
