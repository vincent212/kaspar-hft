/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Standalone CoordinatorActor executable.
 *
 * This is an Actor-framework-based implementation of the coordination service
 * using pure Actor messaging via ZmqReceiver (no DEALER-ROUTER sockets).
 *
 * Usage:
 *   coordinator_actor [--port PORT] [--address ADDRESS] [--monitor-port PORT] [--paused] [--debug]
 *
 * Options:
 *   --port PORT          Port to bind to (default: 5555)
 *   --address ADDR       Full ZeroMQ address (default: tcp://*:5555)
 *   --monitor-port PORT  MQ0 monitoring port (default: 11009)
 *   --paused, -p         Start in debug-stopped mode (requires DEBUG_CONTINUE to run)
 *   --debug, -d          Enable verbose logging (permissions, tokens, sends)
 *
 * Examples:
 *   coordinator_actor                              # Binds to tcp://*:5555
 *   coordinator_actor --port 11101                 # Binds to tcp://*:11101
 *   coordinator_actor --monitor-port 11009         # Monitoring on port 11009
 *   coordinator_actor --paused --debug             # Start paused with logging enabled
 *
 * The coordinator will run until:
 *   - SIGINT (Ctrl+C) or SIGTERM is received
 */

#include "actors/coordination/CoordinatorActor.hpp"
#include "actors/coordination/CoordinatorMessages.hpp"
#include "actors/coordination/CoordinationActorMessages.hpp"
#include "actors/remote/ZmqReceiver.hpp"
#include "actors/remote/Serialization.hpp"
#include "actors/console/ConsoleActor.hpp"
#include "actors/console/MQ0ServerActor.hpp"
#include "actors/registry/RegistryClient.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>

using namespace actors;
using namespace actors::coordination;
using namespace actors::registry;

// Register all coordination protocol messages
namespace coordination_registration {
    // Debug messages (IDs 1019-1021)
    using DebugFlush = actors::coordination::DebugFlushMessage;
    using DebugContinue = actors::coordination::DebugContinueMessage;
    using DebugStop = actors::coordination::DebugStopMessage;

    REGISTER_REMOTE_MESSAGE(DebugFlush,
        { return nlohmann::json::object(); },
        { return new actors::coordination::DebugFlushMessage(); }
    )

    REGISTER_REMOTE_MESSAGE(DebugContinue,
        { return nlohmann::json::object(); },
        { return new actors::coordination::DebugContinueMessage(); }
    )

    REGISTER_REMOTE_MESSAGE(DebugStop,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::DebugStopMessage(); if (j.contains("reason") && !j["reason"].is_null()) { m->reason = j["reason"].get<std::string>(); } return m; }
    )

    // Registration protocol messages (IDs 1010-1013)
    using RegisterGroup = actors::coordination::RegisterGroupMessage;
    using RegisterActor = actors::coordination::RegisterActorMessage;
    using RegisterAck = actors::coordination::RegisterAckMessage;
    using RegisterNack = actors::coordination::RegisterNackMessage;

    REGISTER_REMOTE_MESSAGE(RegisterGroup,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::RegisterGroupMessage(); m->reg.group_id = j["group_id"].get<std::string>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(RegisterActor,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::RegisterActorMessage(); m->reg.group_id = j["group_id"].get<std::string>(); m->reg.actor_id = j["actor_id"].get<std::string>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(RegisterAck,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::RegisterAckMessage(); m->ack.id = j["id"].get<std::string>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(RegisterNack,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::RegisterNackMessage(); m->nack.id = j["id"].get<std::string>(); m->nack.reason = j["reason"].get<std::string>(); return m; }
    )

    // Permission protocol messages (IDs 1014-1018)
    using PermissionToken = actors::coordination::PermissionTokenMessage;
    using PermissionRequest = actors::coordination::PermissionRequestMessage;
    using PermissionGrant = actors::coordination::PermissionGrantMessage;
    using PermissionWait = actors::coordination::PermissionWaitMessage;
    using PermissionDone = actors::coordination::PermissionDoneMessage;

    REGISTER_REMOTE_MESSAGE(PermissionToken,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::PermissionTokenMessage(); m->token.recipient_id = j["recipient_id"].get<std::string>(); m->token.sender_id = j["sender_id"].get<std::string>(); m->token.timestamp = j["timestamp"].get<uint64_t>(); m->token.message_type = j["message_type"].get<std::string>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(PermissionRequest,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::PermissionRequestMessage(); m->request.actor_id = j["actor_id"].get<std::string>(); m->request.group_id = j["group_id"].get<std::string>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(PermissionGrant,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::PermissionGrantMessage(); m->grant.actor_id = j["actor_id"].get<std::string>(); m->grant.sequence = j["sequence"].get<uint64_t>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(PermissionWait,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::PermissionWaitMessage(); m->wait.actor_id = j["actor_id"].get<std::string>(); m->wait.position = j["position"].get<uint32_t>(); return m; }
    )

    REGISTER_REMOTE_MESSAGE(PermissionDone,
        { return nlohmann::json::object(); },
        { auto* m = new actors::coordination::PermissionDoneMessage(); m->done.actor_id = j["actor_id"].get<std::string>(); m->done.sequence = j["sequence"].get<uint64_t>(); return m; }
    )
}

// Global pointers for signal handler
static CoordinatorActor* g_coordinator = nullptr;
static ZmqReceiver* g_receiver = nullptr;
static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signum)
{
    (void)signum;
    std::cout << "\n[coordinator_actor] Signal received, shutting down..." << std::endl;
    g_shutdown_requested = true;

    if (g_coordinator) {
        g_coordinator->send(new ShutdownSignal("signal received"), g_coordinator);
    }
    if (g_receiver) {
        g_receiver->send(new ShutdownSignal("signal received"), g_receiver);
    }
}

void print_usage(const char* program)
{
    std::cerr << "Usage: " << program << " [--port PORT] [--address ADDRESS] [--monitor-port PORT] [--registry ADDR] [--paused] [--debug]\n"
              << "\n"
              << "Options:\n"
              << "  --port PORT          Port to bind to (default: 5555)\n"
              << "  --address ADDR       Full ZeroMQ address (default: tcp://*:5555)\n"
              << "  --monitor-port PORT  MQ0 monitoring port (default: 11009)\n"
              << "  --registry ADDR      GlobalRegistry address (default: tcp://localhost:5556)\n"
              << "  --paused, -p         Start in debug-stopped mode (requires DEBUG_CONTINUE to run)\n"
              << "  --debug, -d          Enable verbose logging (permissions, tokens, sends)\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << "                              # Binds to tcp://*:5555\n"
              << "  " << program << " --port 11101                 # Binds to tcp://*:11101\n"
              << "  " << program << " --monitor-port 11009         # Monitoring on port 11009\n"
              << "  " << program << " --registry tcp://localhost:5556  # Registry address\n"
              << "  " << program << " --paused --debug             # Start paused with logging enabled\n";
}

int main(int argc, char* argv[])
{
    std::string address = "tcp://*:5555";
    std::string registry_address = "tcp://localhost:5556";
    int port = 5555;
    int monitor_port = 11009;
    bool port_specified = false;
    bool address_specified = false;
    bool start_paused = false;
    bool enable_logging = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
            port_specified = true;
        }
        else if (arg == "--address" && i + 1 < argc) {
            address = argv[++i];
            address_specified = true;
        }
        else if (arg == "--registry" && i + 1 < argc) {
            registry_address = argv[++i];
        }
        else if (arg == "--paused" || arg == "-p") {
            start_paused = true;
        }
        else if (arg == "--debug" || arg == "-d") {
            enable_logging = true;
        }
        else if (arg == "--monitor-port" && i + 1 < argc) {
            monitor_port = std::stoi(argv[++i]);
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Build address if only port was specified
    if (port_specified && !address_specified) {
        address = "tcp://*:" + std::to_string(port);
    }

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        std::cout << "[coordinator_actor] Starting Actor-based coordinator (pure Actor messaging)" << std::endl;
        std::cout << "[coordinator_actor] Binding to " << address << std::endl;
        if (enable_logging) {
            std::cout << "[coordinator_actor] Logging: ENABLED" << std::endl;
        }
        if (start_paused) {
            std::cout << "[coordinator_actor] Starting PAUSED - waiting for DEBUG_CONTINUE" << std::endl;
        }

        // Create Manager
        Manager manager;

        // Create CoordinatorActor
        CoordinatorActor* coordinator = new CoordinatorActor();

        // Build public endpoint for replies (convert * to localhost)
        std::string public_address = address;
        size_t star_pos = public_address.find("*");
        if (star_pos != std::string::npos) {
            public_address.replace(star_pos, 1, "localhost");
        }

        // Create ZmqSender for outgoing messages (replies)
        // ZmqReceiver will use this to send replies via RemoteReplyProxy
        auto sender = std::make_shared<ZmqSender>(public_address);

        // Create ZmqReceiver for remote messages (binds PULL socket)
        // Groups will connect their ZmqSender (PUSH socket) to this address
        // Use Manager lookup mode so it can find "coordinator" actor automatically
        auto receiver = new ZmqReceiver(address, sender, &manager);

        // NOTE: No need to call register_actor() when using Manager mode!
        // The receiver will automatically find all managed actors via manager_->get_local_actor()

        // Create monitoring actors
        auto console = new frame::cons::ConsoleActor(&manager);
        ActorRef console_ref(console);
        auto mq0_server = new frame::cons::MQ0ServerActor(console_ref, monitor_port);

        // Set global pointers for signal handler
        g_coordinator = coordinator;
        g_receiver = receiver;

        // Set paused state if requested
        if (start_paused) {
            // Note: CoordinatorActor starts unpaused by default.
            // To pause it, send a DEBUG_STOP message via ZMQ after startup.
            std::cout << "[coordinator_actor] NOTE: Send DEBUG_STOP message to pause coordination" << std::endl;
        }

        // Manage actors (manager takes ownership)
        manager.manage(receiver);     // ZmqReceiver (PULL socket for remote messages)
        manager.manage(coordinator);  // CoordinatorActor
        manager.manage(console);      // ConsoleActor
        manager.manage(mq0_server);   // MQ0ServerActor

        std::cout << "[coordinator_actor] Socket bound to " << address << std::endl;

        // Initialize actors
        manager.init();

        // Register with GlobalRegistry
        std::cout << "[coordinator_actor] Registering with GlobalRegistry at " << registry_address << std::endl;
        std::unique_ptr<RegistryClient> registry_client;
        try {
            registry_client = std::make_unique<RegistryClient>(registry_address);
            registry_client->connect();

            // Build public endpoint (replace * with localhost for registry advertisement)
            std::string public_address = address;
            size_t star_pos = public_address.find("*");
            if (star_pos != std::string::npos) {
                public_address.replace(star_pos, 1, "localhost");
            }

            // Register as "coordinator" manager with no actors (it's a service, not a manager)
            std::vector<std::string> actors = {"coordinator"};
            bool success = registry_client->register_manager("coordinator", public_address, actors);
            if (success) {
                std::cout << "[coordinator_actor] Registered as 'coordinator' at " << public_address << std::endl;

                // Start heartbeat thread
                registry_client->start_heartbeat_thread("coordinator");
            } else {
                std::cerr << "[coordinator_actor] WARNING: Failed to register with GlobalRegistry" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[coordinator_actor] WARNING: Registry registration failed: " << e.what() << std::endl;
            std::cerr << "[coordinator_actor] Continuing without registry registration..." << std::endl;
        }

        std::cout << "[coordinator_actor] Running. Press Ctrl+C to stop." << std::endl;
        std::cout << "[coordinator_actor] Monitoring interface:" << std::endl;
        std::cout << "  MQ0 Server: tcp://*:" << monitor_port << std::endl;
        std::cout << "  Commands: pid, time, list" << std::endl;
        std::cout << std::endl;

        // Wait for shutdown signal
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "[coordinator_actor] Stopping actors..." << std::endl;

        // Unregister from GlobalRegistry
        if (registry_client) {
            try {
                registry_client->stop_heartbeat_thread();
                registry_client->unregister_manager("coordinator");
                registry_client->disconnect();
                std::cout << "[coordinator_actor] Unregistered from GlobalRegistry" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[coordinator_actor] Error during unregistration: " << e.what() << std::endl;
            }
        }

        // Signal actors to terminate
        coordinator->send(new ShutdownSignal("shutdown requested"), coordinator);
        receiver->send(new ShutdownSignal("shutdown requested"), receiver);

        // End the manager (waits for actors to finish)
        manager.end();

        std::cout << "[coordinator_actor] Shutdown complete." << std::endl;

        g_coordinator = nullptr;
        g_receiver = nullptr;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "[coordinator_actor] Error: " << e.what() << std::endl;
        g_coordinator = nullptr;
        g_receiver = nullptr;
        return 1;
    }
}
