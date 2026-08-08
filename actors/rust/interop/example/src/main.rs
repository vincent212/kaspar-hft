// Hybrid C++/Rust interop benchmark: a Rust actor fast_sends to a real C++ actor
// across the C-ABI FFI bridge, in one process. Measures the cross-language cost,
// alongside a same-language (Rust->Rust) baseline in the same binary.
//
// The C++ side (cpp/hybrid_cpp.cpp) sets up a C++ Manager + a msg::Ping-handling
// actor named "cpp_pong" and calls cpp_actor_init(). The Rust side resolves it as
// an ActorRef::Cpp and calls fast_send in a loop.

use std::hint::black_box;
use std::time::Instant;

use actors::interop::generated::{register, Ping};
use actors::{handle_messages, ActorContext, Manager, ThreadConfig};

extern "C" {
    fn hybrid_setup(); // implemented in cpp/hybrid_cpp.cpp
}

// A same-language Rust receiver: trivial work, no reply — mirrors the C++ actor.
struct RustPong {
    acc: u64,
}
impl RustPong {
    fn on_ping(&mut self, m: &Ping, _c: &mut ActorContext) {
        self.acc = self.acc.wrapping_add(m.count as u64);
    }
}
handle_messages!(RustPong, Ping => on_ping);

fn time_loop(n: i32, r: &actors::ActorRef) -> f64 {
    for i in 0..200_000i32 { black_box(r.fast_send(black_box(&Ping { count: i }), None)); }
    let t = Instant::now();
    for i in 0..n { black_box(r.fast_send(black_box(&Ping { count: i }), None)); }
    t.elapsed().as_nanos() as f64 / n as f64
}

fn main() {
    let n: i32 = 5_000_000;

    // Baseline: Rust -> Rust fast_send (same process, same language, no reply).
    let mut lmgr = Manager::new();
    let rust_pong = lmgr.manage("rust_pong", Box::new(RustPong { acc: 0 }), ThreadConfig::default());
    let base = time_loop(n, &rust_pong);

    // Interop: Rust -> C++ fast_send across the C-ABI bridge.
    unsafe { hybrid_setup(); }        // C++: Manager + "cpp_pong" actor + cpp_actor_init
    register();                        // install the generated inbound dispatcher + C++ resolver
    let mgr = Manager::new();
    let cpp = mgr.get_ref("cpp_pong", "rust_driver").expect("cpp_pong not resolvable over FFI");
    let ffi = time_loop(n, &cpp);

    println!();
    println!("fast_send, no reply, {} calls (Apple Silicon, single thread):", n);
    println!("  Rust -> Rust  (same language):   {:6.1} ns/call", base);
    println!("  Rust -> C++   (over FFI bridge): {:6.1} ns/call   ({:.1}x, +{:.0} ns)",
             ffi, ffi / base, ffi - base);
}
