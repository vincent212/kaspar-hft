// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Licensed under the MIT License. See LICENSE file in the project root.

//! Round-trip test for the GENERATED interop layer (Phase 2). Exercises the code
//! emitted by `interop/codegen/generate.py` — the wire/native structs, `to_c`/
//! `from_c`, the inbound dispatcher, and the outbound marshalling — against a
//! **pure-Rust mock of the C++ bridge** (the `cpp_actor_*` stubs below). No C++
//! build is needed. Run: `cargo test --features interop`.

#![cfg(feature = "interop")]

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::sync::Mutex;

use actors2::interop::generated::{register, CPing, DataRequest, Ping};
use actors2::interop::{register_local_lookup, rust_actor_send};
use actors2::{handle_messages, ActorContext, Manager, ThreadConfig};

// ---------------------------------------------------------------------------
// Mock C++ bridge: the symbols the generated outbound path calls. In a real
// hybrid build these come from the C++ side (interop/generated/cpp/*).
// ---------------------------------------------------------------------------
static OUT: Mutex<Vec<(i32, i32)>> = Mutex::new(Vec::new()); // (msg_id, Ping.count)

/// # Safety: `data` points to the wire struct for `id`.
#[no_mangle]
pub unsafe extern "C" fn cpp_actor_send(
    _actor: *const c_char,
    _sender: *const c_char,
    id: c_int,
    data: *const c_void,
) -> c_int {
    if id == 400 {
        let c = &*(data as *const CPing);
        OUT.lock().unwrap().push((id, c.count));
    }
    0
}

/// # Safety: same as [`cpp_actor_send`].
#[no_mangle]
pub unsafe extern "C" fn cpp_actor_fast_send(
    actor: *const c_char,
    sender: *const c_char,
    id: c_int,
    data: *const c_void,
) -> c_int {
    cpp_actor_send(actor, sender, id, data)
}

/// # Safety: `name` is a NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn cpp_actor_exists(name: *const c_char) -> c_int {
    let n = CStr::from_ptr(name).to_str().unwrap_or("");
    if n == "cpp_calc" {
        1
    } else {
        0
    }
}

// ---------------------------------------------------------------------------
// A Rust actor that records the inbound message.
// ---------------------------------------------------------------------------
static GOT_REQ: Mutex<Option<(i32, String)>> = Mutex::new(None);

struct Worker;
impl Worker {
    fn on_req(&mut self, m: &DataRequest, _ctx: &mut ActorContext) {
        *GOT_REQ.lock().unwrap() = Some((m.request_id, m.symbol.clone()));
    }
}
handle_messages!(Worker, DataRequest => on_req);

#[test]
fn generated_roundtrip_both_directions() {
    let mut mgr = Manager::new();
    mgr.manage("calc", Box::new(Worker), ThreadConfig::default());

    // The generated inbound dispatcher resolves local names through this.
    let handle = mgr.get_handle();
    register_local_lookup(move |n| handle.get_ref_local(n));
    register(); // install generated inbound + C++ lookup
    mgr.init();

    // --- inbound: simulate C++ marshalling a DataRequest into Rust ---
    // Build the wire struct the same way C++ would (to_c), then hand it to the
    // exported entry point as an opaque pointer.
    let wire = DataRequest { request_id: 7, symbol: "BTC-PERP".into() }.to_c();
    let name = CString::new("calc").unwrap();
    let rc = unsafe {
        rust_actor_send(name.as_ptr(), std::ptr::null(), 402, &wire as *const _ as *const c_void)
    };
    assert_eq!(rc, 0);

    // --- outbound: location-transparent get_ref resolves a C++ actor ---
    let cpp = mgr.get_ref("cpp_calc", "calc").expect("cpp actor resolved via lookup");
    assert_eq!(cpp.name(), "cpp_calc");
    cpp.send(Box::new(Ping { count: 5 }), None);
    assert!(cpp.fast_send(&Ping { count: 9 }, None).is_none()); // no reply across FFI

    mgr.end(); // flush + join so the inbound DataRequest is processed

    assert_eq!(*GOT_REQ.lock().unwrap(), Some((7, "BTC-PERP".to_string())));
    let out = OUT.lock().unwrap();
    assert!(out.contains(&(400, 5)), "async send marshalled: {:?}", *out);
    assert!(out.contains(&(400, 9)), "fast_send marshalled: {:?}", *out);
}
