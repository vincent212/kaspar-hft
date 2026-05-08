#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <iostream>
#include <typeinfo>
#include "actors/Actor.hpp"

namespace actors {

// Forward declarations
class ZmqSender;

/**
 * LocalActorRef - Reference to an actor in the same process
 */
class LocalActorRef {
    Actor* actor_;

public:
    explicit LocalActorRef(Actor* a) : actor_(a) {}

    void send(const Message* m, Actor* sender = nullptr) {
        actor_->send(m, sender);
    }

    std::unique_ptr<const Message> fast_send(const Message* m, Actor* sender) {
        return actor_->fast_send(m, sender);
    }

    const char* name() const { return actor_->get_name(); }
    Actor* actor() const { return actor_; }
};

/**
 * RustActorRef - Reference to a Rust actor via FFI
 *
 * Communicates via extern "C" functions (rust_actor_send).
 * The send() implementation is in actors-interop.
 */
class RustActorRef {
    std::string target_name_;
    std::string sender_name_;

public:
    RustActorRef(std::string target, std::string sender = "")
        : target_name_(std::move(target))
        , sender_name_(std::move(sender)) {}

    // Implemented in actors-interop (RustActorRef.cpp)
    void send(const Message* m, Actor* sender = nullptr);

    const std::string& name() const { return target_name_; }
    const std::string& sender() const { return sender_name_; }
};

/**
 * RemoteActorRef - Reference to an actor in another process
 *
 * Communicates via ZeroMQ using JSON wire protocol.
 */
class RemoteActorRef {
    std::string name_;
    std::string endpoint_;
    std::shared_ptr<ZmqSender> sender_;

public:
    RemoteActorRef(std::string name, std::string endpoint, std::shared_ptr<ZmqSender> sender)
        : name_(std::move(name))
        , endpoint_(std::move(endpoint))
        , sender_(std::move(sender)) {}

    // Implemented in ZmqSender.cpp to avoid circular dependency
    void send(const Message* m, Actor* sender = nullptr);

    const std::string& name() const { return name_; }
    const std::string& endpoint() const { return endpoint_; }
    std::shared_ptr<ZmqSender> sender() const { return sender_; }
};

/**
 * ActorRef - Unified reference to local or remote actor
 *
 * Uses std::variant for zero-overhead polymorphism.
 * Same send() syntax for both local and remote actors.
 *
 * Usage:
 *   ActorRef local_ref(pong_actor);
 *   ActorRef remote_ref("pong", "tcp://localhost:5001", zmq_sender);
 *
 *   local_ref.send(new Ping{1}, this);   // local
 *   remote_ref.send(new Ping{1}, this);  // remote - same syntax!
 */
class ActorRef {
    std::variant<LocalActorRef, RemoteActorRef, RustActorRef> ref_;

public:
    // Default constructor - creates an empty/invalid ref
    ActorRef() : ref_(LocalActorRef(nullptr)) {}

    // Construct from local actor
    explicit ActorRef(Actor* a) : ref_(LocalActorRef(a)) {}

    // Construct from remote actor
    ActorRef(std::string name, std::string endpoint, std::shared_ptr<ZmqSender> sender)
        : ref_(RemoteActorRef(std::move(name), std::move(endpoint), std::move(sender))) {}

    // Construct from Rust actor
    explicit ActorRef(RustActorRef rust_ref) : ref_(std::move(rust_ref)) {}

    // Copy/move constructors
    ActorRef(const ActorRef&) = default;
    ActorRef(ActorRef&&) = default;
    ActorRef& operator=(const ActorRef&) = default;
    ActorRef& operator=(ActorRef&&) = default;

    /**
     * Send a message asynchronously
     * Works identically for local and remote actors
     */
    void send(const Message* m, Actor* sender = nullptr) {
        std::visit([&](auto& r) { r.send(m, sender); }, ref_);
    }

    /**
     * Send a message synchronously (local only)
     * Throws if called on remote actor
     */
    std::unique_ptr<const Message> fast_send(const Message* m, Actor* sender) {
        if (auto* local = std::get_if<LocalActorRef>(&ref_)) {
            return local->fast_send(m, sender);
        }
        throw std::runtime_error("fast_send not supported for remote actors");
    }

    bool is_local() const { return std::holds_alternative<LocalActorRef>(ref_); }
    bool is_remote() const { return std::holds_alternative<RemoteActorRef>(ref_); }
    bool is_rust() const { return std::holds_alternative<RustActorRef>(ref_); }

    // Check if this is a valid (non-null) reference
    bool is_valid() const {
        if (auto* local = std::get_if<LocalActorRef>(&ref_)) {
            return local->actor() != nullptr;
        }
        return true;  // Remote and Rust refs are always valid if constructed
    }

    explicit operator bool() const { return is_valid(); }

    // Get name (works for both local and remote)
    std::string name() const {
        return std::visit([](const auto& r) -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(r)>, LocalActorRef>) {
                return r.name();
            } else {
                return r.name();
            }
        }, ref_);
    }

    // Access underlying local actor (throws if remote)
    Actor* actor() const {
        if (auto* local = std::get_if<LocalActorRef>(&ref_)) {
            return local->actor();
        }
        throw std::runtime_error("Cannot get actor pointer for remote actor");
    }

    // Access underlying remote ref (throws if local)
    const RemoteActorRef& remote_ref() const {
        if (auto* remote = std::get_if<RemoteActorRef>(&ref_)) {
            return *remote;
        }
        throw std::runtime_error("Cannot get remote ref for local actor");
    }
};

} // namespace actors
