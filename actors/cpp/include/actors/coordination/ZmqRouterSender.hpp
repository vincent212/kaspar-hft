#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "CoordinatorMessages.hpp"
#include <zmq.hpp>
#include <memory>
#include <string>
#include <vector>

namespace actors::coordination {

/**
 * ZmqRouterSender - ZMQ ROUTER socket sender
 *
 * Responsibilities:
 * - Shares the ZMQ ROUTER socket (with ZmqRouterReceiver)
 * - Sends outgoing messages from CoordinatorActor
 * - Handles OutgoingZmqMessage messages
 *
 * This actor only sends, no receiving or polling.
 */
class ZmqRouterSender : public Actor {
public:
    /**
     * Constructor
     * @param router Shared pointer to ZMQ ROUTER socket
     */
    ZmqRouterSender(std::shared_ptr<zmq::socket_t> router);

    ~ZmqRouterSender() override = default;

protected:
    void init() override;
    void end() override;
    void process_message(const Message* m) override;

private:
    /**
     * Handle outgoing message
     */
    void on_outgoing_message(const OutgoingZmqMessage* msg);

    /**
     * Send a multi-frame message to ROUTER socket
     * Format: [identity][empty][data]
     */
    void send_multiframe(const std::string& identity, const std::vector<uint8_t>& data);

    std::shared_ptr<zmq::socket_t> router_;
};

} // namespace actors::coordination
