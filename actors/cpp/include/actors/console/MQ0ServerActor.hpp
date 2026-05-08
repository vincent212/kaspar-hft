#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/Message.hpp"
#include "actors/console/ConsoleMessages.hpp"
#include <zmq.hpp>
#include <memory>
#include <string>

namespace frame {
namespace cons {

// Internal message for polling cycle
struct PollTimeout : public actors::Message_N<204> {};

/**
 * MQ0ServerActor - ZMQ REP server for monitoring
 *
 * Provides ZMQ REP socket interface for querying actors.
 * Matches C++ mq0::act::MQ0_server and Python MQ0ServerActor functionality.
 *
 * Built-in commands (handled directly):
 * - pid  : Return process ID
 * - time : Return current time
 * - quit : Shutdown the system
 *
 * All other commands forwarded to Console.
 *
 * Example:
 *   ActorRef console_ref = manager->get_actor_by_name("Console");
 *   auto mq0 = new MQ0ServerActor(console_ref, 9002);
 *   manager->manage(mq0);
 *
 *   // From external client:
 *   // zmq_client.send("get actors") -> Returns JSON
 */
class MQ0ServerActor : public actors::Actor {
public:
    /**
     * Create MQ0 server
     * @param console_ref Reference to ConsoleActor for query forwarding
     * @param port ZMQ REP port (default 9002)
     */
    MQ0ServerActor(actors::ActorRef console_ref, int port = 9002);
    virtual ~MQ0ServerActor() = default;

    const char* get_name() const override { return "MQ0Server"; }

    void init() override;
    void end() override;

private:
    // Message handlers
    void on_poll_timeout(const PollTimeout* m) noexcept;

    // Internal methods
    void handle_command(const std::string& command);
    void schedule_continue();

    actors::ActorRef console_ref_;
    int port_;

    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    bool running_ = false;
};

} // namespace cons
} // namespace frame
