#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/coordination/CoordinatorMessages.hpp"
#include "actors/coordination/messages.hpp"
#include <map>
#include <set>
#include <string>
#include <chrono>

namespace actors::registry {

using namespace actors::coordination;

/**
 * RegistryActor - Actor-based Global Registry
 *
 * Responsibilities:
 * - Manages actor and manager registrations
 * - Handles actor lookups
 * - Processes heartbeats and timeouts
 *
 * This actor contains the registry logic extracted from GlobalRegistry.
 */
class RegistryActor : public Actor {
public:
    /**
     * Constructor
     * @param zmq_router_ref Reference to ZmqRouterSender for sending messages
     */
    explicit RegistryActor(Actor* zmq_router_ref);

    ~RegistryActor() override = default;

    /**
     * Set the ZmqRouterSender reference
     */
    void set_router(Actor* zmq_router_ref) { zmq_router_ref_ = zmq_router_ref; }

    /**
     * Set heartbeat timeout duration
     */
    void set_heartbeat_timeout(std::chrono::seconds timeout) {
        heartbeat_timeout_ = timeout;
    }

    /**
     * Enable/disable logging
     */
    void set_logging_enabled(bool enabled) {
        logging_enabled_ = enabled;
    }

    // Query methods (for testing/monitoring)
    size_t manager_count() const { return managers_.size(); }
    size_t actor_count() const;
    bool is_manager_registered(const std::string& manager_id) const;
    bool is_actor_registered(const std::string& actor_name) const;
    std::string get_actor_endpoint(const std::string& name) const;
    std::vector<std::string> get_manager_ids() const;
    std::vector<std::string> get_manager_actors(const std::string& manager_id) const;
    bool is_running() const { return !shutdown_requested_; }

protected:
    void init() override;
    void end() override;
    void process_message(const Message* m) override;

private:
    // Message handlers
    void on_incoming_zmq(const IncomingZmqMessage* msg);
    void on_shutdown(const ShutdownSignal* msg);

    // Protocol message handlers
    void handle_register_manager(const std::string& identity, const RegisterManager& msg);
    void handle_unregister_manager(const std::string& identity, const UnregisterManager& msg);
    void handle_lookup_actor(const std::string& identity, const LookupActor& msg);
    void handle_heartbeat(const std::string& identity, const Heartbeat& msg);
    void handle_list_managers(const std::string& identity, const ListManagers& msg);
    void handle_list_all_actors(const std::string& identity, const ListAllActors& msg);

    // Internal helpers
    void unregister_manager_internal(const std::string& manager_id);
    void check_heartbeat_timeouts();
    LookupResult do_lookup(const std::string& name) const;
    static std::pair<std::string, std::string> parse_actor_name(const std::string& name);

    // Utility
    void send_zmq(const std::string& identity, MsgType msg_type, const std::vector<uint8_t>& data);
    std::string get_timestamp_str() const;

    // State
    Actor* zmq_router_ref_;

    // Registry data
    std::map<std::string, ManagerInfo> managers_;
    std::map<std::string, std::set<std::string>> actor_to_managers_;

    // Config
    std::chrono::seconds heartbeat_timeout_{30};
    bool logging_enabled_{true};
    bool shutdown_requested_{false};

    // Timing
    uint64_t last_timeout_check_ms_ = 0;
    static constexpr uint64_t TIMEOUT_CHECK_INTERVAL_MS = 5000;
};

} // namespace actors::registry
