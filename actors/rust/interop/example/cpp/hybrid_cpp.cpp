// C++ side of the hybrid interop benchmark: a real C++ actor that handles the
// interop msg::Ping, registered by name so the generated bridge can reach it,
// plus the C++-driven half of the benchmark (C++->C++ and C++->Rust).
#include <chrono>
#include <cstring>

#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "InteropMessages.hpp"   // msg::Ping, ::Ping (generated)
#include "CppActorBridge.hpp"    // cpp_actor_init
#include "RustActorIF.hpp"       // rust_actor_fast_send (into Rust)

using namespace actors;
using clk = std::chrono::steady_clock;

struct PongC : Actor {
    long count = 0;
    PongC() {
        std::strncpy(name, "cpp_pong", sizeof(name));
        MESSAGE_HANDLER(msg::Ping, on_ping);
    }
    // Trivial work, no reply — measures the dispatch into a C++ handler.
    void on_ping(const msg::Ping* m) { count += m->count; }
};

static Manager* g_mgr = nullptr;
static PongC* g_pong = nullptr;

extern "C" void hybrid_setup() {
    g_mgr = new Manager();
    g_pong = new PongC();
    g_mgr->add_to_manage_q(g_pong);   // registers "cpp_pong" by name (no thread needed for fast_send)
    cpp_actor_init(g_mgr);            // let the bridge find C++ actors via this Manager
}

static double ns_per(long n, clk::time_point t0) {
    return std::chrono::duration<double, std::nano>(clk::now() - t0).count() / (double)n;
}

// The C++-driven half: C++ -> C++ (same language) and C++ -> Rust (over the bridge,
// into a Rust actor named "rust_pong" that the Rust side has registered).
extern "C" void run_cpp_bench(long n, double* cpp_cpp_ns, double* cpp_rust_ns) {
    // C++ -> C++ : fast_send to the local C++ actor.
    {
        for (long i = 0; i < 200000; i++) { msg::Ping p; p.count = (int)i; auto r = g_pong->fast_send(&p, nullptr); (void)r; }
        auto t0 = clk::now();
        for (long i = 0; i < n; i++) { msg::Ping p; p.count = (int)i; auto r = g_pong->fast_send(&p, nullptr); (void)r; }
        *cpp_cpp_ns = ns_per(n, t0);
    }
    // C++ -> Rust : marshal a POD and call into the Rust actor over the FFI.
    {
        for (long i = 0; i < 200000; i++) { ::Ping c; c.count = (int)i; rust_actor_fast_send("rust_pong", nullptr, 400, &c); }
        auto t0 = clk::now();
        for (long i = 0; i < n; i++) { ::Ping c; c.count = (int)i; rust_actor_fast_send("rust_pong", nullptr, 400, &c); }
        *cpp_rust_ns = ns_per(n, t0);
    }
}
