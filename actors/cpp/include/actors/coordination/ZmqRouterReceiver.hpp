#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "CoordinatorMessages.hpp"
#include <zmq.hpp>
#include <memory>
#include <string>

namespace actors {
    namespace msg {
        struct Start;
        struct Continue;
    }
}

namespace actors::coordination {

/**
 * ZmqRouterReceiver - ZMQ ROUTER socket receiver with Continue pattern
 *
 * Responsibilities:
 * - Owns the ZMQ ROUTER socket (shared with ZmqRouterSender)
 * - Polls for incoming messages using zmq::poll()
 * - Receives complete multi-frame messages atomically
 * - Forwards incoming messages to CoordinatorActor
 */
class ZmqRouterReceiver : public Actor {
public:
    /**
     * Constructor
     * @param coordinator_ref Reference to the CoordinatorActor
     * @param router Shared pointer to ZMQ ROUTER socket
     */
    ZmqRouterReceiver(Actor* coordinator_ref, std::shared_ptr<zmq::socket_t> router);

    ~ZmqRouterReceiver() override = default;

    void terminate() noexcept override;

protected:
    void init() override;
    void end() override;
    void process_message(const Message* m) override;

private:
    void on_start(const msg::Start* msg);
    void on_continue(const msg::Continue* msg);

    /**
     * Receive incoming message (blocking)
     */
    void recv_zmq();

    /**
     * Receive a complete multi-frame message from ROUTER socket
     * Format: [identity][empty][data]
     * @return pair of (identity, data) or empty if receive failed
     */
    std::pair<std::string, std::vector<uint8_t>> recv_multiframe();

    Actor* coordinator_ref_;
    std::shared_ptr<zmq::socket_t> router_;
    bool running_ = true;
};

} // namespace actors::coordination
