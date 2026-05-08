#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/coordination/messages.hpp"
#include <string>
#include <vector>

namespace actors::coordination {

/**
 * Internal actor messages - use IDs under 2048 for handler cache
 *
 * Message from ZmqRouterActor to CoordinatorActor
 * Contains a received ZMQ message with identity and payload
 */
struct IncomingZmqMessage : public Message_N<490> {
    std::string identity;  // ZMQ ROUTER identity
    std::vector<uint8_t> data;  // Message payload

    IncomingZmqMessage(std::string id, std::vector<uint8_t> d)
        : identity(std::move(id)), data(std::move(d)) {}
};

/**
 * Message from CoordinatorActor to ZmqRouterActor
 * Requests sending a message to a specific identity
 */
struct OutgoingZmqMessage : public Message_N<491> {
    std::string identity;  // ZMQ ROUTER identity
    MsgType msg_type;      // Message type
    std::vector<uint8_t> data;  // Serialized message

    OutgoingZmqMessage(std::string id, MsgType type, std::vector<uint8_t> d)
        : identity(std::move(id)), msg_type(type), data(std::move(d)) {}
};

/**
 * Timer message for periodic tasks
 */
struct TimerTick : public Message_N<492> {
    TimerTick() = default;
};

/**
 * Shutdown signal from main
 */
struct ShutdownSignal : public Message_N<493> {
    std::string reason;
    bool force;

    ShutdownSignal(std::string r = "", bool f = false)
        : reason(std::move(r)), force(f) {}
};

} // namespace actors::coordination
