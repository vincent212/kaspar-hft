#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <zmq.hpp>

#include "actors/coordination/messages.hpp"

namespace actors::registry {

using namespace actors::coordination;

/**
 * Error types for registry operations.
 */
class RegistryError : public std::runtime_error {
public:
    explicit RegistryError(const std::string& msg) : std::runtime_error(msg) {}
};

class ActorNotFoundError : public RegistryError {
public:
    explicit ActorNotFoundError(const std::string& name)
        : RegistryError("Actor not found: " + name), actor_name_(name) {}
    const std::string& actor_name() const { return actor_name_; }
private:
    std::string actor_name_;
};

class ActorAmbiguousError : public RegistryError {
public:
    explicit ActorAmbiguousError(const std::string& name)
        : RegistryError("Actor name is ambiguous (exists in multiple managers): " + name), actor_name_(name) {}
    const std::string& actor_name() const { return actor_name_; }
private:
    std::string actor_name_;
};

class RegistrationFailedError : public RegistryError {
public:
    RegistrationFailedError(const std::string& name, const std::string& reason)
        : RegistryError("Registration failed for '" + name + "': " + reason)
        , actor_name_(name), reason_(reason) {}
    const std::string& actor_name() const { return actor_name_; }
    const std::string& reason() const { return reason_; }
private:
    std::string actor_name_;
    std::string reason_;
};

class TimeoutError : public RegistryError {
public:
    explicit TimeoutError(const std::string& msg) : RegistryError("Timeout: " + msg) {}
};

/**
 * RegistryClient - Client for Manager/Group to communicate with GlobalRegistry.
 *
 * Provides methods to:
 * - Register a manager and its actors
 * - Look up actors by name (qualified or unqualified)
 * - Send heartbeats to keep registration alive
 * - Unregister on shutdown
 *
 * Usage:
 *   RegistryClient client("tcp://localhost:5556");
 *   client.connect();
 *   client.register_manager("group1", "tcp://localhost:6001", {"actor1", "actor2"});
 *   auto result = client.lookup_actor("group2::actorA");
 *   if (result.found) {
 *       // Use result.endpoint to send messages
 *   }
 *   client.unregister_manager("group1");
 *   client.disconnect();
 *
 * Thread Safety:
 *   The client is thread-safe for concurrent lookup calls.
 *   Registration/unregistration should be done from a single thread.
 */
class RegistryClient {
public:
    /**
     * Create a client for the specified registry address.
     * @param registry_address ZeroMQ address (e.g., "tcp://localhost:5556")
     */
    explicit RegistryClient(const std::string& registry_address);
    ~RegistryClient();

    // Non-copyable
    RegistryClient(const RegistryClient&) = delete;
    RegistryClient& operator=(const RegistryClient&) = delete;

    // ========================================================================
    // Connection management
    // ========================================================================

    /**
     * Connect to the GlobalRegistry.
     * Must be called before any other operations.
     */
    void connect();

    /**
     * Disconnect from the GlobalRegistry.
     * Stops heartbeat thread if running.
     */
    void disconnect();

    /**
     * Check if connected to the registry.
     */
    bool is_connected() const { return connected_.load(); }

    // ========================================================================
    // Registration
    // ========================================================================

    /**
     * Register this manager and its actors with the GlobalRegistry.
     *
     * @param manager_id Unique manager/group ID
     * @param endpoint ZMQ endpoint for reaching this manager's actors
     * @param actors List of actor names (NOT qualified - will be stored as-is)
     * @return true if registration was acknowledged
     */
    bool register_manager(const std::string& manager_id,
                          const std::string& endpoint,
                          const std::vector<std::string>& actors);

    /**
     * Unregister this manager (graceful shutdown).
     * All actors owned by this manager will also be unregistered.
     *
     * @param manager_id The manager ID that was used during registration
     */
    void unregister_manager(const std::string& manager_id);

    /**
     * Update the actor list for an already-registered manager.
     * This re-registers the manager with the new actor list.
     *
     * @param manager_id Manager ID
     * @param endpoint Manager endpoint
     * @param actors New complete list of actors
     * @return true if registration was acknowledged
     */
    bool update_actors(const std::string& manager_id,
                       const std::string& endpoint,
                       const std::vector<std::string>& actors);

    // ========================================================================
    // Lookup
    // ========================================================================

    /**
     * Look up an actor's endpoint by name.
     *
     * Supports both name formats:
     * - Unqualified: "actor_name" - searches all managers (must be unique)
     * - Qualified: "manager::actor_name" - searches specific manager
     *
     * @param name Actor name (qualified or unqualified)
     * @return LookupResult with found/ambiguous status and endpoint
     */
    LookupResult lookup_actor(const std::string& name);

    /**
     * Look up an actor and return just the endpoint (simpler interface).
     *
     * @param name Actor name (qualified or unqualified)
     * @return Endpoint if found and unambiguous, nullopt otherwise
     */
    std::optional<std::string> lookup_actor_endpoint(const std::string& name);

    // ========================================================================
    // Heartbeat
    // ========================================================================

    /**
     * Send a single heartbeat to keep registration alive.
     * Normally called automatically by the heartbeat thread.
     *
     * @param manager_id The registered manager ID
     */
    void send_heartbeat(const std::string& manager_id);

    /**
     * Start automatic heartbeat sending.
     * Heartbeats are sent every `interval` seconds.
     *
     * @param manager_id The registered manager ID
     * @param interval Heartbeat interval (default: 10 seconds)
     */
    void start_heartbeat_thread(const std::string& manager_id,
                                std::chrono::seconds interval = std::chrono::seconds(10));

    /**
     * Stop automatic heartbeat sending.
     */
    void stop_heartbeat_thread();

    /**
     * Check if heartbeat thread is running.
     */
    bool is_heartbeat_running() const { return heartbeat_running_.load(); }

    // ========================================================================
    // Registry Queries
    // ========================================================================

    /**
     * Query all registered managers.
     *
     * @return ManagersResponse with lists of manager IDs and endpoints
     */
    ManagersResponse list_managers();

    /**
     * Query all registered actors across all managers.
     *
     * @return AllActorsResponse with actor names and their manager IDs
     */
    AllActorsResponse list_all_actors();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * Set the timeout for waiting for responses from the registry.
     * Default: 5 seconds
     */
    void set_timeout(std::chrono::milliseconds timeout) {
        timeout_ = timeout;
    }

    /**
     * Get the manager ID (if registered).
     */
    const std::string& manager_id() const { return heartbeat_manager_id_; }

private:
    std::string registry_address_;
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> socket_;
    std::atomic<bool> connected_{false};
    std::chrono::milliseconds timeout_{5000};

    // Heartbeat thread
    std::unique_ptr<std::thread> heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::string heartbeat_manager_id_;

    // Thread safety for socket operations
    mutable std::mutex socket_mutex_;

    // ========================================================================
    // Internal helpers
    // ========================================================================

    /**
     * Send a message and wait for a response.
     * @return Response data, or empty vector on timeout
     */
    template<typename T>
    std::vector<uint8_t> send_and_receive(MsgType type, const T& msg);

    /**
     * Heartbeat thread function.
     */
    void heartbeat_loop(const std::string& manager_id, std::chrono::seconds interval);
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename T>
std::vector<uint8_t> RegistryClient::send_and_receive(MsgType type, const T& msg) {
    std::lock_guard<std::mutex> lock(socket_mutex_);

    if (!socket_ || !connected_.load()) {
        return {};
    }

    // Serialize and send with empty delimiter (DEALER->ROUTER protocol)
    auto data = MessageSerializer::serialize(type, msg);
    zmq::message_t empty_msg(0);  // Empty delimiter frame
    zmq::message_t request(data.data(), data.size());
    socket_->send(empty_msg, zmq::send_flags::sndmore);
    socket_->send(request, zmq::send_flags::none);

    // Wait for response with timeout
    zmq::pollitem_t items[] = {
        { socket_->handle(), 0, ZMQ_POLLIN, 0 }
    };

    zmq::poll(items, 1, timeout_);

    if (items[0].revents & ZMQ_POLLIN) {
        // ROUTER sends back: empty delimiter, then data
        zmq::message_t empty_resp;
        zmq::message_t response;

        auto res1 = socket_->recv(empty_resp, zmq::recv_flags::none);
        if (!res1) return {};

        auto res2 = socket_->recv(response, zmq::recv_flags::none);
        if (res2) {
            return std::vector<uint8_t>(
                static_cast<uint8_t*>(response.data()),
                static_cast<uint8_t*>(response.data()) + response.size()
            );
        }
    }

    return {};  // Timeout or error
}

} // namespace actors::registry
