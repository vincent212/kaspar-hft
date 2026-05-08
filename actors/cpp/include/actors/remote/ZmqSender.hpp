#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/Message.hpp"
#include "actors/msg/Start.hpp"
#include "actors/remote/Serialization.hpp"
#include "actors/act/Group.hpp"  // For send_permission_token()
#include <iostream>
#include <cassert>
#include <stdexcept>

// Define ACTORS_ASSERT for use in this file (avoids conflict with other ASSERT macros)
#define ACTORS_ASSERT(cond, msg) do { if (!(cond)) { std::cerr << "ASSERTION FAILED: " << msg << std::endl; std::abort(); } } while(0)

namespace actors {

// Forward declaration
class RemoteActorRef;
class ZmqSender;

/**
 * Internal message for async remote sends
 * Message ID 10 (reserved for internal use)
 */
class RemoteSendRequest : public Message_N<10> {
public:
    std::string endpoint;
    std::string actor_name;
    std::string sender_name;      // Empty if no sender
    std::string sender_endpoint;  // Empty if no sender
    std::string message_type;
    std::string message_json;     // Pre-serialized message JSON

    RemoteSendRequest(std::string ep, std::string actor,
                      std::string s_name, std::string s_ep,
                      std::string msg_type, std::string msg_json)
        : endpoint(std::move(ep))
        , actor_name(std::move(actor))
        , sender_name(std::move(s_name))
        , sender_endpoint(std::move(s_ep))
        , message_type(std::move(msg_type))
        , message_json(std::move(msg_json)) {}
};

/**
 * ZmqSender - Actor that manages PUSH sockets for sending messages to remote actors
 *
 * Features:
 * - Async sending (never blocks caller)
 * - Connection caching (one socket per endpoint)
 * - JSON wire protocol compatible with Rust/Python
 *
 * Usage:
 *   auto sender = std::make_shared<ZmqSender>("tcp://localhost:5002");
 *   manager.manage(sender.get());  // Must be managed to run
 *
 *   // After init(), sends are async:
 *   sender->send_to("tcp://localhost:5001", "pong", new Ping{1}, this);
 */
class ZmqSender : public Actor, public std::enable_shared_from_this<ZmqSender> {
public:
    /**
     * Create a ZmqSender
     *
     * @param local_endpoint Our endpoint for reply routing (e.g., "tcp://localhost:5002")
     */
    explicit ZmqSender(const std::string& local_endpoint)
        : context_(1)
        , local_endpoint_(local_endpoint) {
        strncpy(name, "ZmqSender", sizeof(name));

        MESSAGE_HANDLER(msg::Start, on_start);
        MESSAGE_HANDLER(RemoteSendRequest, on_send_request);
    }

    ~ZmqSender() {
        close();
    }

    // Non-copyable
    ZmqSender(const ZmqSender&) = delete;
    ZmqSender& operator=(const ZmqSender&) = delete;

    /**
     * Send a message to a remote actor (async - returns immediately)
     *
     * @param endpoint Remote endpoint (e.g., "tcp://localhost:5001")
     * @param actor_name Name of target actor
     * @param msg Message to send (ownership transferred)
     * @param sender Sending actor (for reply routing, can be nullptr)
     */
    void send_to(const std::string& endpoint,
                 const std::string& actor_name,
                 const Message* msg,
                 Actor* sender = nullptr) {
        // Get type name and serialize message NOW (on caller's thread)
        std::string type_name = serialization::get_type_name(msg->get_message_id());
        ACTORS_ASSERT(!type_name.empty(), ("Message type not registered for remote send: msg_id=" + std::to_string(msg->get_message_id()) + " to " + actor_name + "@" + endpoint).c_str());

        nlohmann::json msg_json = serialization::serialize(msg);

        // FIX: Do NOT send TOKEN here - receiving group will send it when processing
        // This prevents deadlock from token queue mismatch where coordinator expects
        // requests in TOKEN order but receiving group processes internal messages first
        std::cerr << "ZmqSender: Sending remote message to " << actor_name
                  << " (TOKEN will be sent by receiver when processing)" << std::endl;

        // Delete original message - we've copied the data
        delete msg;

        // Build request and queue it for async send
        std::string sender_name = sender ? sender->get_name() : "";
        std::string sender_ep = sender ? local_endpoint_ : "";

        auto* req = new RemoteSendRequest(
            endpoint, actor_name,
            sender_name, sender_ep,
            type_name, msg_json.dump()
        );

        // Queue to our own actor thread (async ZMQ send happens later)
        this->Actor::send(req, nullptr);
    }

    /**
     * Create a remote actor reference
     */
    ActorRef remote_ref(const std::string& name, const std::string& endpoint);

    /**
     * Close all sockets
     */
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.clear();
    }

    const std::string& local_endpoint() const { return local_endpoint_; }

private:
    void on_start(const msg::Start*) noexcept {
        // Ready to send
    }

    void on_send_request(const RemoteSendRequest* req) noexcept {
        // NOTE: TOKEN already sent synchronously in send_to() before queuing this request
        // We just send the ZMQ message here

        // Build envelope
        nlohmann::json envelope;
        if (!req->sender_name.empty()) {
            envelope["sender_actor"] = req->sender_name;
            envelope["sender_endpoint"] = req->sender_endpoint;
        } else {
            envelope["sender_actor"] = nullptr;
            envelope["sender_endpoint"] = nullptr;
        }
        envelope["receiver"] = req->actor_name;
        envelope["message_type"] = req->message_type;
        envelope["message"] = nlohmann::json::parse(req->message_json);

        // Serialize and send
        std::string data = envelope.dump();
        std::cerr << "ZmqSender: Sending to " << req->endpoint << " actor=" << req->actor_name
                  << " type=" << req->message_type << std::endl;
        send_raw(req->endpoint, data);
    }

    void send_raw(const std::string& endpoint, const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Get or create socket
        auto it = sockets_.find(endpoint);
        if (it == sockets_.end()) {
            zmq::socket_t socket(context_, zmq::socket_type::push);

            // Convert endpoint for connection
            std::string connect_endpoint = endpoint;
            // Replace *: with localhost: for connection
            size_t pos = connect_endpoint.find("*:");
            if (pos != std::string::npos) {
                connect_endpoint.replace(pos, 2, "localhost:");
            }
            // Replace 0.0.0.0: with localhost: for connection
            pos = connect_endpoint.find("0.0.0.0:");
            if (pos != std::string::npos) {
                connect_endpoint.replace(pos, 8, "localhost:");
            }

            socket.connect(connect_endpoint);
            auto result = sockets_.emplace(endpoint, std::move(socket));
            it = result.first;
        }

        // Send message
        zmq::message_t message(data.data(), data.size());
        it->second.send(message, zmq::send_flags::none);
    }

private:
    zmq::context_t context_;
    std::unordered_map<std::string, zmq::socket_t> sockets_;
    std::mutex mutex_;
    std::string local_endpoint_;
};

// Implementation of RemoteActorRef::send (declared in ActorRef.hpp)
inline void RemoteActorRef::send(const Message* m, Actor* sender) {
    sender_->send_to(endpoint_, name_, m, sender);
}

// Implementation of ZmqSender::remote_ref
inline ActorRef ZmqSender::remote_ref(const std::string& name, const std::string& endpoint) {
    return ActorRef(name, endpoint, shared_from_this());
}

} // namespace actors
