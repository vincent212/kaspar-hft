/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Standalone Group Manager executable.
 *
 * Usage:
 *   group_manager [--port PORT] [--address ADDRESS] [--paused] [--debug] [--trace-file PATH]
 *
 * Options:
 *   --port PORT        Port to bind to (default: 5555)
 *   --address ADDR     Full ZeroMQ address (default: tcp://*:5555)
 *   --paused, -p       Start in debug-stopped mode (requires DEBUG_CONTINUE to run)
 *   --debug, -d        Enable verbose logging (permissions, tokens, sends)
 *   --log, -l          Alias for --debug
 *   --trace-file PATH  Enable tracing to file (JSON Lines format)
 *
 * Examples:
 *   group_manager                    # Binds to tcp://*:5555
 *   group_manager --port 5556        # Binds to tcp://*:5556
 *   group_manager --address tcp://*:5557
 *   group_manager --paused --debug   # Start paused with logging enabled
 *   group_manager --trace-file /tmp/trace.jsonl  # Enable tracing
 *
 * The Group Manager will run until:
 *   - A SHUTDOWN_REQUEST is received from a Group
 *   - SIGINT (Ctrl+C) or SIGTERM is received
 */

#include "actors/coordination/GroupManager.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

using namespace actors::coordination;

// Global pointer for signal handler
static GroupManager* g_manager = nullptr;
static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signum)
{
    (void)signum;
    g_shutdown_requested = true;
    if (g_manager) {
        g_manager->request_shutdown("signal received");
    }
}

void print_usage(const char* program)
{
    std::cerr << "Usage: " << program << " [--port PORT] [--address ADDRESS] [--paused] [--debug] [--trace-file PATH]\n"
              << "\n"
              << "Options:\n"
              << "  --port PORT        Port to bind to (default: 5555)\n"
              << "  --address ADDR     Full ZeroMQ address (default: tcp://*:5555)\n"
              << "  --paused, -p       Start in debug-stopped mode (requires DEBUG_CONTINUE to run)\n"
              << "  --debug, -d        Enable verbose logging (permissions, tokens, sends)\n"
              << "  --log, -l          Alias for --debug\n"
              << "  --trace-file PATH  Enable tracing to file (JSON Lines format)\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << "                    # Binds to tcp://*:5555\n"
              << "  " << program << " --port 5556        # Binds to tcp://*:5556\n"
              << "  " << program << " --address tcp://*:5557\n"
              << "  " << program << " --paused --debug   # Start paused with logging enabled\n"
              << "  " << program << " --trace-file /tmp/trace.jsonl  # Enable tracing\n";
}

int main(int argc, char* argv[])
{
    std::string address = "tcp://*:5555";
    int port = 5555;
    bool port_specified = false;
    bool address_specified = false;
    bool start_paused = false;
    bool enable_logging = false;
    std::string trace_file;

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
        else if (arg == "--paused" || arg == "-p") {
            start_paused = true;
        }
        else if (arg == "--debug" || arg == "-d" || arg == "--log" || arg == "-l") {
            enable_logging = true;
        }
        else if (arg == "--trace-file" && i + 1 < argc) {
            trace_file = argv[++i];
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
        GroupManager manager;
        g_manager = &manager;

        // Configure debug options
        if (enable_logging) {
            manager.set_logging_enabled(true);
            std::cout << "GroupManager logging ENABLED" << std::endl;
        }

        if (!trace_file.empty()) {
            if (manager.enable_tracing(trace_file)) {
                std::cout << "GroupManager tracing ENABLED to " << trace_file << std::endl;
            } else {
                std::cerr << "Warning: Failed to open trace file: " << trace_file << std::endl;
            }
        }

        if (start_paused) {
            manager.set_debug_stopped(true, "Started in paused mode");
            std::cout << "GroupManager starting PAUSED - waiting for DEBUG_CONTINUE" << std::endl;
        }

        std::cout << "GroupManager starting on " << address << std::endl;
        manager.bind(address);

        std::cout << "GroupManager running. Press Ctrl+C to stop." << std::endl;
        manager.run();

        std::cout << "GroupManager shutdown complete." << std::endl;
        g_manager = nullptr;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        g_manager = nullptr;
        return 1;
    }
}
