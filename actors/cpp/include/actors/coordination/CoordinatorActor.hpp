#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "CoordinatorMessages.hpp"
#include "CoordinationActorMessages.hpp"
#include "messages.hpp"
#include "actors/console/ConsoleMessages.hpp"
#include "actors/console/Table.hpp"
#include <map>
#include <set>
#include <deque>
#include <string>
#include <cstdint>

namespace actors::coordination {

/**
 * CoordinatorActor - Permission queue and coordination logic
 *
 * Responsibilities:
 * - Maintains permission queue for deterministic execution
 * - Handles TOKEN, REQUEST, GRANT, DONE protocol
 * - Manages group and actor registration
 * - Processes debug commands
 * - Tracks stale groups and cleanup
 *
 * This actor uses pure Actor messaging (no DEALER-ROUTER sockets).
 */
class CoordinatorActor : public Actor {
public:
    /**
     * Constructor - uses pure Actor messaging (no special setup needed)
     */
    CoordinatorActor();

    ~CoordinatorActor() override = default;

protected:
    void init() override;
    void end() override;
    void process_message(const Message* m) override;

private:
    // Message handlers for coordination protocol
    void on_register_group(const RegisterGroupMessage* msg);
    void on_register_actor(const RegisterActorMessage* msg);
    void on_permission_token(const PermissionTokenMessage* msg);
    void on_permission_request(const PermissionRequestMessage* msg);
    void on_permission_done(const PermissionDoneMessage* msg);
    void on_shutdown(const ShutdownSignal* msg);
    void on_get(const frame::cons::msg::Get* m) noexcept;

    // Core coordination logic
    void process_permission_queue();
    void grant_permission_to_next();
    void check_for_stale_groups();

    // Utility
    std::string get_timestamp_str() const;
    std::string msg_type_to_string(MsgType msg_type) const;
    void print_queue_state() const;

    // State - no longer need zmq_sender_ref, we use msg->sender for replies

    // Permission queue state
    struct PermissionEntry {
        std::string group_id;
        std::string actor_id;
        uint64_t sequence;
        uint64_t enqueue_time_ms;
        Actor* reply_to;  // RemoteReplyProxy for sending replies to this group
    };
    std::deque<PermissionEntry> token_queue_;  // TOKENs in arrival order
    std::map<std::string, PermissionEntry> pending_requests_;  // actor_id -> REQUEST
    uint64_t global_sequence_ = 0;

    // Group registration
    struct GroupInfo {
        std::string group_id;
        std::set<std::string> actors;
        uint64_t last_seen_ms;
        Actor* reply_to;  // RemoteReplyProxy for this group (from last message)
    };
    std::map<std::string, GroupInfo> groups_;  // group_id -> info

    // Actor to group mapping
    std::map<std::string, std::string> actor_to_group_;  // actor_id -> group_id

    // Debug control
    bool paused_ = false;
    bool shutdown_requested_ = false;
    bool logging_enabled_ = false;

    // Timing
    uint64_t last_stale_check_ms_ = 0;
    static constexpr uint64_t STALE_CHECK_INTERVAL_MS = 5000;
    static constexpr uint64_t STALE_TIMEOUT_MS = 30000;
    static constexpr uint64_t PERMISSION_TIMEOUT_MS = 10000;
};

} // namespace actors::coordination
