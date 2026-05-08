/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/coordination/CoordinatorActor.hpp"
#include "actors/coordination/CoordinationActorMessages.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>

// Uncomment to enable verbose coordination logging
#define DEBUG_LOGGING

namespace actors::coordination {

CoordinatorActor::CoordinatorActor()
{
    strcpy(name, "coordinator");
}

void CoordinatorActor::init() {
    std::cout << "[CoordinatorActor] Initializing..." << std::endl;

    // Register message handlers for coordination protocol
    MESSAGE_HANDLER(RegisterGroupMessage, on_register_group);
    MESSAGE_HANDLER(RegisterActorMessage, on_register_actor);
    MESSAGE_HANDLER(PermissionTokenMessage, on_permission_token);
    MESSAGE_HANDLER(PermissionRequestMessage, on_permission_request);
    MESSAGE_HANDLER(PermissionDoneMessage, on_permission_done);
    MESSAGE_HANDLER(ShutdownSignal, on_shutdown);
    MESSAGE_HANDLER(frame::cons::msg::Get, on_get);

    last_stale_check_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::cout << "[CoordinatorActor] Ready (using pure Actor messaging)" << std::endl;
}

void CoordinatorActor::end() {
    std::cout << "[CoordinatorActor] Shutting down..." << std::endl;
    std::cout << "[CoordinatorActor] Final stats:" << std::endl;
    std::cout << "  Registered groups: " << groups_.size() << std::endl;
    std::cout << "  Token queue size: " << token_queue_.size() << std::endl;
    std::cout << "  Pending requests: " << pending_requests_.size() << std::endl;
}

void CoordinatorActor::process_message(const Message* m) {
    // Default processing - registered handlers called via MESSAGE_HANDLER macro
}

void CoordinatorActor::on_register_group(const RegisterGroupMessage* msg) {
    std::cout << "[CoordinatorActor] REGISTER_GROUP: " << msg->reg.group_id << std::endl;

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Register or update group
    auto& group = groups_[msg->reg.group_id];
    group.group_id = msg->reg.group_id;
    group.reply_to = msg->sender;  // Store RemoteReplyProxy for replies
    group.last_seen_ms = now_ms;

    std::cout << "[CoordinatorActor] Group " << msg->reg.group_id
              << " registered (total groups: " << groups_.size() << ")" << std::endl;

    // Send ACK via msg->sender (RemoteReplyProxy)
    RegisterAck ack(msg->reg.group_id);
    auto* ack_msg = new RegisterAckMessage(ack);
    msg->sender->send(ack_msg, this);
}

void CoordinatorActor::on_register_actor(const RegisterActorMessage* msg) {
    std::cout << "[CoordinatorActor] REGISTER_ACTOR: " << msg->reg.actor_id
              << " in group " << msg->reg.group_id << std::endl;

    // Find group
    auto it = groups_.find(msg->reg.group_id);
    if (it == groups_.end()) {
        std::cerr << "[CoordinatorActor] ERROR: Cannot register actor " << msg->reg.actor_id
                  << " - group " << msg->reg.group_id << " not found" << std::endl;

        RegisterNack nack(msg->reg.full_id(), "group not registered");
        auto* nack_msg = new RegisterNackMessage(nack);
        msg->sender->send(nack_msg, this);
        return;
    }

    // Register actor in group
    it->second.actors.insert(msg->reg.actor_id);
    actor_to_group_[msg->reg.actor_id] = msg->reg.group_id;

    std::cout << "[CoordinatorActor] Actor " << msg->reg.actor_id
              << " registered (group now has " << it->second.actors.size()
              << " actors)" << std::endl;

    // Send ACK
    RegisterAck ack(msg->reg.full_id());
    auto* ack_msg = new RegisterAckMessage(ack);
    msg->sender->send(ack_msg, this);
}

void CoordinatorActor::on_permission_token(const PermissionTokenMessage* msg) {
#ifdef DEBUG_LOGGING
    std::cout << "[CoordinatorActor] TOKEN sender=" << msg->token.sender_id
              << " recipient=" << msg->token.recipient_id << std::endl;
#endif

    // Extract recipient's group from actor ID
    auto pos = msg->token.recipient_id.find('.');
    if (pos == std::string::npos) {
        std::cerr << "[CoordinatorActor] ERROR: Invalid recipient_id format: "
                  << msg->token.recipient_id << " (sender=" << msg->token.sender_id << ")" << std::endl;
        return;
    }
    std::string recipient_group = msg->token.recipient_id.substr(0, pos);

    auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());

    // Extract sender's group and update last_seen
    auto sender_dot = msg->token.sender_id.find('.');
    if (sender_dot != std::string::npos) {
        std::string sender_group = msg->token.sender_id.substr(0, sender_dot);
        auto it = groups_.find(sender_group);
        if (it != groups_.end()) {
            it->second.last_seen_ms = now_ms;
        }
    }

    // Touch recipient group (it will process this message)
    auto rit = groups_.find(recipient_group);
    if (rit != groups_.end()) {
        rit->second.last_seen_ms = now_ms;
    }

    // Add TOKEN to queue
    token_queue_.push_back({
        recipient_group,
        msg->token.recipient_id,
        ++global_sequence_,
        now_ms,
        msg->sender  // Store reply_to for this token
    });

#ifdef DEBUG_LOGGING
    print_queue_state();
#endif

    if (!paused_) {
        // Check if this TOKEN is at head of queue (can grant immediately)
        if (token_queue_.front().actor_id == msg->token.recipient_id) {
            // This TOKEN is at head of queue
            auto req_it = pending_requests_.find(msg->token.recipient_id);
            if (req_it != pending_requests_.end()) {
                // Matching REQUEST exists - GRANT immediately
                auto& entry = token_queue_.front();

                #ifdef DEBUG_LOGGING
                std::cout << "[CoordinatorActor] *** GRANTED seq=" << entry.sequence
                          << " to " << entry.actor_id
                          << " (group=" << entry.group_id << ") ***" << std::endl;
                #endif

                // Send GRANT message via the reply_to from REQUEST
                PermissionGrant grant(entry.actor_id, entry.sequence);
                auto* grant_msg = new PermissionGrantMessage(grant);
                req_it->second.reply_to->send(grant_msg, this);

                // Remove from both structures
                token_queue_.pop_front();
                pending_requests_.erase(req_it);

#ifdef DEBUG_LOGGING
                print_queue_state();
#endif
            }
        }
    }
}

void CoordinatorActor::on_permission_request(const PermissionRequestMessage* msg) {
#ifdef DEBUG_LOGGING
    std::cout << "[CoordinatorActor] REQUEST from " << msg->request.group_id
              << " actor=" << msg->request.actor_id << std::endl;
#endif

    auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());

    // Check for duplicate REQUEST
    if (pending_requests_.count(msg->request.actor_id) > 0) {
        std::cerr << "[CoordinatorActor] ERROR: Duplicate REQUEST for " << msg->request.actor_id << std::endl;
        abort();
        return;
    }

    // Add REQUEST to pending map
    pending_requests_[msg->request.actor_id] = {
        msg->request.group_id,
        msg->request.actor_id,
        ++global_sequence_,
        now_ms,
        msg->sender  // Store reply_to for sending GRANT
    };

#ifdef DEBUG_LOGGING
    print_queue_state();
#endif

    if (!paused_) {
        // Check if matching TOKEN at head of queue
        if (!token_queue_.empty() && token_queue_.front().actor_id == msg->request.actor_id) {
            // Matching TOKEN exists at head - GRANT immediately
            auto& entry = token_queue_.front();

            std::cout << "[CoordinatorActor] *** GRANTED seq=" << entry.sequence
                      << " to " << entry.actor_id
                      << " (group=" << entry.group_id << ") ***" << std::endl;

            // Send GRANT message via msg->sender (REQUEST's reply_to)
            PermissionGrant grant(entry.actor_id, entry.sequence);
            auto* grant_msg = new PermissionGrantMessage(grant);
            msg->sender->send(grant_msg, this);

            // Remove from both structures
            token_queue_.pop_front();
            pending_requests_.erase(msg->request.actor_id);

#ifdef DEBUG_LOGGING
            print_queue_state();
#endif
        }
    }

    // Update last_seen
    auto it = groups_.find(msg->request.group_id);
    if (it != groups_.end()) {
        it->second.last_seen_ms = now_ms;
    }
}

void CoordinatorActor::on_permission_done(const PermissionDoneMessage* msg) {
    // DONE is a no-op - no logging needed
}

void CoordinatorActor::on_shutdown(const ShutdownSignal* msg) {
    std::cout << "[CoordinatorActor] Shutdown requested: " << msg->reason << std::endl;
    shutdown_requested_ = true;
    terminated = true;
}

void CoordinatorActor::process_permission_queue() {
    if (token_queue_.empty()) {
        return;
    }

    // Check if head of token queue has matching request
    auto& entry = token_queue_.front();
    auto req_it = pending_requests_.find(entry.actor_id);
    if (req_it != pending_requests_.end()) {
        // Both TOKEN and REQUEST exist - GRANT
#ifdef DEBUG_LOGGING
        std::cout << "[CoordinatorActor] GRANTING ### seq=" << entry.sequence
                  << " to " << entry.actor_id
                  << " (group=" << entry.group_id << ")" << std::endl;
#endif

        // Send GRANT message via REQUEST's reply_to
        PermissionGrant grant(entry.actor_id, entry.sequence);
        auto* grant_msg = new PermissionGrantMessage(grant);
        req_it->second.reply_to->send(grant_msg, this);

        // Remove from both structures
        token_queue_.pop_front();
        pending_requests_.erase(req_it);
    }
}

void CoordinatorActor::grant_permission_to_next() {
    // This function is no longer used - logic moved to process_permission_queue()
}

void CoordinatorActor::check_for_stale_groups() {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::vector<std::string> stale_groups;

    for (const auto& [group_id, info] : groups_) {
        uint64_t idle_time = now_ms - info.last_seen_ms;
        if (idle_time > STALE_TIMEOUT_MS) {
            std::cout << "[CoordinatorActor] WARNING: Group " << group_id
                      << " is stale (idle for " << idle_time << "ms)" << std::endl;
            stale_groups.push_back(group_id);
        }
    }

    // Remove stale groups
    for (const auto& group_id : stale_groups) {
        std::cout << "[CoordinatorActor] Removing stale group: " << group_id << std::endl;

        // Remove actors
        auto& group = groups_[group_id];
        for (const auto& actor_id : group.actors) {
            actor_to_group_.erase(actor_id);
        }

        // Remove group
        groups_.erase(group_id);
    }
}

std::string CoordinatorActor::get_timestamp_str() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::ostringstream oss;
    oss << ms;
    return oss.str();
}

std::string CoordinatorActor::msg_type_to_string(MsgType msg_type) const {
    switch (msg_type) {
        case MsgType::REGISTER_GROUP: return "REGISTER_GROUP";
        case MsgType::REGISTER_ACTOR: return "REGISTER_ACTOR";
        case MsgType::REGISTER_ACK: return "REGISTER_ACK";
        case MsgType::REGISTER_NACK: return "REGISTER_NACK";
        case MsgType::PERMISSION_TOKEN: return "PERMISSION_TOKEN";
        case MsgType::PERMISSION_REQUEST: return "PERMISSION_REQUEST";
        case MsgType::PERMISSION_GRANT: return "PERMISSION_GRANT";
        case MsgType::PERMISSION_WAIT: return "PERMISSION_WAIT";
        case MsgType::PERMISSION_DONE: return "PERMISSION_DONE";
        case MsgType::SHUTDOWN_REQUEST: return "SHUTDOWN_REQUEST";
        case MsgType::SHUTDOWN: return "SHUTDOWN";
        case MsgType::SHUTDOWN_ACK: return "SHUTDOWN_ACK";
        case MsgType::DEBUG_STOP: return "DEBUG_STOP";
        case MsgType::DEBUG_CONTINUE: return "DEBUG_CONTINUE";
        case MsgType::DEBUG_STATUS: return "DEBUG_STATUS";
        case MsgType::DEBUG_FLUSH: return "DEBUG_FLUSH";
        case MsgType::DEBUG_SET_LOGGING: return "DEBUG_SET_LOGGING";
        default: return "UNKNOWN_" + std::to_string(static_cast<int>(msg_type));
    }
}

void CoordinatorActor::print_queue_state() const {
    std::cout << "  [STATE] Token queue (" << token_queue_.size() << "): [";
    bool first = true;
    for (const auto& entry : token_queue_) {
        if (!first) std::cout << ", ";
        std::cout << entry.actor_id;
        first = false;
    }
    std::cout << "]" << std::endl;

    std::cout << "  [STATE] Pending requests (" << pending_requests_.size() << "): {";
    first = true;
    for (const auto& [actor_id, entry] : pending_requests_) {
        if (!first) std::cout << ", ";
        std::cout << actor_id;
        first = false;
    }
    std::cout << "}" << std::endl;
}

void CoordinatorActor::on_get(const frame::cons::msg::Get* m) noexcept {
    using namespace frame::cons;

    const std::string& what = m->what;

    try {
        if (what == "queue") {
            // Show token queue
            Table table("Token Queue", {"Position", "Actor ID", "Group ID", "Sequence", "Age (ms)"});

            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            int pos = 1;
            for (const auto& entry : token_queue_) {
                table.add_row();
                table.set_value(pos++);
                table.set_value(entry.actor_id);
                table.set_value(entry.group_id);
                table.set_value(static_cast<int>(entry.sequence));
                table.set_value(static_cast<int>(now_ms - entry.enqueue_time_ms));
            }

            reply(new msg::Page(table.to_json(true)));

        } else if (what == "requests") {
            // Show pending requests
            Table table("Pending Requests", {"Actor ID", "Group ID", "Sequence", "Age (ms)"});

            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            for (const auto& [actor_id, entry] : pending_requests_) {
                table.add_row();
                table.set_value(entry.actor_id);
                table.set_value(entry.group_id);
                table.set_value(static_cast<int>(entry.sequence));
                table.set_value(static_cast<int>(now_ms - entry.enqueue_time_ms));
            }

            reply(new msg::Page(table.to_json(true)));

        } else if (what == "status") {
            // Show coordinator status
            Table table("Coordinator Status", {"Property", "Value"});

            table.add_row();
            table.set_value("Paused");
            table.set_value(paused_ ? "YES" : "NO");

            table.add_row();
            table.set_value("Logging");
            table.set_value(logging_enabled_ ? "ENABLED" : "DISABLED");

            table.add_row();
            table.set_value("Groups");
            table.set_value(static_cast<int>(groups_.size()));

            table.add_row();
            table.set_value("Token Queue Size");
            table.set_value(static_cast<int>(token_queue_.size()));

            table.add_row();
            table.set_value("Pending Requests");
            table.set_value(static_cast<int>(pending_requests_.size()));

            table.add_row();
            table.set_value("Global Sequence");
            table.set_value(static_cast<int>(global_sequence_));

            if (!token_queue_.empty()) {
                table.add_row();
                table.set_value("Head of Queue");
                table.set_value(token_queue_.front().actor_id);
            }

            reply(new msg::Page(table.to_json(true)));

        } else if (what == "stop") {
            // Pause coordination
            paused_ = true;
            std::cout << "[CoordinatorActor] MQ0 STOP command - pausing coordination" << std::endl;
            reply(new msg::Page("Coordination PAUSED"));

        } else if (what == "continue") {
            // Resume coordination
            paused_ = false;
            std::cout << "[CoordinatorActor] MQ0 CONTINUE command - resuming coordination" << std::endl;

            // Process queue immediately
            process_permission_queue();

            reply(new msg::Page("Coordination RESUMED"));

        } else if (what == "step") {
            // Single-step: grant one permission then pause
            std::cout << "[CoordinatorActor] MQ0 STEP command" << std::endl;

            if (token_queue_.empty()) {
                reply(new msg::Page("Token queue is empty - nothing to step"));
            } else {
                // Check if we can grant (TOKEN at head with matching REQUEST)
                auto& entry = token_queue_.front();
                auto req_it = pending_requests_.find(entry.actor_id);

                if (req_it != pending_requests_.end()) {
                    // Will grant - unpause temporarily
                    paused_ = false;
                    process_permission_queue();
                    paused_ = true;
                    std::cout << "[CoordinatorActor] STEP complete - paused again" << std::endl;
                    reply(new msg::Page("Granted permission and paused"));
                } else {
                    // No matching REQUEST - can't grant
                    std::cout << "[CoordinatorActor] STEP blocked - no matching REQUEST for " << entry.actor_id << std::endl;
                    reply(new msg::Page("Cannot grant - no matching REQUEST for " + entry.actor_id));
                }
            }

        } else if (what == "flush") {
            // Clear all queues
            std::cout << "[CoordinatorActor] MQ0 FLUSH command - clearing queues" << std::endl;
            std::cout << "  Flushing " << token_queue_.size() << " tokens" << std::endl;
            std::cout << "  Flushing " << pending_requests_.size() << " pending requests" << std::endl;

            token_queue_.clear();
            pending_requests_.clear();

            std::cout << "[CoordinatorActor] Flush complete" << std::endl;
            reply(new msg::Page("Queues flushed"));

        } else if (what == "groups") {
            // List registered groups
            Table table("Registered Groups", {"Group ID", "Actors", "Last Seen (ms ago)"});

            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            for (const auto& [group_id, info] : groups_) {
                table.add_row();
                table.set_value(group_id);
                table.set_value(static_cast<int>(info.actors.size()));
                table.set_value(static_cast<int>(now_ms - info.last_seen_ms));
            }

            reply(new msg::Page(table.to_json(true)));

        } else {
            // Unknown command
            reply(new msg::Page("ERROR: Unknown command '" + what + "'\n\n"
                "Available commands:\n"
                "  queue      - Show token queue\n"
                "  requests   - Show pending requests\n"
                "  status     - Show coordinator status\n"
                "  stop       - Pause coordination\n"
                "  continue   - Resume coordination\n"
                "  step       - Grant one permission then pause\n"
                "  flush      - Clear all queues\n"
                "  groups     - List registered groups\n"));
        }

    } catch (const std::exception& e) {
        reply(new msg::Page("ERROR: " + std::string(e.what())));
    }
}

} // namespace actors::coordination
