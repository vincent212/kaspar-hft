// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Licensed under the MIT License. See LICENSE file in the project root.

//! Tests for the review-fix batch: poison non-cascade, manage-after-init,
//! and Manager Drop cleanup.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use actors2::{define_message, handle_messages, ActorContext, Manager, ThreadConfig};

struct Ping;
define_message!(Ping, 10);
struct Pong;
define_message!(Pong, 11);
struct Boom;
define_message!(Boom, 12);

// Actor that panics on Boom, replies on Ping.
struct Flaky;
impl Flaky {
    fn on_boom(&mut self, _m: &Boom, _ctx: &mut ActorContext) {
        panic!("boom");
    }
    fn on_ping(&mut self, _m: &Ping, ctx: &mut ActorContext) {
        ctx.reply(Box::new(Pong));
    }
}
handle_messages!(Flaky, Boom => on_boom, Ping => on_ping);

// Actor that counts Pings into a shared counter.
struct Counter {
    c: Arc<AtomicUsize>,
}
impl Counter {
    fn on_ping(&mut self, _m: &Ping, _ctx: &mut ActorContext) {
        self.c.fetch_add(1, Ordering::Relaxed);
    }
}
handle_messages!(Counter, Ping => on_ping);

#[test]
fn handler_panic_does_not_poison_actor() {
    // Regression for the poison-cascade finding: a panicking handler must not
    // brick the actor for later callers.
    let mut mgr = Manager::new();
    let a = mgr.manage("flaky", Box::new(Flaky), ThreadConfig::default());

    // Silence the default panic hook for the deliberately-panicking call.
    let prev = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| a.fast_send(&Boom, None)));
    std::panic::set_hook(prev);
    assert!(r.is_err(), "the Boom handler should have panicked");

    // The lock was poisoned by that panic; a subsequent fast_send must still
    // work because we recover the poisoned guard instead of unwrapping.
    let reply = a.fast_send(&Ping, None);
    assert!(reply.is_some(), "actor must survive a prior handler panic");
}

#[test]
fn manage_after_init_actor_is_live() {
    // Regression: an actor registered AFTER init() must actually run (not be a
    // silent dead actor whose async messages are dropped).
    let c = Arc::new(AtomicUsize::new(0));
    let mut mgr = Manager::new();
    mgr.init(); // no actors yet -> marks started

    let a = mgr.manage("late", Box::new(Counter { c: c.clone() }), ThreadConfig::default());
    for _ in 0..100 {
        a.send(Box::new(Ping), None);
    }
    // end() pushes Shutdown behind the 100 FIFO Pings and joins, so all 100 are
    // processed before the actor stops.
    mgr.end();
    assert_eq!(c.load(Ordering::Relaxed), 100);
}

#[test]
fn manager_drop_shuts_down_without_end() {
    // Regression: dropping a Manager without calling end() must still join the
    // actor threads (not detach+leak them). If Drop failed to shut down, this
    // test would hang.
    let c = Arc::new(AtomicUsize::new(0));
    {
        let mut mgr = Manager::new();
        let a = mgr.manage("c", Box::new(Counter { c: c.clone() }), ThreadConfig::default());
        mgr.init();
        a.send(Box::new(Ping), None);
        // no end(); scope exit drops mgr -> Drop calls end() -> joins threads.
    }
    // Reaching here means Drop completed without hanging.
    assert_eq!(c.load(Ordering::Relaxed), 1);
}
