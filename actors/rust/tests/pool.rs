// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Licensed under the MIT License. See LICENSE file in the project root.

//! Tests for the per-type object pool + `MsgBox`.

use actors::{define_pooled_message, Message, MsgBox};

struct P {
    v: u64,
}
define_pooled_message!(P, 100, 8);

#[test]
fn pooled_roundtrip_and_reuse() {
    let a = MsgBox::pooled(P { v: 1 });
    assert_eq!(a.message_id(), 100);
    assert_eq!(a.as_message().as_any().downcast_ref::<P>().unwrap().v, 1);

    // Record the block address, free it, allocate again — the thread-local
    // free-list is LIFO, so we must get the very same block back.
    let pa = (a.as_message() as *const dyn Message).cast::<u8>();
    drop(a);
    let b = MsgBox::pooled(P { v: 2 });
    let pb = (b.as_message() as *const dyn Message).cast::<u8>();
    assert_eq!(pa, pb, "pool should reuse the just-freed block (LIFO)");
    assert_eq!(b.as_message().as_any().downcast_ref::<P>().unwrap().v, 2);
}

#[test]
fn pooled_many_alloc_free_is_correct() {
    for i in 0..200_000u64 {
        let m = MsgBox::pooled(P { v: i });
        assert_eq!(m.as_message().as_any().downcast_ref::<P>().unwrap().v, i);
        // dropped here -> returned to pool
    }
}

#[test]
fn pooled_survives_cross_thread_free() {
    // Allocate on one thread, move to another, drop there. The block routes
    // back through that thread's freed-cache/shared list without corruption.
    let handles: Vec<_> = (0..4)
        .map(|_| {
            std::thread::spawn(|| {
                let mut boxes = Vec::new();
                for i in 0..10_000u64 {
                    boxes.push(MsgBox::pooled(P { v: i }));
                }
                for (i, b) in boxes.iter().enumerate() {
                    assert_eq!(
                        b.as_message().as_any().downcast_ref::<P>().unwrap().v,
                        i as u64
                    );
                }
                // boxes dropped here
            })
        })
        .collect();
    for h in handles {
        h.join().unwrap();
    }
}
