// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! Async ping-pong across two actors (each on its own thread) — validates the
//! `BQueue` async `send` + `reply` path and Manager lifecycle.
//!
//! Run: `cargo run --release --example ping_pong`

use actors2::{define_message, handle_messages, ActorContext, ActorRef, Manager, ManagerHandle, Start, ThreadConfig};

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
    max: i32,
}
impl PingActor {
    fn on_start(&mut self, _m: &Start, ctx: &mut ActorContext) {
        self.pong.send(Box::new(Ping { count: 1 }), ctx.self_ref());
    }
    fn on_pong(&mut self, m: &Pong, ctx: &mut ActorContext) {
        if m.count >= self.max {
            println!("PingActor: done at {}", m.count);
            self.handle.terminate();
        } else {
            self.pong
                .send(Box::new(Ping { count: m.count + 1 }), ctx.self_ref());
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
    println!("=== actors2 async ping-pong ===");
    let mut mgr = Manager::new();
    let handle = mgr.get_handle();

    let pong = mgr.manage("Pong", Box::new(PongActor), ThreadConfig::default());
    let ping = PingActor {
        pong,
        handle,
        max: 1_000_000,
    };
    let _ping_ref = mgr.manage("Ping", Box::new(ping), ThreadConfig::default());

    let t0 = std::time::Instant::now();
    mgr.init();
    mgr.run();
    let elapsed = t0.elapsed();
    mgr.end();

    println!(
        "1,000,000 async round-trips in {:.3?}  ({:.2} M round-trips/sec)",
        elapsed,
        1_000_000.0 / elapsed.as_secs_f64() / 1e6
    );
}
