// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Messages carry a **compile-time integer ID** used as the O(1) dispatch key —
//! the Rust analog of the C++ `Message_N<N>` / `handler_cache` design.

use std::any::Any;

/// Compile-time message id. Implemented by `define_message!`.
///
/// Kept as a separate trait so the id is available as an associated `const`
/// (usable when building the dispatch table), in addition to the per-instance
/// `Message::message_id()` used when dispatching a `&dyn Message`.
pub trait MsgId {
    const ID: u32;
}

/// Trait for all messages. `Send + 'static` so they can cross threads via `send`.
pub trait Message: Any + Send + 'static {
    /// For downcasting to the concrete type in a handler.
    fn as_any(&self) -> &dyn Any;
    /// The message's integer id (the dispatch key).
    fn message_id(&self) -> u32;
}

/// Implement `Message` + `MsgId` for a type with a fixed integer id.
///
/// ```ignore
/// struct Ping { count: i32 }
/// define_message!(Ping, 10);
/// ```
///
/// Ids must be `< actors2::HANDLER_CACHE_SIZE`. Ids `< 16` are reserved for
/// framework messages (see `messages.rs`).
#[macro_export]
macro_rules! define_message {
    ($name:ty, $id:expr) => {
        impl $crate::MsgId for $name {
            const ID: u32 = $id;
        }
        impl $crate::Message for $name {
            #[inline(always)]
            fn as_any(&self) -> &dyn ::std::any::Any {
                self
            }
            #[inline(always)]
            fn message_id(&self) -> u32 {
                $id
            }
        }
    };
}
