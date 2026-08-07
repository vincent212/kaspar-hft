// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Cross-language C++/Rust actor interop — in-process, over a C ABI.
//!
//! Feature-gated (`interop`). The design principle is **location transparency**:
//! an actor calls `ref.send(...)` / `ref.fast_send(...)` and never knows the peer
//! is C++. This module is the framework-side **plumbing**; it is deliberately
//! message-agnostic — the per-message marshalling (`to_c_struct`/`from_c_struct`
//! and the id dispatch) lives in generated (or hand-written) glue that fills in
//! the callbacks below.
//!
//! Directions:
//! - **Rust → C++:** an [`ActorRef::Cpp`] holds a [`CppRef`] whose `send_fn`/
//!   `fast_fn` marshal the message and call the C++ FFI entry points.
//! - **C++ → Rust:** the exported [`rust_actor_send`] / [`rust_actor_fast_send`] /
//!   [`rust_actor_exists`] are called by the C++ bridge and forwarded to the
//!   registered inbound callbacks.
//!
//! (Phase 1: the plumbing + a full round-trip path, exercised by `tests/interop.rs`
//! with a pure-Rust mock of the C++ side. Codegen + reply routing land in later
//! phases.)

use std::ffi::CStr;
use std::os::raw::{c_char, c_int, c_void};
use std::sync::OnceLock;

use crate::actor::ActorRef;
use crate::message::Message;
use crate::owner::MsgBox;

/// A Rust function that marshals a `rust2` message to its C struct and calls the
/// matching C++ FFI entry point. Supplied by the generated/hand-written glue, so
/// the framework core never names concrete message types.
///
/// Returns 0 on success, negative on error (unknown message id, etc.).
pub type CppSendFn = fn(target: &str, sender: &str, msg: &dyn Message) -> i32;

/// A handle to a C++ actor reachable over FFI. Held inside [`ActorRef::Cpp`].
#[derive(Clone)]
pub struct CppRef {
    target: Box<str>,
    sender: Box<str>,
    send_fn: CppSendFn,
    fast_fn: CppSendFn,
}

impl CppRef {
    /// `target` is the C++ actor's name; `sender` is the local Rust actor's name
    /// (attached so the C++ side can route replies back).
    pub fn new(target: &str, sender: &str, send_fn: CppSendFn, fast_fn: CppSendFn) -> Self {
        CppRef { target: target.into(), sender: sender.into(), send_fn, fast_fn }
    }

    pub fn name(&self) -> &str {
        &self.target
    }

    /// Async send: marshal + call into C++. The `MsgBox` is dropped after the call
    /// — C++ copies the POD, so no ownership crosses the boundary.
    pub fn send(&self, msg: MsgBox, _sender: Option<ActorRef>) {
        (self.send_fn)(&self.target, &self.sender, msg.as_message());
    }

    /// Inline send: run the C++ handler on this thread over FFI. No reply can cross
    /// the C ABI, so this returns `None` (delivered, no synchronous reply).
    pub fn fast_send(&self, msg: &dyn Message, _sender: Option<ActorRef>) -> Option<MsgBox> {
        (self.fast_fn)(&self.target, &self.sender, msg);
        None
    }
}

/// Build a `Cpp` actor ref (used by the lookup glue).
pub fn cpp_ref(target: &str, sender: &str, send_fn: CppSendFn, fast_fn: CppSendFn) -> ActorRef {
    ActorRef::Cpp(CppRef::new(target, sender, send_fn, fast_fn))
}

// ---------------------------------------------------------------------------
// Rust -> C++ : location-transparent lookup
// ---------------------------------------------------------------------------

type CppLookupFn = fn(name: &str, sender: &str) -> Option<ActorRef>;
static CPP_LOOKUP: OnceLock<CppLookupFn> = OnceLock::new();

/// Register how to resolve a name to a C++ actor (returning an [`ActorRef::Cpp`]).
/// Called once at startup by the glue; `Manager::get_ref` uses it after a local miss.
///
/// Returns `true` if this call installed the resolver, `false` if one was already
/// registered (registration is install-once; a second call is a no-op).
pub fn register_cpp_lookup(f: CppLookupFn) -> bool {
    CPP_LOOKUP.set(f).is_ok()
}

/// Resolve a C++ actor by name (used by `Manager::get_ref`).
pub(crate) fn resolve_cpp(name: &str, sender: &str) -> Option<ActorRef> {
    CPP_LOOKUP.get().and_then(|f| f(name, sender))
}

// ---------------------------------------------------------------------------
// C++ -> Rust : inbound delivery, exported over the C ABI
// ---------------------------------------------------------------------------

/// Delivers an inbound C++ message to a Rust actor: `from_c_struct` + look up the
/// target + enqueue (`fast == false`) or run inline (`fast == true`). Supplied by
/// the glue (it knows the message set). Returns 0 on success, negative on error.
///
/// # Safety
/// `data` must point to the C struct matching `msg_id` for the call's duration.
pub type InboundFn = fn(name: &str, sender: &str, msg_id: i32, data: *const c_void, fast: bool) -> i32;
static INBOUND: OnceLock<InboundFn> = OnceLock::new();

/// Register the inbound dispatcher used by `rust_actor_send`/`rust_actor_fast_send`.
/// Returns `true` if installed, `false` if one was already registered (install-once).
pub fn register_inbound(f: InboundFn) -> bool {
    INBOUND.set(f).is_ok()
}

type RustExistsFn = fn(name: &str) -> bool;
static RUST_EXISTS: OnceLock<RustExistsFn> = OnceLock::new();

/// Register the "does this Rust actor exist?" callback used by `rust_actor_exists`.
/// Returns `true` if installed, `false` if one was already registered (install-once).
pub fn register_rust_exists(f: RustExistsFn) -> bool {
    RUST_EXISTS.set(f).is_ok()
}

/// # Safety: `p` must be null or a valid NUL-terminated C string.
unsafe fn cstr<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        ""
    } else {
        CStr::from_ptr(p).to_str().unwrap_or("")
    }
}

/// Status returned across the C ABI when a Rust callback panics. A panic must NOT
/// unwind past an `extern "C"` frame (it aborts the process on modern toolchains
/// and is UB on older ones), and one bad message handler must not take down the
/// whole process — so we catch it at the boundary and report an error code.
const FFI_PANIC: c_int = -3;

/// Run `f` at the FFI boundary, catching any panic and returning `fallback`.
fn guard(fallback: c_int, f: impl FnOnce() -> c_int) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or(fallback)
}

/// C++ → Rust: async send. Called by the C++ bridge.
///
/// # Safety
/// `name`/`sender` are NUL-terminated C strings (or null); `data` points to the C
/// struct for `msg_id`, valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn rust_actor_send(
    name: *const c_char,
    sender: *const c_char,
    msg_id: c_int,
    data: *const c_void,
) -> c_int {
    guard(FFI_PANIC, || match INBOUND.get() {
        Some(f) => f(cstr(name), cstr(sender), msg_id, data, false),
        None => -1,
    })
}

/// C++ → Rust: inline (fast) send. Called by the C++ bridge.
///
/// # Safety
/// Same as [`rust_actor_send`].
#[no_mangle]
pub unsafe extern "C" fn rust_actor_fast_send(
    name: *const c_char,
    sender: *const c_char,
    msg_id: c_int,
    data: *const c_void,
) -> c_int {
    guard(FFI_PANIC, || match INBOUND.get() {
        Some(f) => f(cstr(name), cstr(sender), msg_id, data, true),
        None => -1,
    })
}

/// C++ → Rust: existence check. Called by the C++ bridge.
///
/// # Safety
/// `name` is a NUL-terminated C string (or null).
#[no_mangle]
pub unsafe extern "C" fn rust_actor_exists(name: *const c_char) -> c_int {
    // On panic report 0 (not-exists): a nonzero code would be read as "exists".
    guard(0, || match RUST_EXISTS.get() {
        Some(f) if f(cstr(name)) => 1,
        _ => 0,
    })
}
