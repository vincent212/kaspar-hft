// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Licensed under the MIT License. See LICENSE file in the project root.

//! Tests for `MsgBox` allocation/free, including the zero-sized-type heap path.

use actors2::{define_message, Message, MsgBox};

/// Zero-sized message (like the framework's `Start`/`Shutdown` signals).
struct Zst;
define_message!(Zst, 300);

struct Sized32 {
    a: u64,
    b: u64,
    c: u64,
    d: u64,
}
define_message!(Sized32, 301);

#[test]
fn heap_zst_alloc_free_no_ub() {
    // Regression: heap-allocating a ZST returns a dangling (never-allocated)
    // pointer; dropping it must NOT call dealloc with a zero-size layout, which
    // is UB. (Run under `cargo +nightly miri test` to actually catch a
    // regression here; this test at least exercises the path.)
    for _ in 0..1000 {
        let m = MsgBox::heap(Zst);
        assert_eq!(m.message_id(), 300);
        drop(m);
    }
}

#[test]
fn heap_sized_alloc_free_roundtrip() {
    for i in 0..1000u64 {
        let m = MsgBox::heap(Sized32 {
            a: i,
            b: i.wrapping_mul(2),
            c: i.wrapping_mul(3),
            d: i.wrapping_mul(4),
        });
        let s = m.as_message().as_any().downcast_ref::<Sized32>().unwrap();
        assert_eq!((s.a, s.b, s.c, s.d), (i, i.wrapping_mul(2), i.wrapping_mul(3), i.wrapping_mul(4)));
    }
}

#[test]
fn box_new_into_msgbox_still_works() {
    // The ergonomic path: `Box::new(x).into()` must still produce a valid MsgBox.
    let m: MsgBox = Box::new(Sized32 { a: 7, b: 8, c: 9, d: 10 }).into();
    assert_eq!(m.message_id(), 301);
    assert_eq!(m.as_message().as_any().downcast_ref::<Sized32>().unwrap().a, 7);
}
