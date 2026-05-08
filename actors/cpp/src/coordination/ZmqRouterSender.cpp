/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/coordination/ZmqRouterSender.hpp"
#include "actors/coordination/CoordinatorMessages.hpp"
#include <iostream>

namespace actors::coordination {

ZmqRouterSender::ZmqRouterSender(std::shared_ptr<zmq::socket_t> router)
    : router_(router)
{
    strcpy(name, "ZmqRouterSender");
}

void ZmqRouterSender::init() {
    std::cout << "[ZmqRouterSender] Initializing..." << std::endl;

    // Register message handler
    MESSAGE_HANDLER(OutgoingZmqMessage, on_outgoing_message);

    std::cout << "[ZmqRouterSender] Ready to send" << std::endl;
}

void ZmqRouterSender::end() {
    std::cout << "[ZmqRouterSender] Shutting down..." << std::endl;
}

void ZmqRouterSender::process_message(const Message* m) {
    // Default message processing
    // Registered handlers will be called first via MESSAGE_HANDLER macro
}

void ZmqRouterSender::on_outgoing_message(const OutgoingZmqMessage* msg) {
    // Send message to ZMQ ROUTER socket
    send_multiframe(msg->identity, msg->data);
}

void ZmqRouterSender::send_multiframe(const std::string& identity, const std::vector<uint8_t>& data) {
    if (!router_) return;

    // ROUTER socket requires [identity][empty][data]
    zmq::message_t id_msg(identity.data(), identity.size());
    zmq::message_t empty_msg(0);
    zmq::message_t data_msg(data.data(), data.size());

    try {
        router_->send(id_msg, zmq::send_flags::sndmore);
        router_->send(empty_msg, zmq::send_flags::sndmore);
        router_->send(data_msg, zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqRouterSender] ERROR: Failed to send message: " << e.what() << std::endl;
    }
}

} // namespace actors::coordination
