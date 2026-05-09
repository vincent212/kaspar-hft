/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/registry/RegistryClient.hpp"

#include <iostream>

namespace actors::registry {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RegistryClient::RegistryClient(const std::string& registry_address)
    : registry_address_(registry_address)
    , context_(1) {
}

RegistryClient::~RegistryClient() {
    stop_heartbeat_thread();
    disconnect();
}

// ============================================================================
// Connection management
// ============================================================================

void RegistryClient::connect() {
    if (connected_.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);

    socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::dealer);

    // Set socket options
    int linger = 0;
    socket_->set(zmq::sockopt::linger, linger);

    // Connect to registry
    socket_->connect(registry_address_);
    connected_.store(true);

    std::cout << "[RegistryClient] Connected to " << registry_address_ << std::endl;
}

void RegistryClient::disconnect() {
    stop_heartbeat_thread();

    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    connected_.store(false);
}

// ============================================================================
// Registration
// ============================================================================

bool RegistryClient::register_manager(const std::string& manager_id,
                                       const std::string& endpoint,
                                       const std::vector<std::string>& actors) {
    if (!connected_.load()) {
        throw RegistryError("Not connected to registry");
    }

    RegisterManager msg(manager_id, endpoint, actors);
    auto response = send_and_receive(MsgType::REGISTER_MANAGER, msg);

    if (response.empty()) {
        throw TimeoutError("No response from registry for REGISTER_MANAGER");
    }

    // We expect a HEARTBEAT_ACK as acknowledgment
    MsgType type = MessageSerializer::get_type(response.data(), response.size());
    if (type == MsgType::HEARTBEAT_ACK) {
        std::cout << "[RegistryClient] Manager '" << manager_id << "' registered with "
                  << actors.size() << " actors" << std::endl;
        return true;
    }

    return false;
}

void RegistryClient::unregister_manager(const std::string& manager_id) {
    if (!connected_.load()) {
        return;
    }

    UnregisterManager msg(manager_id);

    // Send without waiting for response (fire and forget)
    std::lock_guard<std::mutex> lock(socket_mutex_);
    auto data = MessageSerializer::serialize(MsgType::UNREGISTER_MANAGER, msg);
    zmq::message_t empty_msg(0);  // Empty delimiter frame
    zmq::message_t request(data.data(), data.size());
    socket_->send(empty_msg, zmq::send_flags::sndmore);
    socket_->send(request, zmq::send_flags::none);

    std::cout << "[RegistryClient] Manager '" << manager_id << "' unregistered" << std::endl;
}

bool RegistryClient::update_actors(const std::string& manager_id,
                                    const std::string& endpoint,
                                    const std::vector<std::string>& actors) {
    // Re-registration updates the actor list
    return register_manager(manager_id, endpoint, actors);
}

// ============================================================================
// Lookup
// ============================================================================

LookupResult RegistryClient::lookup_actor(const std::string& name) {
    if (!connected_.load()) {
        throw RegistryError("Not connected to registry");
    }

    LookupActor msg(name, heartbeat_manager_id_);
    auto response = send_and_receive(MsgType::LOOKUP_ACTOR, msg);

    if (response.empty()) {
        throw TimeoutError("No response from registry for LOOKUP_ACTOR");
    }

    MsgType type = MessageSerializer::get_type(response.data(), response.size());
    if (type != MsgType::LOOKUP_RESULT) {
        return LookupResult::not_found(name);
    }

    return MessageSerializer::deserialize_lookup_result(response.data(), response.size());
}

std::optional<std::string> RegistryClient::lookup_actor_endpoint(const std::string& name) {
    try {
        auto result = lookup_actor(name);
        if (result.found && !result.ambiguous) {
            return result.endpoint;
        }
        return std::nullopt;
    } catch (const RegistryError&) {
        return std::nullopt;
    }
}

// ============================================================================
// Heartbeat
// ============================================================================1

void RegistryClient::send_heartbeat(const std::string& manager_id) {
    if (!connected_.load()) {
        return;
    }

    Heartbeat msg(manager_id);

    std::lock_guard<std::mutex> lock(socket_mutex_);
    auto data = MessageSerializer::serialize(MsgType::HEARTBEAT, msg);
    zmq::message_t empty_msg(0);  // Empty delimiter frame
    zmq::message_t request(data.data(), data.size());
    socket_->send(empty_msg, zmq::send_flags::sndmore);
    socket_->send(request, zmq::send_flags::none);

    // We don't wait for the ack here to avoid blocking
}

void RegistryClient::start_heartbeat_thread(const std::string& manager_id,
                                             std::chrono::seconds interval) {
    if (heartbeat_running_.load()) {
        return;
    }

    heartbeat_manager_id_ = manager_id;
    heartbeat_running_.store(true);

    heartbeat_thread_ = std::make_unique<std::thread>(
        &RegistryClient::heartbeat_loop, this, manager_id, interval);

    std::cout << "[RegistryClient] Heartbeat thread started for '" << manager_id
              << "' (interval: " << interval.count() << "s)" << std::endl;
}

void RegistryClient::stop_heartbeat_thread() {
    if (!heartbeat_running_.load()) {
        return;
    }

    heartbeat_running_.store(false);

    if (heartbeat_thread_ && heartbeat_thread_->joinable()) {
        heartbeat_thread_->join();
    }
    heartbeat_thread_.reset();
}

void RegistryClient::heartbeat_loop(const std::string& manager_id, std::chrono::seconds interval) {
    (void)interval;  // Ignore parameter - hardcoded to 10s

    while (heartbeat_running_.load()) {
        // Sleep for 10 seconds in 1s increments to allow quick shutdown
        for (int i = 0; i < 10 && heartbeat_running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (heartbeat_running_.load()) {
            send_heartbeat(manager_id);
        }
    }
}

// ============================================================================
// Registry Queries
// ============================================================================

ManagersResponse RegistryClient::list_managers() {
    if (!connected_.load()) {
        throw RegistryError("Not connected to registry");
    }

    ListManagers msg;
    auto response = send_and_receive(MsgType::LIST_MANAGERS, msg);

    if (response.empty()) {
        throw TimeoutError("No response from registry for LIST_MANAGERS");
    }

    MsgType type = MessageSerializer::get_type(response.data(), response.size());
    if (type != MsgType::MANAGERS_RESPONSE) {
        throw RegistryError("Unexpected response type for LIST_MANAGERS");
    }

    return MessageSerializer::deserialize_managers_response(response.data(), response.size());
}

AllActorsResponse RegistryClient::list_all_actors() {
    if (!connected_.load()) {
        throw RegistryError("Not connected to registry");
    }

    ListAllActors msg;
    auto response = send_and_receive(MsgType::LIST_ALL_ACTORS, msg);

    if (response.empty()) {
        throw TimeoutError("No response from registry for LIST_ALL_ACTORS");
    }

    MsgType type = MessageSerializer::get_type(response.data(), response.size());
    if (type != MsgType::ALL_ACTORS_RESPONSE) {
        throw RegistryError("Unexpected response type for LIST_ALL_ACTORS");
    }

    return MessageSerializer::deserialize_all_actors_response(response.data(), response.size());
}

} // namespace actors::registry
