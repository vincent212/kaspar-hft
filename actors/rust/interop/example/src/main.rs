// Hybrid C++/Rust interop benchmark. Measures fast_send (no reply) across the
// full 2x2 matrix — every sender/receiver combination of C++ and Rust actors, in
// one process:
//     Rust -> Rust   (same language)       Rust -> C++   (over the FFI bridge)
//     C++  -> Rust   (over the FFI bridge)  C++  -> C++   (same language)
//
// The C++ side (cpp/hybrid_cpp.cpp) provides the C++ actor "cpp_pong" and the
// C++-driven half; the Rust side provides "rust_pong" and the Rust-driven half.

use std::hint::black_box;
use std::time::Instant;

use actors::interop::generated::{register, Ping};
use actors::interop::register_local_lookup;
use actors::{handle_messages, ActorContext, ActorRef, Manager, ThreadConfig};

extern "C" {
    fn hybrid_setup(); // C++: Manager + "cpp_pong" + cpp_actor_init
    fn run_cpp_bench(n: i64, cpp_cpp_ns: *mut f64, cpp_rust_ns: *mut f64); // C++-driven half
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

fn time_loop(n: i32, r: &ActorRef) -> f64 {
    for i in 0..200_000i32 { black_box(r.fast_send(black_box(&Ping { count: i }), None)); }
    let t = Instant::now();
    for i in 0..n { black_box(r.fast_send(black_box(&Ping { count: i }), None)); }
    t.elapsed().as_nanos() as f64 / n as f64
}

fn main() {
    let n: i32 = 5_000_000;

    // Rust manager + a Rust actor "rust_pong".
    let mut lmgr = Manager::new();
    let rust_pong = lmgr.manage("rust_pong", Box::new(RustPong { acc: 0 }), ThreadConfig::default());

    // Wire the interop both directions.
    unsafe { hybrid_setup(); }                 // C++ side: Manager + "cpp_pong" + cpp_actor_init
    register();                                 // install generated inbound dispatch + C++ resolver
    let handle = lmgr.get_handle();
    register_local_lookup(move |name| handle.get_ref_local(name)); // so C++ -> Rust finds "rust_pong"

    // Rust-driven half.
    let rr = time_loop(n, &rust_pong); // Rust -> Rust
    let cpp = lmgr.get_ref("cpp_pong", "rust_driver").expect("cpp_pong not resolvable over FFI");
    let rc = time_loop(n, &cpp); // Rust -> C++ (over FFI)

    // C++-driven half (timed in C++).
    let (mut cc, mut cr) = (0.0f64, 0.0f64);
    unsafe { run_cpp_bench(n as i64, &mut cc, &mut cr); } // C++ -> C++, C++ -> Rust

    println!();
    println!("fast_send, no reply, {n} calls (Apple Silicon, single thread, ns/call):");
    println!("                    ->  Rust receiver       ->  C++ receiver");
    println!("  Rust sender          {rr:5.1}  (same lang)      {rc:5.1}  (over FFI)");
    println!("  C++  sender          {cr:5.1}  (over FFI)       {cc:5.1}  (same lang)");
}
