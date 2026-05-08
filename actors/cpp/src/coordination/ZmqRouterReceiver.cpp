/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/coordination/ZmqRouterReceiver.hpp"
#include "actors/coordination/CoordinatorMessages.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Continue.hpp"
#include <iostream>
#include <chrono>

namespace actors::coordination {

ZmqRouterReceiver::ZmqRouterReceiver(Actor* coordinator_ref, std::shared_ptr<zmq::socket_t> router)
    : coordinator_ref_(coordinator_ref)
    , router_(router)
{
    strcpy(name, "ZmqRouterReceiver");
}

void ZmqRouterReceiver::init() {
    std::cout << "[ZmqRouterReceiver] Initializing..." << std::endl;

    // Register message handlers
    MESSAGE_HANDLER(msg::Start, on_start);
    MESSAGE_HANDLER(msg::Continue, on_continue);

    // Start the receive loop
    std::cout << "[ZmqRouterReceiver] Constructor" << std::endl;
    running_ = true;
    send(new msg::Continue(), this);
}

void ZmqRouterReceiver::end() {
    std::cout << "[ZmqRouterReceiver] Shutting down..." << std::endl;
    running_ = false;
}

void ZmqRouterReceiver::terminate() noexcept {
    running_ = false;
    Actor::terminate();
}

void ZmqRouterReceiver::process_message(const Message* m) {
    // Default message processing
}

void ZmqRouterReceiver::on_start(const msg::Start*) {
    std::cout << "[ZmqRouterReceiver] Start handler" << std::endl;
    running_ = true;
    send(new msg::Continue(), this);
}

void ZmqRouterReceiver::on_continue(const msg::Continue*) {
    if (!running_) return;

    // Receive incoming ZMQ message (blocking)
    recv_zmq();

    // Continue receiving
    if (running_) {
        send(new msg::Continue(), this);
    }
}

void ZmqRouterReceiver::recv_zmq() {
    if (!router_ || !running_) return;

    // BLOCKING receive - waits until message arrives
    auto [identity, data] = recv_multiframe();

    if (!data.empty()) {
        coordinator_ref_->send(new IncomingZmqMessage(identity, data), this);
    }
    // If data is empty (timeout), the 100ms timeout in recv_multiframe() already prevented spinning
}

std::pair<std::string, std::vector<uint8_t>> ZmqRouterReceiver::recv_multiframe() {
    if (!router_) {
        return {{}, {}};
    }

    try {
        // Set infinite timeout for true blocking
        int timeout_ms = -1;  // -1 = infinite (block until message arrives)
        router_->set(zmq::sockopt::rcvtimeo, timeout_ms);

        // Receive Frame 1: Identity (BLOCKING)
        zmq::message_t identity_msg;
        auto res = router_->recv(identity_msg, zmq::recv_flags::none);
        if (!res || !running_) {
            return {{}, {}};
        }

        // Check for more frames
        int more = router_->get(zmq::sockopt::rcvmore);
        if (!more) {
            std::cerr << "[ZmqRouterReceiver] FATAL: Identity frame has no more frames!" << std::endl;
            std::cerr << "[ZmqRouterReceiver] This indicates a critical framing error - aborting to prevent silent message loss." << std::endl;
            std::abort();  // Fatal error - don't silently drop messages
        }

        // Receive Frame 2: Empty delimiter (REQUIRED by our protocol)
        zmq::message_t empty_msg;
        res = router_->recv(empty_msg, zmq::recv_flags::none);
        if (!res || !running_) {
            return {{}, {}};
        }

        // ENFORCE protocol: Frame 2 MUST be empty delimiter
        if (empty_msg.size() != 0) {
            std::cerr << "[ZmqRouterReceiver] FATAL: Protocol violation - Frame 2 must be empty delimiter, got "
                      << empty_msg.size() << " bytes!" << std::endl;
            std::cerr << "[ZmqRouterReceiver] This indicates incorrect DEALER framing - all clients must send [Empty][Data]" << std::endl;
            std::abort();  // Fatal protocol violation
        }

        // Check for more frames
        more = router_->get(zmq::sockopt::rcvmore);
        if (!more) {
            std::cerr << "[ZmqRouterReceiver] FATAL: Empty delimiter frame has no data frame!" << std::endl;
            std::cerr << "[ZmqRouterReceiver] Protocol requires [Identity][Empty][Data] - aborting." << std::endl;
            std::abort();  // Fatal error - missing data frame
        }

        // Receive Frame 3: Data
        zmq::message_t data_msg;
        res = router_->recv(data_msg, zmq::recv_flags::none);
        if (!res || !running_) {
            return {{}, {}};
        }

        // Verify no extra frames
        more = router_->get(zmq::sockopt::rcvmore);
        if (more) {
            std::cerr << "[ZmqRouterReceiver] FATAL: Unexpected additional frames after data!" << std::endl;
            std::cerr << "[ZmqRouterReceiver] This indicates a protocol violation that will corrupt the socket" << std::endl;
            std::cerr << "[ZmqRouterReceiver] Identity hex: ";
            for (size_t i = 0; i < identity_msg.size(); i++) {
                std::cerr << std::hex << static_cast<int>(static_cast<uint8_t*>(identity_msg.data())[i]) << " ";
            }
            std::cerr << std::dec << std::endl;
            std::cerr << "[ZmqRouterReceiver] Identity string: " << std::string(static_cast<char*>(identity_msg.data()), identity_msg.size()) << std::endl;
            std::cerr << "[ZmqRouterReceiver] Frame 3 (data) size: " << data_msg.size() << " bytes" << std::endl;
            if (data_msg.size() > 0 && data_msg.size() < 20) {
                std::cerr << "[ZmqRouterReceiver] Frame 3 content: ";
                for (size_t i = 0; i < data_msg.size(); i++) {
                    std::cerr << static_cast<int>(static_cast<uint8_t*>(data_msg.data())[i]) << " ";
                }
                std::cerr << std::endl;
            }
            std::abort();  // Fatal - cannot recover from protocol violation
        }

        // Extract identity and data
        std::string identity(static_cast<char*>(identity_msg.data()), identity_msg.size());
        std::vector<uint8_t> data(
            static_cast<uint8_t*>(data_msg.data()),
            static_cast<uint8_t*>(data_msg.data()) + data_msg.size()
        );

        return {identity, data};

    } catch (const zmq::error_t& e) {
        if (running_) {
            std::cerr << "[ZmqRouterReceiver] ZMQ error: " << e.what() << std::endl;
            abort();  // Fatal error - likely indicates socket corruption or severe issue
        }
        return {{}, {}};
    }
}

} // namespace actors::coordination
