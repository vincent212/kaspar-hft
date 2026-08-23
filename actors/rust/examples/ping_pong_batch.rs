// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Batched async ping-pong — Ping fires a burst of BATCH messages at Pong before
//! waiting for any reply, so both mailboxes go deep (~BATCH backlog). This is the
//! workload where BQueue batch-drain (one lock + one notify per burst instead of
//! per message) is supposed to pay off. Contrast with `ping_pong` (depth-1).
//!
//! Run: `cargo run --release --example ping_pong_batch`

use actors::{define_message, handle_messages, ActorContext, ActorRef, Manager, ManagerHandle, Start, ThreadConfig};

const BATCH: i32 = 10_000; // messages sent per burst
const ROUNDS: i32 = 1_000; // number of bursts  => BATCH*ROUNDS round-trips

struct Ping {
    count: i32,
}
define_message!(Ping, 10);

struct Pong {
    count: i32,
}
define_message!(Pong, 11);

struct PingActor {
    pong: ActorRef,
    handle: ManagerHandle,
    recv: i32,
    bursts_sent: i32,
}
impl PingActor {
    fn fire(&mut self, ctx: &mut ActorContext) {
        for _ in 0..BATCH {
            self.pong.send(Box::new(Ping { count: 1 }), ctx.self_ref());
        }
        self.bursts_sent += 1;
    }
    fn on_start(&mut self, _m: &Start, ctx: &mut ActorContext) {
        self.fire(ctx);
    }
    fn on_pong(&mut self, _m: &Pong, ctx: &mut ActorContext) {
        self.recv += 1;
        if self.recv >= BATCH * ROUNDS {
            self.handle.terminate();
            return;
        }
        // once a full burst has come back, launch the next one
        if self.recv % BATCH == 0 && self.bursts_sent < ROUNDS {
            self.fire(ctx);
        }
    }
}
handle_messages!(PingActor, Start => on_start, Pong => on_pong);

struct PongActor;
impl PongActor {
    fn on_ping(&mut self, m: &Ping, ctx: &mut ActorContext) {
        ctx.reply(Box::new(Pong { count: m.count }));
    }
}
handle_messages!(PongActor, Ping => on_ping);

fn main() {
    let total = (BATCH as i64) * (ROUNDS as i64);
    println!("=== batched ping-pong: burst={BATCH}, bursts={ROUNDS}, round-trips={total} ===");
    let mut mgr = Manager::new();
    let handle = mgr.get_handle();

    let pong = mgr.manage("Pong", Box::new(PongActor), ThreadConfig::default());
    let ping = PingActor { pong, handle, recv: 0, bursts_sent: 0 };
    let _ping_ref = mgr.manage("Ping", Box::new(ping), ThreadConfig::default());

    let t0 = std::time::Instant::now();
    mgr.init();
    mgr.run();
    let elapsed = t0.elapsed();
    mgr.end();

    let msgs = (total * 2) as f64; // pings + pongs
    println!(
        "{total} round-trips in {:.3?}  ({:.2} M round-trips/s, {:.1} M msgs/s)",
        elapsed,
        total as f64 / elapsed.as_secs_f64() / 1e6,
        msgs / elapsed.as_secs_f64() / 1e6,
    );
}
