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
#include <vector>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/remote/Serialization.hpp"
#include "actors/remote/Reject.hpp"
#include "actors/remote/ZmqSender.hpp"
#include <iostream>
#include "actors/act/Manager.hpp"
#include "actors/act/Group.hpp"

// ASSERT is defined in ZmqSender.hpp if not already defined

namespace actors {

/**
 * ZmqReceiver - Actor that receives and routes remote messages
 *
 * Binds to a ZMQ PULL socket and routes incoming messages to
 * registered local actors. Sends Reject messages for errors.
 *
 * Usage:
 *   auto sender = std::make_shared<ZmqSender>("tcp://localhost:5001");
 *   auto receiver = new ZmqReceiver("tcp://0.0.0.0:5001", sender);
 *
 *   receiver->register_actor("pong", pong_actor);
 *
 *   mgr.manage("zmq_receiver", receiver);
 *   mgr.init();
 */
/**
 * RemoteReplyProxy - Proxy actor that forwards replies to remote actors
 *
 * When a remote message arrives, we create a proxy that the local actor
 * can use as reply_to. When the local actor calls reply(), the proxy
 * intercepts it and forwards via ZMQ.
 */
class RemoteReplyProxy : public Actor {
    std::shared_ptr<ZmqSender> sender_;
    std::string remote_actor_;
    std::string remote_endpoint_;

public:
    RemoteReplyProxy(std::shared_ptr<ZmqSender> sender,
                     std::string actor, std::string endpoint)
        : sender_(std::move(sender))
        , remote_actor_(std::move(actor))
        , remote_endpoint_(std::move(endpoint)) {
        strncpy(name, "RemoteReplyProxy", sizeof(name));
    }

    // Override send() to forward directly via ZMQ instead of queuing
    // This proxy is never started with a thread, so we handle it synchronously
    void send(const Message* m, Actor* /*sender*/ = nullptr) noexcept override {
        // Forward this message to the remote actor
        std::cerr << "RemoteReplyProxy: Forwarding msg_id=" << m->get_message_id()
                  << " to " << remote_actor_ << " at " << remote_endpoint_ << std::endl;
        sender_->send_to(remote_endpoint_, remote_actor_, m, nullptr);
        // Note: ZmqSender::send_to deletes the message
    }

    /**
     * Get the original remote actor name (for subscription tracking).
     * Returns "TradeCollector" instead of "RemoteReplyProxy".
     */
    const std::string& get_remote_actor_name() const {
        return remote_actor_;
    }
};

class ZmqReceiver : public Actor {
public:
    /**
     * Create a ZmqReceiver with manual actor registration
     *
     * @param bind_endpoint Endpoint to bind to (e.g., "tcp://0.0.0.0:5001")
     * @param sender ZmqSender for sending Reject messages
     */
    ZmqReceiver(const std::string& bind_endpoint, std::shared_ptr<ZmqSender> sender)
        : context_(1)
        , socket_(context_, zmq::socket_type::pull)
        , sender_(std::move(sender))
        , bind_endpoint_(bind_endpoint)
        , manager_(nullptr)
        , running_(false) {
        strncpy(name, "ZmqReceiver", sizeof(name));

        // Register message handlers
        MESSAGE_HANDLER(msg::Start, on_start);
        MESSAGE_HANDLER(msg::Continue, on_continue);

        // Bind socket
        std::string bind_addr = bind_endpoint_;
        // Convert tcp://*:PORT to tcp://0.0.0.0:PORT
        size_t pos = bind_addr.find("*:");
        if (pos != std::string::npos) {
            bind_addr.replace(pos, 1, "0.0.0.0");
        }
        socket_.bind(bind_addr);

        // Set receive timeout for non-blocking polls
        socket_.set(zmq::sockopt::rcvtimeo, 10);  // 10ms timeout
    }

    /**
     * Create a ZmqReceiver with automatic actor lookup via Manager
     *
     * When a Manager is provided, incoming messages are routed to actors
     * using Manager::get_local_actor() instead of the manual registry.
     * This automatically supports all actors managed by the Manager.
     *
     * @param bind_endpoint Endpoint to bind to (e.g., "tcp://0.0.0.0:5001")
     * @param sender ZmqSender for sending Reject messages
     * @param manager Manager to use for actor lookup
     */
    ZmqReceiver(const std::string& bind_endpoint, std::shared_ptr<ZmqSender> sender, Manager* manager)
        : ZmqReceiver(bind_endpoint, sender) {
        manager_ = manager;
    }

    ~ZmqReceiver() {
        // Clean up proxy actors
        for (auto* proxy : proxies_) {
            delete proxy;
        }
    }

    /**
     * Register a local actor to receive remote messages
     */
    void register_actor(const std::string& name, Actor* actor) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        registry_[name] = actor;
    }

    /**
     * Unregister an actor
     */
    void unregister_actor(const std::string& name) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        registry_.erase(name);
    }

private:
    void on_start(const msg::Start*) noexcept {
        running_ = true;
        // Send ourselves a Continue to start polling
        send(new msg::Continue(), this);
    }

    void on_continue(const msg::Continue*) {
        if (!running_) return;

        // Poll for messages (non-blocking due to timeout)
        try {
            zmq::message_t message;
            auto result = socket_.recv(message, zmq::recv_flags::none);

            if (result.has_value()) {
                // Parse JSON
                std::string data(static_cast<char*>(message.data()), message.size());
                try {
                    nlohmann::json envelope = nlohmann::json::parse(data);
                    handle_remote_message(envelope);
                } catch (const nlohmann::json::exception& e) {
                    // JSON parse error - log and continue
                    std::cerr << "ZmqReceiver: JSON parse error: " << e.what()
                              << " data=" << data.substr(0, 200) << std::endl;
                }
            }
        } catch (const zmq::error_t& e) {
            // ZMQ error - ignore timeouts
            if (e.num() != EAGAIN) {
                std::cerr << "ZmqReceiver: ZMQ error: " << e.what() << std::endl;
            }
        }

        // Continue polling
        if (running_) {
            send(new msg::Continue(), this);
        }
    }

    void handle_remote_message(const nlohmann::json& envelope) {
        std::string receiver_name = envelope["receiver"].get<std::string>();
        std::string msg_type = envelope["message_type"].get<std::string>();

        std::cerr << "ZmqReceiver: Received message type=" << msg_type
                  << " for actor=" << receiver_name << std::endl;

        // Get sender info for replies
        std::string sender_actor;
        std::string sender_endpoint;
        bool has_sender = envelope.contains("sender_actor") && !envelope["sender_actor"].is_null();
        if (has_sender) {
            sender_actor = envelope["sender_actor"].get<std::string>();
            sender_endpoint = envelope["sender_endpoint"].get<std::string>();
        }

        // Find target actor - use Manager if available, otherwise manual registry
        Actor* target = nullptr;
        if (manager_) {
            target = manager_->get_local_actor(receiver_name);
        } else {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            auto it = registry_.find(receiver_name);
            if (it != registry_.end()) {
                target = it->second;
            }
        }

        if (!target) {
            std::cerr << "ZmqReceiver: Actor not found: " << receiver_name
                      << " msg_type=" << msg_type << std::endl;
            return;
        }


        // Deserialize message
        Message* msg = serialization::deserialize(msg_type, envelope["message"]);
        if (!msg) {
            std::cerr << "ZmqReceiver: Unknown message type: " << msg_type
                      << " for actor=" << receiver_name << std::endl;
            return;
        }

        std::cerr << "ZmqReceiver: Deserialized msg_type=" << msg_type
                  << " msg_id=" << msg->get_message_id() << std::endl;

        // Create proxy for reply routing
        Actor* reply_actor = nullptr;
        if (has_sender) {
            // Create a proxy that forwards replies to the remote sender
            auto* proxy = new RemoteReplyProxy(sender_, sender_actor, sender_endpoint);
            proxies_.push_back(proxy);
            reply_actor = proxy;
        }

        // CRITICAL: Set msg->sender to the reply proxy so recipient can reply/subscribe
        msg->sender = reply_actor;

        // Send to target actor
        std::cerr << "ZmqReceiver: target=" << target->get_name()
                  << " is_part_of_group=" << target->check_is_part_of_group()
                  << " group_ptr=" << (void*)target->get_group_ptr() << std::endl;

        std::cerr << "ZmqReceiver: Calling target->send() for " << target->get_name() << std::endl;
        target->send(msg, reply_actor);
        std::cerr << "ZmqReceiver: target->send() completed" << std::endl;
    }

    void send_reject(const std::string& endpoint,
                     const std::string& actor_name,
                     const std::string& msg_type,
                     const std::string& reason,
                     const std::string& rejected_by) {
        auto* reject = new msg::Reject(msg_type, reason, rejected_by);
        sender_->send_to(endpoint, actor_name, reject, nullptr);
    }

    void terminate() noexcept override {
        running_ = false;
        Actor::terminate();
    }

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
    std::shared_ptr<ZmqSender> sender_;
    std::string bind_endpoint_;
    Manager* manager_;
    std::unordered_map<std::string, Actor*> registry_;
    std::mutex registry_mutex_;
    bool running_;
    std::vector<RemoteReplyProxy*> proxies_;
};

} // namespace actors
