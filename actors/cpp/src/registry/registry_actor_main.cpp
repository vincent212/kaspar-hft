/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Standalone RegistryActor executable.
 *
 * This is an Actor-framework-based implementation of the registry service.
 * Uses RegistryActor, ZmqRouterReceiver, ZmqRouterSender, and MQ0 monitoring.
 *
 * Usage:
 *   registry_actor [--port PORT] [--address ADDRESS] [--monitor-port PORT] [--heartbeat-timeout SECONDS] [--debug] [--quiet]
 *
 * Options:
 *   --port PORT               Port to bind to (default: 5556)
 *   --address ADDR            Full ZeroMQ address (default: tcp://*:5556)
 *   --monitor-port PORT       MQ0 monitoring port (default: 11010)
 *   --heartbeat-timeout SECS  Manager timeout in seconds (default: 30)
 *   --debug, -d               Enable verbose logging
 *   --quiet, -q               Disable logging (overrides --debug)
 *
 * Examples:
 *   registry_actor                           # Binds to tcp://*:5556
 *   registry_actor --port 11102              # Binds to tcp://*:11102
 *   registry_actor --monitor-port 11010      # Monitoring on port 11010
 */

#include "actors/registry/RegistryActor.hpp"
#include "actors/registry/RegistryQueryActor.hpp"
#include "actors/coordination/ZmqRouterReceiver.hpp"
#include "actors/coordination/ZmqRouterSender.hpp"
#include "actors/coordination/CoordinatorMessages.hpp"
#include "actors/console/ConsoleActor.hpp"
#include "actors/console/MQ0ServerActor.hpp"
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
using namespace actors::registry;
using namespace actors::coordination;

// Global pointers for signal handler
static RegistryActor* g_registry = nullptr;
static ZmqRouterReceiver* g_receiver = nullptr;
static ZmqRouterSender* g_sender = nullptr;
static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signum)
{
    (void)signum;
    std::cout << "\n[registry_actor] Signal received, shutting down..." << std::endl;
    g_shutdown_requested = true;

    if (g_registry) {
        g_registry->send(new ShutdownSignal("signal received"), g_registry);
    }
    if (g_receiver) {
        g_receiver->terminate();
    }
    if (g_sender) {
        g_sender->terminate();
    }
}

void print_usage(const char* program)
{
    std::cerr << "Usage: " << program << " [--port PORT] [--address ADDRESS] [--monitor-port PORT] [--heartbeat-timeout SECONDS] [--debug] [--quiet]\n"
              << "\n"
              << "Options:\n"
              << "  --port PORT               Port to bind to (default: 5556)\n"
              << "  --address ADDR            Full ZeroMQ address (default: tcp://*:5556)\n"
              << "  --monitor-port PORT       MQ0 monitoring port (default: 11010)\n"
              << "  --heartbeat-timeout SECS  Manager timeout in seconds (default: 30)\n"
              << "  --debug, -d               Enable verbose logging\n"
              << "  --quiet, -q               Disable logging (overrides --debug)\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << "                           # Binds to tcp://*:5556\n"
              << "  " << program << " --port 11102              # Binds to tcp://*:11102\n"
              << "  " << program << " --monitor-port 11010      # Monitoring on port 11010\n";
}

int main(int argc, char* argv[])
{
    std::string address = "tcp://*:5556";
    int port = 5556;
    int monitor_port = 11010;
    int heartbeat_timeout = 30;
    bool port_specified = false;
    bool address_specified = false;
    bool debug_logging = true;
    bool quiet_mode = false;

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
        else if (arg == "--monitor-port" && i + 1 < argc) {
            monitor_port = std::stoi(argv[++i]);
        }
        else if (arg == "--heartbeat-timeout" && i + 1 < argc) {
            heartbeat_timeout = std::stoi(argv[++i]);
        }
        else if (arg == "--debug" || arg == "-d") {
            debug_logging = true;
        }
        else if (arg == "--quiet" || arg == "-q") {
            quiet_mode = true;
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
        bool logging_enabled = debug_logging && !quiet_mode;

        std::cout << "[registry_actor] Starting Actor-based registry" << std::endl;
        std::cout << "[registry_actor] Binding to " << address << std::endl;
        std::cout << "[registry_actor] Heartbeat timeout: " << heartbeat_timeout << " seconds" << std::endl;
        std::cout << "[registry_actor] Logging: " << (logging_enabled ? "ENABLED" : "DISABLED") << std::endl;

        // Create ZMQ context and ROUTER socket
        auto zmq_context = std::make_shared<zmq::context_t>(1);
        auto router_socket = std::make_shared<zmq::socket_t>(*zmq_context, zmq::socket_type::router);

        // Set socket options
        int linger = 0;
        router_socket->set(zmq::sockopt::linger, linger);

        // Bind socket
        router_socket->bind(address);
        std::cout << "[registry_actor] Socket bound to " << address << std::endl;

        // Create Manager
        Manager manager;

        // Create registry actors
        RegistryActor* registry = new RegistryActor(nullptr);
        registry->set_heartbeat_timeout(std::chrono::seconds(heartbeat_timeout));
        registry->set_logging_enabled(logging_enabled);

        ZmqRouterReceiver* receiver = new ZmqRouterReceiver(registry, router_socket);
        ZmqRouterSender* sender = new ZmqRouterSender(router_socket);

        // Fix up the registry's sender reference
        registry->set_router(sender);

        // Set global pointers for signal handler
        g_registry = registry;
        g_receiver = receiver;
        g_sender = sender;

        // Create monitoring actors (RegistryQueryActor needs a GlobalRegistry pointer)
        // For now, we'll skip RegistryQueryActor since it's tightly coupled to GlobalRegistry
        // TODO: Create a RegistryQueryActor that works with RegistryActor

        auto console = new frame::cons::ConsoleActor(&manager);
        ActorRef console_ref(console);
        auto mq0_server = new frame::cons::MQ0ServerActor(console_ref, monitor_port);

        // Manage actors
        manager.manage(receiver);
        manager.manage(sender);
        manager.manage(registry);
        manager.manage(console);
        manager.manage(mq0_server);

        // Initialize actors
        manager.init();

        std::cout << "[registry_actor] Running. Press Ctrl+C to stop." << std::endl;
        std::cout << "[registry_actor] Monitoring interface:" << std::endl;
        std::cout << "  MQ0 Server: tcp://*:" << monitor_port << std::endl;
        std::cout << "  Commands: list" << std::endl;
        std::cout << std::endl;

        // Wait for shutdown signal
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "[registry_actor] Stopping actors..." << std::endl;

        // Signal actors to terminate
        registry->terminate();
        receiver->terminate();
        sender->terminate();
        console->terminate();
        mq0_server->terminate();

        // End the manager
        manager.end();

        // Close socket
        router_socket->close();

        std::cout << "[registry_actor] Shutdown complete." << std::endl;

        g_registry = nullptr;
        g_receiver = nullptr;
        g_sender = nullptr;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "[registry_actor] Error: " << e.what() << std::endl;
        g_registry = nullptr;
        g_receiver = nullptr;
        g_sender = nullptr;
        return 1;
    }
}
