/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/coordination/GroupManager.hpp"

#include <iostream>
#include <chrono>
#include <algorithm>

namespace actors::coordination {

GroupManager::GroupManager()
    : context_(1)
{
}

GroupManager::~GroupManager()
{
    if (router_) {
        router_->close();
    }
}

void GroupManager::bind(const std::string& address)
{
    router_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::router);

    // Set socket options
    int linger = 0;
    router_->set(zmq::sockopt::linger, linger);

    // NO TIMEOUT on socket! We'll use zmq::poll() instead
    // This is the proper fix - no rcvtimeo means subsequent frames won't timeout

    router_->bind(address);
}

void GroupManager::run()
{
    if (!router_) {
        throw std::runtime_error("GroupManager::run() called before bind()");
    }

    running_ = true;
    auto last_stale_check = std::chrono::steady_clock::now();

    while (running_) {
        // Check for external shutdown request
        if (shutdown_requested_.load() && !shutting_down_) {
            std::cout << "[GroupManager::run] Shutdown requested externally" << std::endl;
            initiate_shutdown(shutdown_reason_.empty() ? "external request" : shutdown_reason_);
        }

        // Periodically check for stale groups (every 5 seconds)
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stale_check).count() >= 5) {
            check_stale_groups();
            last_stale_check = now;
        }

        // Use zmq::poll() to check if message is ready (non-blocking)
        zmq::pollitem_t items[] = { { *router_, 0, ZMQ_POLLIN, 0 } };
        zmq::poll(items, 1, std::chrono::milliseconds(100));

        if (!(items[0].revents & ZMQ_POLLIN)) {
            // No message ready - continue polling
            continue;
        }

        // Message is ready - receive all frames atomically
        // Once poll() returns ready, all frames are available without timeout
        zmq::message_t identity_msg;
        zmq::message_t empty_msg;
        zmq::message_t data_msg;

        // Frame 1: Identity
        auto res = router_->recv(identity_msg, zmq::recv_flags::none);
        if (!res) {
            std::cerr << "[GroupManager] ERROR: Failed to receive identity frame" << std::endl;
            continue;
        }

        // Frame 2: Empty delimiter
        res = router_->recv(empty_msg, zmq::recv_flags::none);
        if (!res) {
            std::cerr << "[GroupManager] ERROR: Failed to receive empty delimiter" << std::endl;
            continue;
        }

        // Frame 3: Data
        res = router_->recv(data_msg, zmq::recv_flags::none);
        if (!res) {
            std::cerr << "[GroupManager] ERROR: Failed to receive data frame" << std::endl;
            continue;
        }

        // Drain any extra frames
        int more = router_->get(zmq::sockopt::rcvmore);
        if (more) {
            zmq::message_t extra;
            while (more) {
                router_->recv(extra, zmq::recv_flags::none);
                more = router_->get(zmq::sockopt::rcvmore);
            }
        }

        // Extract identity as string
        std::string identity(static_cast<char*>(identity_msg.data()), identity_msg.size());

        // Handle the message
        handle_message(identity,
                       static_cast<uint8_t*>(data_msg.data()),
                       data_msg.size());
    }
}

void GroupManager::request_shutdown(const std::string& reason, bool force)
{
    shutdown_reason_ = reason;
    shutdown_requested_ = true;
    if (force) {
        running_ = false;
    }
}

bool GroupManager::is_group_registered(const std::string& group_id) const
{
    return registered_groups_.find(group_id) != registered_groups_.end();
}

bool GroupManager::is_actor_registered(const std::string& full_actor_id) const
{
    return actor_to_group_.find(full_actor_id) != actor_to_group_.end();
}

void GroupManager::handle_message(const std::string& identity, const uint8_t* data, size_t len)
{
    if (len < 1) return;

    MsgType type = MessageSerializer::get_type(data, len);

    switch (type) {
        case MsgType::REGISTER_GROUP: {
            auto msg = MessageSerializer::deserialize_register_group(data, len);
            handle_register_group(identity, msg);
            break;
        }
        case MsgType::REGISTER_ACTOR: {
            auto msg = MessageSerializer::deserialize_register_actor(data, len);
            handle_register_actor(identity, msg);
            break;
        }
        case MsgType::PERMISSION_TOKEN: {
            auto msg = MessageSerializer::deserialize_permission_token(data, len);
            handle_permission_token(identity, msg);
            break;
        }
        case MsgType::PERMISSION_REQUEST: {
            auto msg = MessageSerializer::deserialize_permission_request(data, len);
            handle_permission_request(identity, msg);
            break;
        }
        case MsgType::PERMISSION_DONE: {
            auto msg = MessageSerializer::deserialize_permission_done(data, len);
            handle_permission_done(identity, msg);
            break;
        }
        case MsgType::SHUTDOWN_REQUEST: {
            auto msg = MessageSerializer::deserialize_shutdown_request(data, len);
            handle_shutdown_request(identity, msg);
            break;
        }
        case MsgType::SHUTDOWN_ACK: {
            auto msg = MessageSerializer::deserialize_shutdown_ack(data, len);
            handle_shutdown_ack(identity, msg);
            break;
        }

        // Debug messages
        case MsgType::DEBUG_STOP: {
            auto msg = MessageSerializer::deserialize_debug_stop(data, len);
            handle_debug_stop(identity, msg);
            break;
        }
        case MsgType::DEBUG_CONTINUE: {
            handle_debug_continue(identity);
            break;
        }
        case MsgType::DEBUG_ADD_BREAKPOINT: {
            auto msg = MessageSerializer::deserialize_debug_add_breakpoint(data, len);
            handle_debug_add_breakpoint(identity, msg);
            break;
        }
        case MsgType::DEBUG_REMOVE_BREAKPOINT: {
            auto msg = MessageSerializer::deserialize_debug_remove_breakpoint(data, len);
            handle_debug_remove_breakpoint(identity, msg);
            break;
        }
        case MsgType::DEBUG_LIST_BREAKPOINTS: {
            handle_debug_list_breakpoints(identity);
            break;
        }
        case MsgType::DEBUG_LIST_GROUPS: {
            handle_debug_list_groups(identity);
            break;
        }
        case MsgType::DEBUG_LIST_ACTORS: {
            auto msg = MessageSerializer::deserialize_debug_list_actors(data, len);
            handle_debug_list_actors(identity, msg);
            break;
        }
        case MsgType::DEBUG_STATUS: {
            handle_debug_status(identity);
            break;
        }
        case MsgType::DEBUG_SET_LOGGING: {
            auto msg = MessageSerializer::deserialize_debug_set_logging(data, len);
            handle_debug_set_logging(identity, msg);
            break;
        }
        case MsgType::DEBUG_GET_QUEUE: {
            handle_debug_get_queue(identity);
            break;
        }

        default:
            // Unknown message type - ignore
            break;
    }
}

void GroupManager::handle_register_group(const std::string& identity, const RegisterGroup& msg)
{
    if (shutting_down_) {
        // Reject registration during shutdown
        send_to_identity(identity, MsgType::REGISTER_NACK,
                         RegisterNack(msg.group_id, "shutdown in progress"));
        return;
    }

    // Check if group already registered
    if (registered_groups_.find(msg.group_id) != registered_groups_.end()) {
        send_to_identity(identity, MsgType::REGISTER_NACK,
                         RegisterNack(msg.group_id, "duplicate group_id"));
        return;
    }

    // Register the group
    registered_groups_.insert(msg.group_id);
    group_identities_[msg.group_id] = identity;
    touch_group(msg.group_id);

    if (logging_enabled_) {
        std::cout << "[GroupManager] REGISTER_GROUP " << msg.group_id << std::endl;
    }

    // Send ACK
    send_to_identity(identity, MsgType::REGISTER_ACK, RegisterAck(msg.group_id));
}

void GroupManager::handle_register_actor(const std::string& identity, const RegisterActor& msg)
{
    if (shutting_down_) {
        send_to_identity(identity, MsgType::REGISTER_NACK,
                         RegisterNack(msg.full_id(), "shutdown in progress"));
        return;
    }

    // Check if group is registered
    if (registered_groups_.find(msg.group_id) == registered_groups_.end()) {
        send_to_identity(identity, MsgType::REGISTER_NACK,
                         RegisterNack(msg.full_id(), "group not registered"));
        return;
    }

    std::string full_id = msg.full_id();

    // Check if actor already registered
    if (actor_to_group_.find(full_id) != actor_to_group_.end()) {
        std::cerr << "[GroupManager] ERROR: Duplicate actor_id=\"" << full_id
                  << "\" already registered in group=\"" << actor_to_group_[full_id]
                  << "\", rejecting registration from group=\"" << msg.group_id << "\"" << std::endl;
        send_to_identity(identity, MsgType::REGISTER_NACK,
                         RegisterNack(full_id, "duplicate actor_id"));
        return;
    }

    // Register the actor
    actor_to_group_[full_id] = msg.group_id;

    // Send ACK
    send_to_identity(identity, MsgType::REGISTER_ACK, RegisterAck(full_id));
}

void GroupManager::handle_permission_token(const std::string& identity, const PermissionToken& token)
{
    (void)identity;  // Token can come from any group

    if (shutting_down_) {
        // Don't accept new tokens during shutdown
        return;
    }

    // Touch the sender's group to keep it alive
    std::string sender_group = get_group_from_actor_id(token.sender_id);
    if (!sender_group.empty()) {
        touch_group(sender_group);
    }

    if (logging_enabled_) {
        std::cout << "[GroupManager] TOKEN recipient=" << token.recipient_id
                  << " sender=" << token.sender_id
                  << " ts=" << token.timestamp << std::endl;
    }

    // Enqueue the token
    permission_queue_.push_back(token);

    // Try to grant if no active permission
    try_grant_next();
}

void GroupManager::handle_permission_request(const std::string& identity, const PermissionRequest& req)
{
    // Track this request so we can send GRANT proactively when it's their turn
    pending_requests_[req.actor_id] = identity;

    // Touch the group to keep it alive
    touch_group(req.group_id);

    // Check debug stopped state first
    if (debug_stopped_) {
        send_to_identity(identity, MsgType::PERMISSION_WAIT,
                         PermissionWait(req.actor_id, UINT32_MAX));
        return;
    }

    // Find position in queue
    uint32_t position = 0;
    bool found = false;

    for (size_t i = 0; i < permission_queue_.size(); i++) {
        if (permission_queue_[i].recipient_id == req.actor_id) {
            position = static_cast<uint32_t>(i);
            found = true;
            break;
        }
    }

    if (!found) {
        // Actor not in queue - this shouldn't happen in normal flow
        // Send WAIT with high position
        send_to_identity(identity, MsgType::PERMISSION_WAIT,
                         PermissionWait(req.actor_id, UINT32_MAX));
        return;
    }

    if (position == 0 && !active_permission_.has_value()) {
        // Check breakpoints before granting
        auto [matches, bp_id] = matches_breakpoint(req.actor_id);
        if (matches) {
            debug_stopped_ = true;
            debug_stopped_reason_ = "Breakpoint " + std::to_string(bp_id) +
                                    " hit on " + req.actor_id;
            if (logging_enabled_) {
                std::cout << "[GroupManager] BREAKPOINT " << bp_id
                          << " hit on " << req.actor_id << std::endl;
            }
            send_to_identity(identity, MsgType::PERMISSION_WAIT,
                             PermissionWait(req.actor_id, 0));
            return;
        }

        // At head of queue and no active permission - grant
        active_permission_ = ++global_sequence_;
        active_actor_ = req.actor_id;

        // Save token for tracing before removing from queue
        PermissionToken granted_token = permission_queue_.front();

        // Remove from queue and pending requests
        permission_queue_.pop_front();
        pending_requests_.erase(req.actor_id);

        // Record trace if enabled
        record_trace(granted_token, *active_permission_);

        if (logging_enabled_) {
            std::cout << "[GroupManager] GRANT actor=" << req.actor_id
                      << " seq=" << *active_permission_ << std::endl;
        }

        // Send grant
        send_to_identity(identity, MsgType::PERMISSION_GRANT,
                         PermissionGrant(req.actor_id, *active_permission_));

        if (on_permission_granted_) {
            on_permission_granted_(req.actor_id, *active_permission_);
        }
    } else {
        // Not at head or permission already active - send wait
        send_to_identity(identity, MsgType::PERMISSION_WAIT,
                         PermissionWait(req.actor_id, position));
    }
}

void GroupManager::handle_permission_done(const std::string& identity, const PermissionDone& done)
{
    (void)identity;

    // Touch the group to keep it alive
    std::string group_id = get_group_from_actor_id(done.actor_id);
    if (!group_id.empty()) {
        touch_group(group_id);
    }

    // Verify this is the active permission
    if (!active_permission_.has_value() || *active_permission_ != done.sequence) {
        // Mismatched sequence - ignore (could be duplicate or old message)
        return;
    }

    if (active_actor_ != done.actor_id) {
        // Wrong actor - ignore
        return;
    }

    if (logging_enabled_) {
        std::cout << "[GroupManager] DONE actor=" << done.actor_id
                  << " seq=" << done.sequence << std::endl;
    }

    // Clear active permission
    active_permission_.reset();
    active_actor_.clear();

    // If shutting down and queue is empty, proceed with shutdown
    if (shutting_down_ && permission_queue_.empty()) {
        // Send shutdown to all groups
        for (const auto& group_id : registered_groups_) {
            pending_shutdown_acks_.insert(group_id);
            send_to_group(group_id, MsgType::SHUTDOWN, Shutdown(shutdown_reason_));
        }

        // If no groups registered, just stop
        if (pending_shutdown_acks_.empty()) {
            running_ = false;
        }
        return;
    }

    // Try to grant next permission
    try_grant_next();
}

void GroupManager::handle_shutdown_request(const std::string& identity, const ShutdownRequest& req)
{
    (void)identity;

    if (shutting_down_) {
        return;  // Already shutting down
    }

    initiate_shutdown(req.reason);
}

void GroupManager::handle_shutdown_ack(const std::string& identity, const ShutdownAck& ack)
{
    (void)identity;

    pending_shutdown_acks_.erase(ack.group_id);

    if (pending_shutdown_acks_.empty()) {
        // All groups acknowledged - stop
        running_ = false;
    }
}

void GroupManager::try_grant_next()
{
    // Can't grant if already have an active permission
    if (active_permission_.has_value()) {
        return;
    }

    // Can't grant if queue is empty
    if (permission_queue_.empty()) {
        return;
    }

    // Don't auto-grant during shutdown (let queue drain via requests)
    if (shutting_down_) {
        return;
    }

    // Don't auto-grant when debug stopped
    if (debug_stopped_) {
        return;
    }

    // Get the actor at the head of the queue (copy, not reference, since we pop later)
    const std::string next_actor = permission_queue_.front().recipient_id;

    // Check if this actor has a pending request (is blocking waiting for GRANT)
    auto pending_it = pending_requests_.find(next_actor);
    if (pending_it == pending_requests_.end()) {
        // Actor hasn't requested yet - they will get GRANT when they do
        return;
    }

    // Check breakpoints before granting
    auto [matches, bp_id] = matches_breakpoint(next_actor);
    if (matches) {
        debug_stopped_ = true;
        debug_stopped_reason_ = "Breakpoint " + std::to_string(bp_id) +
                                " hit on " + next_actor;
        if (logging_enabled_) {
            std::cout << "[GroupManager] BREAKPOINT " << bp_id
                      << " hit on " << next_actor << std::endl;
        }
        // Send WAIT to the waiting group (they're still blocking)
        send_to_identity(pending_it->second, MsgType::PERMISSION_WAIT,
                         PermissionWait(next_actor, 0));
        return;
    }

    // Proactively grant permission to the waiting group
    active_permission_ = ++global_sequence_;
    active_actor_ = next_actor;

    // Save token for tracing before removing from queue
    PermissionToken granted_token = permission_queue_.front();

    // Get the identity before removing from pending
    std::string identity = pending_it->second;

    // Remove from queue and pending requests
    permission_queue_.pop_front();
    pending_requests_.erase(pending_it);

    // Record trace if enabled
    record_trace(granted_token, *active_permission_);

    if (logging_enabled_) {
        std::cout << "[GroupManager] GRANT (proactive) actor=" << next_actor
                  << " seq=" << *active_permission_ << std::endl;
    }

    // Send grant to the waiting group
    send_to_identity(identity, MsgType::PERMISSION_GRANT,
                     PermissionGrant(next_actor, *active_permission_));

    if (on_permission_granted_) {
        on_permission_granted_(next_actor, *active_permission_);
    }
}

void GroupManager::initiate_shutdown(const std::string& reason)
{
    std::cout << "[GroupManager::initiate_shutdown] Called with reason=\"" << reason << "\"" << std::endl;
    if (shutting_down_) {
        std::cout << "[GroupManager::initiate_shutdown] Already shutting down, ignoring" << std::endl;
        return;
    }

    shutting_down_ = true;
    shutdown_reason_ = reason;
    std::cout << "[GroupManager::initiate_shutdown] Shutdown initiated" << std::endl;

    // If no active permission and queue is empty, shutdown immediately
    if (!active_permission_.has_value() && permission_queue_.empty()) {
        std::cout << "[GroupManager::initiate_shutdown] No active permission, queue empty" << std::endl;
        for (const auto& group_id : registered_groups_) {
            pending_shutdown_acks_.insert(group_id);
            send_to_group(group_id, MsgType::SHUTDOWN, Shutdown(reason));
        }

        if (pending_shutdown_acks_.empty()) {
            std::cout << "[GroupManager::initiate_shutdown] No groups registered, setting running_=false" << std::endl;
            running_ = false;
        }
    }
    // Otherwise, let queue drain naturally
}

std::string GroupManager::get_group_from_actor_id(const std::string& full_actor_id)
{
    auto dot_pos = full_actor_id.find('.');
    if (dot_pos == std::string::npos) {
        return "";
    }
    return full_actor_id.substr(0, dot_pos);
}

// ============================================================================
// Debug interface implementation
// ============================================================================

void GroupManager::set_debug_stopped(bool stopped, const std::string& reason)
{
    debug_stopped_ = stopped;
    debug_stopped_reason_ = reason;
}

std::pair<bool, uint32_t> GroupManager::matches_breakpoint(const std::string& actor_id) const
{
    for (const auto& [id, pattern] : breakpoints_) {
        // Exact match
        if (pattern == actor_id) {
            return {true, id};
        }
        // Wildcard: "group.*" matches "group.anything"
        if (pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == ".*") {
            std::string prefix = pattern.substr(0, pattern.size() - 1);  // "group."
            if (actor_id.compare(0, prefix.size(), prefix) == 0) {
                return {true, id};
            }
        }
    }
    return {false, 0};
}

// ============================================================================
// Debug message handlers
// ============================================================================

void GroupManager::handle_debug_stop(const std::string& identity, const DebugStop& msg)
{
    debug_stopped_ = true;
    debug_stopped_reason_ = msg.reason.empty() ? "Manual stop" : msg.reason;

    if (logging_enabled_) {
        std::cout << "[GroupManager] DEBUG_STOP reason=\"" << debug_stopped_reason_ << "\"" << std::endl;
    }

    // Send status as confirmation
    DebugStatusResponse status;
    status.stopped = true;
    status.stopped_reason = debug_stopped_reason_;
    status.current_sequence = global_sequence_;
    status.queue_size = static_cast<uint32_t>(permission_queue_.size());
    status.active_actor = active_actor_;
    send_to_identity(identity, MsgType::DEBUG_STATUS_RESPONSE, status);
}

void GroupManager::handle_debug_continue(const std::string& identity)
{
    debug_stopped_ = false;
    debug_stopped_reason_.clear();

    if (logging_enabled_) {
        std::cout << "[GroupManager] DEBUG_CONTINUE" << std::endl;
    }

    // Send status as confirmation
    DebugStatusResponse status;
    status.stopped = false;
    status.stopped_reason = "";
    status.current_sequence = global_sequence_;
    status.queue_size = static_cast<uint32_t>(permission_queue_.size());
    status.active_actor = active_actor_;
    send_to_identity(identity, MsgType::DEBUG_STATUS_RESPONSE, status);

    // Try to grant next now that we're resumed
    try_grant_next();
}

void GroupManager::handle_debug_add_breakpoint(const std::string& identity, const DebugAddBreakpoint& msg)
{
    uint32_t id = next_breakpoint_id_++;
    breakpoints_[id] = msg.pattern;

    if (logging_enabled_) {
        std::cout << "[GroupManager] BREAKPOINT_ADDED id=" << id
                  << " pattern=\"" << msg.pattern << "\"" << std::endl;
    }

    DebugBreakpointAdded response;
    response.breakpoint_id = id;
    response.pattern = msg.pattern;
    send_to_identity(identity, MsgType::DEBUG_BREAKPOINT_ADDED, response);
}

void GroupManager::handle_debug_remove_breakpoint(const std::string& identity, const DebugRemoveBreakpoint& msg)
{
    auto it = breakpoints_.find(msg.breakpoint_id);
    bool removed = (it != breakpoints_.end());
    if (removed) {
        if (logging_enabled_) {
            std::cout << "[GroupManager] BREAKPOINT_REMOVED id=" << msg.breakpoint_id << std::endl;
        }
        breakpoints_.erase(it);
    }

    // Send updated breakpoints list
    handle_debug_list_breakpoints(identity);
}

void GroupManager::handle_debug_list_breakpoints(const std::string& identity)
{
    DebugBreakpointsResponse response;
    for (const auto& [id, pattern] : breakpoints_) {
        response.ids.push_back(id);
        response.patterns.push_back(pattern);
    }
    send_to_identity(identity, MsgType::DEBUG_BREAKPOINTS_RESPONSE, response);
}

void GroupManager::handle_debug_list_groups(const std::string& identity)
{
    DebugGroupsResponse response;
    for (const auto& group : registered_groups_) {
        response.groups.push_back(group);
    }
    send_to_identity(identity, MsgType::DEBUG_GROUPS_RESPONSE, response);
}

void GroupManager::handle_debug_list_actors(const std::string& identity, const DebugListActors& msg)
{
    DebugActorsResponse response;
    response.group_id = msg.group_id;

    std::string prefix = msg.group_id + ".";
    for (const auto& [actor, group] : actor_to_group_) {
        if (group == msg.group_id) {
            // Extract actor name without group prefix
            if (actor.size() > prefix.size() &&
                actor.compare(0, prefix.size(), prefix) == 0) {
                response.actors.push_back(actor.substr(prefix.size()));
            }
        }
    }
    send_to_identity(identity, MsgType::DEBUG_ACTORS_RESPONSE, response);
}

void GroupManager::handle_debug_status(const std::string& identity)
{
    DebugStatusResponse status;
    status.stopped = debug_stopped_;
    status.stopped_reason = debug_stopped_reason_;
    status.current_sequence = global_sequence_;
    status.queue_size = static_cast<uint32_t>(permission_queue_.size());
    status.active_actor = active_actor_;
    send_to_identity(identity, MsgType::DEBUG_STATUS_RESPONSE, status);
}

void GroupManager::handle_debug_set_logging(const std::string& identity, const DebugSetLogging& msg)
{
    logging_enabled_ = msg.enabled;

    if (logging_enabled_) {
        std::cout << "[GroupManager] LOGGING_ENABLED" << std::endl;
    }

    DebugLoggingResponse response;
    response.enabled = logging_enabled_;
    send_to_identity(identity, MsgType::DEBUG_LOGGING_RESPONSE, response);
}

void GroupManager::handle_debug_get_queue(const std::string& identity)
{
    DebugQueueResponse response;
    for (const auto& token : permission_queue_) {
        response.recipient_ids.push_back(token.recipient_id);
        response.sender_ids.push_back(token.sender_id);
        response.timestamps.push_back(token.timestamp);
    }
    send_to_identity(identity, MsgType::DEBUG_QUEUE_RESPONSE, response);
}

// ============================================================================
// Group Lifecycle Management
// ============================================================================

void GroupManager::touch_group(const std::string& group_id)
{
    group_last_seen_[group_id] = std::chrono::steady_clock::now();
}

void GroupManager::unregister_group(const std::string& group_id)
{
    if (logging_enabled_) {
        std::cout << "[GroupManager] UNREGISTER_GROUP " << group_id << " (stale/disconnected)" << std::endl;
    }

    // Remove all actors belonging to this group
    std::vector<std::string> actors_to_remove;
    for (const auto& [actor_id, grp_id] : actor_to_group_) {
        if (grp_id == group_id) {
            actors_to_remove.push_back(actor_id);
        }
    }
    for (const auto& actor_id : actors_to_remove) {
        actor_to_group_.erase(actor_id);
        pending_requests_.erase(actor_id);
    }

    // Remove tokens from permission queue that belong to this group's actors
    permission_queue_.erase(
        std::remove_if(permission_queue_.begin(), permission_queue_.end(),
            [&](const PermissionToken& token) {
                return get_group_from_actor_id(token.recipient_id) == group_id;
            }),
        permission_queue_.end()
    );

    // Clear active permission if it was this group's actor
    if (active_permission_.has_value() && get_group_from_actor_id(active_actor_) == group_id) {
        active_permission_.reset();
        active_actor_.clear();
    }

    // Remove group registration
    registered_groups_.erase(group_id);
    group_identities_.erase(group_id);
    group_last_seen_.erase(group_id);
    pending_shutdown_acks_.erase(group_id);

    // Try to grant next permission (queue may have changed)
    try_grant_next();
}

void GroupManager::check_stale_groups()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> stale_groups;

    for (const auto& [group_id, last_seen] : group_last_seen_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_seen).count();
        if (elapsed > GROUP_TIMEOUT_SECONDS) {
            stale_groups.push_back(group_id);
        }
    }

    for (const auto& group_id : stale_groups) {
        unregister_group(group_id);
    }
}

// ============================================================================
// Tracing interface implementation
// ============================================================================

bool GroupManager::enable_tracing(const std::string& file_path)
{
    if (tracing_enabled_) {
        disable_tracing();
    }

    trace_file_.open(file_path, std::ios::out | std::ios::trunc);
    if (!trace_file_.is_open()) {
        return false;
    }

    trace_file_path_ = file_path;
    tracing_enabled_ = true;

    if (logging_enabled_) {
        std::cout << "[GroupManager] TRACING_ENABLED file=\"" << file_path << "\"" << std::endl;
    }

    return true;
}

void GroupManager::disable_tracing()
{
    if (!tracing_enabled_) {
        return;
    }

    tracing_enabled_ = false;

    if (trace_file_.is_open()) {
        trace_file_.close();
    }

    if (logging_enabled_) {
        std::cout << "[GroupManager] TRACING_DISABLED" << std::endl;
    }

    trace_file_path_.clear();
}

void GroupManager::record_trace(const PermissionToken& token, uint64_t sequence)
{
    if (!tracing_enabled_ || !trace_file_.is_open()) {
        return;
    }

    // Write JSON line (manual JSON to avoid external dependency)
    trace_file_ << "{\"sequence\":" << sequence
                << ",\"timestamp\":" << token.timestamp
                << ",\"recipient\":\"" << token.recipient_id << "\""
                << ",\"sender\":\"" << token.sender_id << "\""
                << ",\"message_type\":\"" << token.message_type << "\"}\n";

    // Flush for crash safety
    trace_file_.flush();
}

} // namespace actors::coordination
