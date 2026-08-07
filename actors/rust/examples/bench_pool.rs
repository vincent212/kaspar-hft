// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Licensed under the MIT License. See LICENSE file in the project root.

//! Allocation-latency benchmark: pooled vs heap message allocation.
//!
//! Run: `cargo run --release --example bench_pool`

use std::hint::black_box;
use std::time::Instant;

use actors::{define_message, define_pooled_message, MsgBox};

/// A message big enough to be representative of a real market-data payload.
struct HeapMsg {
    a: u64,
    b: u64,
    c: u64,
    d: u64,
}
define_message!(HeapMsg, 200);

struct PoolMsg {
    a: u64,
    b: u64,
    c: u64,
    d: u64,
}
define_pooled_message!(PoolMsg, 201, 64);

fn main() {
    println!("=== actors allocation benchmark: heap vs pool ===");
    let n: u64 = 5_000_000;

    // Warmup the pool so the slab is already carved.
    for i in 0..200_000u64 {
        let m = MsgBox::pooled(PoolMsg { a: i, b: i, c: i, d: i });
        black_box(&m);
    }

    // Read a field back through the allocation each iteration and accumulate,
    // so LLVM can't elide the alloc/free pair (allocation-removal) — otherwise
    // the numbers would measure the optimizer, not the allocator. The same
    // downcast+read cost is paid by both loops, so the comparison stays fair.
    let mut sink = 0u64;

    // --- Heap (global allocator) ---
    let t = Instant::now();
    for i in 0..n {
        let m = MsgBox::heap(HeapMsg { a: i, b: i, c: i, d: i });
        sink = sink.wrapping_add(
            m.as_message().as_any().downcast_ref::<HeapMsg>().unwrap().a,
        );
        drop(black_box(m)); // dropped -> global dealloc
    }
    let heap = t.elapsed();

    // --- Pool ---
    let t = Instant::now();
    for i in 0..n {
        let m = MsgBox::pooled(PoolMsg { a: i, b: i, c: i, d: i });
        sink = sink.wrapping_add(
            m.as_message().as_any().downcast_ref::<PoolMsg>().unwrap().a,
        );
        drop(black_box(m)); // dropped -> back to thread-local pool cache
    }
    let pool = t.elapsed();
    black_box(sink);

    let heap_ns = heap.as_nanos() as f64 / n as f64;
    let pool_ns = pool.as_nanos() as f64 / n as f64;
    println!("heap  alloc+free: {heap_ns:6.2} ns/op  ({:.1} M/s)", n as f64 / heap.as_secs_f64() / 1e6);
    println!("pool  alloc+free: {pool_ns:6.2} ns/op  ({:.1} M/s)", n as f64 / pool.as_secs_f64() / 1e6);
    println!("speedup: {:.2}x", heap_ns / pool_ns);
}
