// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Latency benchmark for on-stack `fast_send`.
//!
//! Run: `cargo run --release --example bench_fast_send`
//!
//! Reports two numbers:
//! - **bulk mean**: total elapsed / N — free of per-call timer overhead (the
//!   honest "cost of a fast_send").
//! - **percentiles**: min/p50/p99/max from per-call `Instant` sampling. NOTE
//!   these include ~2× `Instant::now()` overhead per sample, so treat them as an
//!   upper bound for a sub-100ns operation.

use std::hint::black_box;
use std::time::Instant;

use actors2::{define_message, define_pooled_message, handle_messages, ActorContext, Manager, MsgBox, ThreadConfig};

struct Ping {
    n: u64,
}
define_message!(Ping, 10);

struct Pong {
    n: u64,
}
// Pooled reply so fast_send's reply allocation comes from the object pool.
define_pooled_message!(Pong, 11, 64);

/// Replies to Ping with Pong. Never started on its own thread — `fast_send`
/// runs its handler inline on the caller's thread.
struct PongActor;
impl PongActor {
    fn on_ping(&mut self, msg: &Ping, ctx: &mut ActorContext) {
        ctx.reply(MsgBox::pooled(Pong { n: msg.n }));
    }
}
handle_messages!(PongActor, Ping => on_ping);

fn report(label: &str, samples: &mut [u64]) {
    samples.sort_unstable();
    let n = samples.len();
    let pct = |p: f64| samples[((n as f64 * p) as usize).min(n - 1)];
    let sum: u128 = samples.iter().map(|&x| x as u128).sum();
    println!(
        "{label:<22} n={n}  min={:>4}  p50={:>4}  p99={:>5}  max={:>7}  mean(sampled)={:>4} ns",
        samples[0],
        pct(0.50),
        pct(0.99),
        samples[n - 1],
        (sum / n as u128) as u64,
    );
}

fn main() {
    println!("=== actors2 fast_send latency benchmark ===");

    let mut mgr = Manager::new();
    // manage() returns a usable ActorRef without starting a thread.
    let pong = mgr.manage("Pong", Box::new(PongActor), ThreadConfig::default());

    let iters: u64 = 2_000_000;

    // Warmup (populate the dispatch table, warm caches/branch predictor).
    for i in 0..200_000u64 {
        let reply = pong.fast_send(black_box(&Ping { n: i }), None);
        black_box(reply);
    }

    // --- Bulk mean: single timer around the whole loop (no per-call overhead) ---
    let t0 = Instant::now();
    let mut acc = 0u64;
    for i in 0..iters {
        let msg = Ping { n: i }; // lives on the stack
        let reply = pong.fast_send(black_box(&msg), None);
        // Touch the reply so the whole thing can't be optimized away.
        if let Some(r) = reply {
            acc = acc.wrapping_add(r.message_id() as u64);
        }
    }
    let bulk = t0.elapsed();
    black_box(acc);
    println!(
        "fast_send bulk mean    n={iters}  {:.2} ns/call  ({:.2} M calls/sec)",
        bulk.as_nanos() as f64 / iters as f64,
        iters as f64 / bulk.as_secs_f64() / 1e6,
    );

    // --- Percentiles: per-call sampling (includes timer overhead) ---
    let sample_n = 1_000_000usize;
    let mut lat = Vec::with_capacity(sample_n);
    for i in 0..sample_n as u64 {
        let msg = Ping { n: i };
        let t = Instant::now();
        let reply = pong.fast_send(black_box(&msg), None);
        let ns = t.elapsed().as_nanos() as u64;
        black_box(reply);
        lat.push(ns);
    }
    report("fast_send sampled", &mut lat);

    println!("(note: sampled percentiles include ~2x Instant::now() overhead)");
}
