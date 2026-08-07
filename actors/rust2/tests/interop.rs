// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Licensed under the MIT License. See LICENSE file in the project root.

//! Phase-1 interop plumbing test. Exercises the full round-trip with a **pure-Rust
//! mock of the C++ side** (no C++ build needed): outbound `send`/`fast_send` through
//! an `ActorRef::Cpp`, inbound delivery via the exported `rust_actor_send`, and
//! location-transparent `Manager::get_ref`. Run: `cargo test --features interop`.

#![cfg(feature = "interop")]

use std::ffi::CString;
use std::os::raw::c_void;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

use actors2::interop::{
    cpp_ref, register_cpp_lookup, register_inbound, register_rust_exists, rust_actor_exists,
    rust_actor_send,
};
use actors2::{define_message, handle_messages, ActorContext, ActorRef, Manager, Message, ThreadConfig};

// A message + its POD C-struct (hand-written here; codegen produces this in Phase 2).
struct Ping {
    seq: u64,
}
define_message!(Ping, 1000);

#[repr(C)]
struct CPing {
    seq: u64,
}
impl Ping {
    fn to_c(&self) -> CPing {
        CPing { seq: self.seq }
    }
    fn from_c(c: &CPing) -> Ping {
        Ping { seq: c.seq }
    }
}

// ---------------------------------------------------------------------------
// Rust -> C++ : a mock `CppSendFn` that records instead of calling real FFI.
// ---------------------------------------------------------------------------
static SENT: Mutex<Vec<(String, i32, u64)>> = Mutex::new(Vec::new());

fn mock_cpp_send(target: &str, _sender: &str, msg: &dyn Message) -> i32 {
    match msg.message_id() as i32 {
        1000 => {
            let p = msg.as_any().downcast_ref::<Ping>().unwrap();
            let c = p.to_c(); // (a real send_fn would call `extern "C" cpp_actor_send(.., &c)`)
            SENT.lock().unwrap().push((target.to_string(), 1000, c.seq));
            0
        }
        _ => -2,
    }
}

#[test]
fn rust_to_cpp_send_and_fast_send() {
    let r: ActorRef = cpp_ref("cpp_actor", "rust_me", mock_cpp_send, mock_cpp_send);
    assert_eq!(r.name(), "cpp_actor");

    r.send(Box::new(Ping { seq: 42 }), None);
    // fast_send to a C++ actor delivers but returns no reply across the C ABI.
    assert!(r.fast_send(&Ping { seq: 7 }, None).is_none());

    let sent = SENT.lock().unwrap();
    assert!(sent.contains(&("cpp_actor".to_string(), 1000, 42)));
    assert!(sent.contains(&("cpp_actor".to_string(), 1000, 7)));
}

// ---------------------------------------------------------------------------
// C++ -> Rust : inbound delivery to a real Rust actor via `rust_actor_send`.
// ---------------------------------------------------------------------------
static GOT: AtomicU64 = AtomicU64::new(0);
static REC_REF: Mutex<Option<ActorRef>> = Mutex::new(None);

struct Recorder;
impl Recorder {
    fn on_ping(&mut self, m: &Ping, _c: &mut ActorContext) {
        GOT.store(m.seq, Ordering::SeqCst);
    }
}
handle_messages!(Recorder, Ping => on_ping);

fn mock_inbound(_name: &str, _sender: &str, msg_id: i32, data: *const c_void, _fast: bool) -> i32 {
    match msg_id {
        1000 => {
            let c = unsafe { &*(data as *const CPing) };
            let ping = Ping::from_c(c);
            match REC_REF.lock().unwrap().clone() {
                Some(r) => {
                    r.send(Box::new(ping), None);
                    0
                }
                None => -1,
            }
        }
        _ => -2,
    }
}

#[test]
fn cpp_to_rust_inbound_delivery() {
    let mut mgr = Manager::new();
    let rec = mgr.manage("rec", Box::new(Recorder), ThreadConfig::default());
    *REC_REF.lock().unwrap() = Some(rec);
    register_inbound(mock_inbound);
    register_rust_exists(|name| name == "rec");
    mgr.init();

    // Simulate the C++ bridge calling into Rust.
    let c = CPing { seq: 99 };
    let name = CString::new("rec").unwrap();
    let rc =
        unsafe { rust_actor_send(name.as_ptr(), std::ptr::null(), 1000, &c as *const _ as *const c_void) };
    assert_eq!(rc, 0);

    // Existence check.
    assert_eq!(unsafe { rust_actor_exists(name.as_ptr()) }, 1);
    let bad = CString::new("nope").unwrap();
    assert_eq!(unsafe { rust_actor_exists(bad.as_ptr()) }, 0);

    mgr.end(); // FIFO flush + join: Ping is processed before Shutdown
    assert_eq!(GOT.load(Ordering::SeqCst), 99);
}

// ---------------------------------------------------------------------------
// Location transparency: Manager::get_ref resolves a C++ actor via the lookup.
// ---------------------------------------------------------------------------
#[test]
fn manager_get_ref_resolves_cpp() {
    register_cpp_lookup(|name, sender| {
        if name == "cpp_pub" {
            Some(cpp_ref(name, sender, mock_cpp_send, mock_cpp_send))
        } else {
            None
        }
    });
    let mgr = Manager::new();
    let r = mgr.get_ref("cpp_pub", "me").expect("cpp actor resolved");
    assert!(matches!(r, ActorRef::Cpp(_)));
    assert_eq!(r.name(), "cpp_pub");
    assert!(mgr.get_ref("missing", "me").is_none());
}
