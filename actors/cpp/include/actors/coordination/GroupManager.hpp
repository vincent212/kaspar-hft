#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <deque>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <memory>
#include <atomic>
#include <utility>
#include <chrono>
#include <fstream>

#include <zmq.hpp>

#include "messages.hpp"

namespace actors::coordination {

/**
 * GroupManager - Central coordinator for deterministic simulation.
 *
 * The GroupManager serializes execution across all Groups by:
 * 1. Maintaining a FIFO queue of permission tokens
 * 2. Granting permission to only one actor at a time
 * 3. Coordinating shutdown across all Groups
 *
 * All Groups connect to the GroupManager via ZeroMQ DEALER sockets.
 * The GroupManager uses a ROUTER socket to handle multiple connections.
 *
 * Default port: tcp://*:5555
 *
 * Usage:
 *   GroupManager gm;
 *   gm.bind("tcp://*:5555");
 *   gm.run();  // Blocks until shutdown
 */
class GroupManager {
public:
    GroupManager();
    ~GroupManager();

    // Non-copyable
    GroupManager(const GroupManager&) = delete;
    GroupManager& operator=(const GroupManager&) = delete;

    /**
     * Bind to a ZeroMQ address.
     * @param address ZeroMQ address (e.g., "tcp://*:5555")
     */
    void bind(const std::string& address);

    /**
     * Run the main event loop.
     * Blocks until shutdown is complete.
     */
    void run();

    /**
     * Request shutdown (thread-safe).
     * Can be called from signal handler.
     * @param force If true, stop immediately without waiting for ACKs
     */
    void request_shutdown(const std::string& reason = "external request", bool force = false);

    /**
     * Check if currently running.
     */
    bool is_running() const { return running_.load(); }

    // ========================================================================
    // Query methods (for testing)
    // ========================================================================

    /**
     * Check if a group is registered.
     */
    bool is_group_registered(const std::string& group_id) const;

    /**
     * Check if an actor is registered.
     * @param full_actor_id Full actor ID (group.actor)
     */
    bool is_actor_registered(const std::string& full_actor_id) const;

    /**
     * Get the number of registered groups.
     */
    size_t group_count() const { return registered_groups_.size(); }

    /**
     * Get the number of registered actors.
     */
    size_t actor_count() const { return actor_to_group_.size(); }

    /**
     * Get the current permission queue size.
     */
    size_t queue_size() const { return permission_queue_.size(); }

    /**
     * Get the current global sequence number.
     */
    uint64_t current_sequence() const { return global_sequence_; }

    /**
     * Set a callback for when permission is granted (for testing/debugging).
     */
    void set_permission_granted_callback(std::function<void(const std::string&, uint64_t)> cb) {
        on_permission_granted_ = std::move(cb);
    }

    // ========================================================================
    // Debug interface
    // ========================================================================

    /**
     * Check if debug stopped (paused).
     */
    bool is_debug_stopped() const { return debug_stopped_; }

    /**
     * Get the reason for debug stop.
     */
    const std::string& debug_stopped_reason() const { return debug_stopped_reason_; }

    /**
     * Get the number of breakpoints.
     */
    size_t breakpoint_count() const { return breakpoints_.size(); }

    /**
     * Set debug stopped state (for --paused startup).
     */
    void set_debug_stopped(bool stopped, const std::string& reason = "");

    /**
     * Check if permission logging is enabled.
     */
    bool is_logging_enabled() const { return logging_enabled_; }

    /**
     * Enable/disable permission logging.
     */
    void set_logging_enabled(bool enabled) { logging_enabled_ = enabled; }

    // ========================================================================
    // Tracing interface (debug feature, off by default)
    // ========================================================================

    /**
     * Enable tracing to a file.
     * Writes JSON Lines format: one JSON object per permission grant.
     * @param file_path Path to the trace file
     * @return true if file was opened successfully, false otherwise
     */
    bool enable_tracing(const std::string& file_path);

    /**
     * Disable tracing and close the file.
     */
    void disable_tracing();

    /**
     * Check if tracing is enabled.
     */
    bool is_tracing_enabled() const { return tracing_enabled_; }

    /**
     * Get the trace file path.
     */
    const std::string& trace_file_path() const { return trace_file_path_; }

    /**
     * Get name (for logging).
     */
    const char* get_name() const { return "GroupManager"; }

private:
    // ZeroMQ context and socket
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> router_;

    // Registry
    std::set<std::string> registered_groups_;
    std::map<std::string, std::string> actor_to_group_;  // full_actor_id -> group_id
    std::map<std::string, std::string> group_identities_;  // group_id -> ZMQ identity (as string)
    std::map<std::string, std::chrono::steady_clock::time_point> group_last_seen_;  // group_id -> last activity time
    static constexpr int GROUP_TIMEOUT_SECONDS = 30;  // Unregister groups after 30s inactivity

    // Permission queue (FIFO)
    std::deque<PermissionToken> permission_queue_;
    std::optional<uint64_t> active_permission_;  // Sequence of currently granted permission
    std::string active_actor_;  // Actor that currently has permission
    uint64_t global_sequence_ = 0;

    // Track pending requests - actor_id -> identity (for proactive GRANT)
    std::map<std::string, std::string> pending_requests_;

    // Shutdown state
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    bool shutting_down_ = false;
    std::string shutdown_reason_;
    std::set<std::string> pending_shutdown_acks_;

    // Callbacks
    std::function<void(const std::string&, uint64_t)> on_permission_granted_;

    // Debug state
    bool debug_stopped_ = false;
    std::string debug_stopped_reason_;
    std::map<uint32_t, std::string> breakpoints_;  // id -> pattern
    uint32_t next_breakpoint_id_ = 1;
    bool logging_enabled_ = false;

    // Tracing state
    bool tracing_enabled_ = false;
    std::string trace_file_path_;
    std::ofstream trace_file_;

    // ========================================================================
    // Message handlers
    // ========================================================================

    void handle_message(const std::string& identity, const uint8_t* data, size_t len);

    void handle_register_group(const std::string& identity, const RegisterGroup& msg);
    void handle_register_actor(const std::string& identity, const RegisterActor& msg);
    void handle_permission_token(const std::string& identity, const PermissionToken& token);
    void handle_permission_request(const std::string& identity, const PermissionRequest& req);
    void handle_permission_done(const std::string& identity, const PermissionDone& done);
    void handle_shutdown_request(const std::string& identity, const ShutdownRequest& req);
    void handle_shutdown_ack(const std::string& identity, const ShutdownAck& ack);

    // Debug message handlers
    void handle_debug_stop(const std::string& identity, const DebugStop& msg);
    void handle_debug_continue(const std::string& identity);
    void handle_debug_add_breakpoint(const std::string& identity, const DebugAddBreakpoint& msg);
    void handle_debug_remove_breakpoint(const std::string& identity, const DebugRemoveBreakpoint& msg);
    void handle_debug_list_breakpoints(const std::string& identity);
    void handle_debug_list_groups(const std::string& identity);
    void handle_debug_list_actors(const std::string& identity, const DebugListActors& msg);
    void handle_debug_status(const std::string& identity);
    void handle_debug_set_logging(const std::string& identity, const DebugSetLogging& msg);
    void handle_debug_get_queue(const std::string& identity);

    // ========================================================================
    // Internal helpers
    // ========================================================================

    /**
     * Try to grant permission to the next actor in the queue.
     */
    void try_grant_next();

    /**
     * Unregister a group and all its actors.
     */
    void unregister_group(const std::string& group_id);

    /**
     * Check for stale groups and unregister them.
     */
    void check_stale_groups();

    /**
     * Update last seen time for a group.
     */
    void touch_group(const std::string& group_id);

    /**
     * Initiate coordinated shutdown.
     */
    void initiate_shutdown(const std::string& reason);

    /**
     * Send a message to a specific group.
     */
    template<typename T>
    void send_to_group(const std::string& group_id, MsgType type, const T& msg);

    /**
     * Send a message to an identity.
     */
    template<typename T>
    void send_to_identity(const std::string& identity, MsgType type, const T& msg);

    /**
     * Get group ID from actor's full ID.
     */
    static std::string get_group_from_actor_id(const std::string& full_actor_id);

    /**
     * Check if actor_id matches any breakpoint pattern.
     * @param actor_id Full actor ID (group.actor)
     * @return pair<matches, breakpoint_id> - true and ID if matches, false and 0 if not
     */
    std::pair<bool, uint32_t> matches_breakpoint(const std::string& actor_id) const;

    /**
     * Record a trace entry for a permission grant.
     * No-op if tracing is disabled.
     * @param token The permission token being granted
     * @param sequence The sequence number assigned
     */
    void record_trace(const PermissionToken& token, uint64_t sequence);
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename T>
void GroupManager::send_to_group(const std::string& group_id, MsgType type, const T& msg) {
    auto it = group_identities_.find(group_id);
    if (it == group_identities_.end()) {
        return;  // Group not connected
    }
    send_to_identity(it->second, type, msg);
}

template<typename T>
void GroupManager::send_to_identity(const std::string& identity, MsgType type, const T& msg) {
    if (!router_) return;

    auto data = MessageSerializer::serialize(type, msg);

    // ROUTER socket requires identity frame first
    zmq::message_t id_msg(identity.data(), identity.size());
    zmq::message_t empty_msg(0);  // Delimiter
    zmq::message_t data_msg(data.data(), data.size());

    router_->send(id_msg, zmq::send_flags::sndmore);
    router_->send(empty_msg, zmq::send_flags::sndmore);
    router_->send(data_msg, zmq::send_flags::none);
}

} // namespace actors::coordination
