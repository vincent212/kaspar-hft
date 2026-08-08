// C++ side of the hybrid interop benchmark: a real C++ actor that handles the
// interop msg::Ping, registered by name so the generated bridge can reach it.
#include <cstring>

#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "InteropMessages.hpp"   // msg::Ping (generated)
#include "CppActorBridge.hpp"    // cpp_actor_init

using namespace actors;

struct PongC : Actor {
    long count = 0;
    PongC() {
        std::strncpy(name, "cpp_pong", sizeof(name));
        MESSAGE_HANDLER(msg::Ping, on_ping);
    }
    // Trivial work, no reply — measures the cross-FFI dispatch into a C++ handler.
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
