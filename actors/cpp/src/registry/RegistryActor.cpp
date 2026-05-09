/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/registry/RegistryActor.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <iomanip>

namespace actors::registry {

RegistryActor::RegistryActor(Actor* zmq_router_ref)
    : zmq_router_ref_(zmq_router_ref)
{
    strcpy(name, "RegistryActor");
}

void RegistryActor::init() {
    std::cout << "[RegistryActor] Initializing..." << std::endl;

    // Register message handlers
    MESSAGE_HANDLER(IncomingZmqMessage, on_incoming_zmq);
    MESSAGE_HANDLER(ShutdownSignal, on_shutdown);

    last_timeout_check_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void RegistryActor::end() {
    std::cout << "[RegistryActor] Shutting down..." << std::endl;
    std::cout << "[RegistryActor] Final stats:" << std::endl;
    std::cout << "  Registered managers: " << managers_.size() << std::endl;
    std::cout << "  Total actors: " << actor_count() << std::endl;
}

void RegistryActor::process_message(const Message* m) {
    // Default processing - registered handlers called via MESSAGE_HANDLER macro
}

void RegistryActor::on_incoming_zmq(const IncomingZmqMessage* msg) {
    if (msg->data.empty()) {
        std::cerr << "[RegistryActor] ERROR: Received empty message" << std::endl;
        return;
    }

    // Deserialize message type
    MsgType msg_type = MessageSerializer::get_type(msg->data.data(), msg->data.size());

    if (logging_enabled_) {
        std::cout << "[RegistryActor] Received message type " << static_cast<int>(msg_type)
                  << " from " << msg->identity << std::endl;
    }

    // Dispatch based on message type
    switch (msg_type) {
        case MsgType::REGISTER_MANAGER: {
            auto reg = MessageSerializer::deserialize_register_manager(msg->data.data(), msg->data.size());
            handle_register_manager(msg->identity, reg);
            break;
        }
        case MsgType::UNREGISTER_MANAGER: {
            auto unreg = MessageSerializer::deserialize_unregister_manager(msg->data.data(), msg->data.size());
            handle_unregister_manager(msg->identity, unreg);
            break;
        }
        case MsgType::LOOKUP_ACTOR: {
            auto lookup = MessageSerializer::deserialize_lookup_actor(msg->data.data(), msg->data.size());
            handle_lookup_actor(msg->identity, lookup);
            break;
        }
        case MsgType::HEARTBEAT: {
            auto hb = MessageSerializer::deserialize_heartbeat(msg->data.data(), msg->data.size());
            handle_heartbeat(msg->identity, hb);
            break;
        }
        case MsgType::LIST_MANAGERS: {
            auto req = MessageSerializer::deserialize_list_managers(msg->data.data(), msg->data.size());
            handle_list_managers(msg->identity, req);
            break;
        }
        case MsgType::LIST_ALL_ACTORS: {
            auto req = MessageSerializer::deserialize_list_all_actors(msg->data.data(), msg->data.size());
            handle_list_all_actors(msg->identity, req);
            break;
        }
        default:
            std::cerr << "[RegistryActor] ERROR: Unknown message type "
                      << static_cast<int>(msg_type) << std::endl;
            break;
    }

    // Periodically check for heartbeat timeouts
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (now_ms - last_timeout_check_ms_ >= TIMEOUT_CHECK_INTERVAL_MS) {
        check_heartbeat_timeouts();
        last_timeout_check_ms_ = now_ms;
    }
}

void RegistryActor::on_shutdown(const ShutdownSignal* msg) {
    std::cout << "[RegistryActor] Shutdown requested: " << msg->reason << std::endl;
    shutdown_requested_ = true;
    terminated = true;
}

void RegistryActor::handle_register_manager(const std::string& identity, const RegisterManager& msg) {
    if (logging_enabled_) {
        std::cout << "[RegistryActor] REGISTER manager: " << msg.manager_id
                  << " at " << msg.endpoint
                  << " with " << msg.actors.size() << " actors" << std::endl;
    }

    // Check if already registered (re-registration)
    if (managers_.find(msg.manager_id) != managers_.end()) {
        if (logging_enabled_) {
            std::cout << "[RegistryActor]   Re-registering (unregister first)" << std::endl;
        }
        unregister_manager_internal(msg.manager_id);
    }

    // Create manager info
    ManagerInfo info(msg.manager_id, msg.endpoint, identity);

    // Register each actor
    for (const auto& actor : msg.actors) {
        info.actors.insert(actor);
        actor_to_managers_[actor].insert(msg.manager_id);
        if (logging_enabled_) {
            std::cout << "[RegistryActor]   + " << actor << std::endl;
        }
    }

    managers_[msg.manager_id] = std::move(info);

    // Send acknowledgment
    HeartbeatAck ack;
    ack.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    auto ack_data = MessageSerializer::serialize(MsgType::HEARTBEAT_ACK, ack);
    send_zmq(identity, MsgType::HEARTBEAT_ACK, ack_data);
}

void RegistryActor::handle_unregister_manager(const std::string& identity, const UnregisterManager& msg) {
    (void)identity;

    if (logging_enabled_) {
        std::cout << "[RegistryActor] UNREGISTER manager: " << msg.manager_id << std::endl;
    }
    unregister_manager_internal(msg.manager_id);
}

void RegistryActor::handle_lookup_actor(const std::string& identity, const LookupActor& msg) {
    if (logging_enabled_) {
        std::cout << "[RegistryActor] LOOKUP: " << msg.name
                  << " from: " << msg.requester_id << std::endl;
    }

    auto result = do_lookup(msg.name);

    if (logging_enabled_) {
        if (result.found) {
            std::cout << "[RegistryActor]   -> " << result.endpoint << std::endl;
        } else if (result.ambiguous) {
            std::cout << "[RegistryActor]   -> AMBIGUOUS (multiple managers)" << std::endl;
        } else {
            std::cout << "[RegistryActor]   -> NOT FOUND" << std::endl;
        }
    }

    auto result_data = MessageSerializer::serialize(MsgType::LOOKUP_RESULT, result);
    send_zmq(identity, MsgType::LOOKUP_RESULT, result_data);
}

void RegistryActor::handle_heartbeat(const std::string& identity, const Heartbeat& msg) {
    auto it = managers_.find(msg.manager_id);
    if (it != managers_.end()) {
        it->second.last_heartbeat = std::chrono::steady_clock::now();
        it->second.zmq_identity = identity;
    }

    // Send ack
    HeartbeatAck ack;
    ack.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    auto ack_data = MessageSerializer::serialize(MsgType::HEARTBEAT_ACK, ack);
    send_zmq(identity, MsgType::HEARTBEAT_ACK, ack_data);
}

void RegistryActor::unregister_manager_internal(const std::string& manager_id) {
    auto it = managers_.find(manager_id);
    if (it == managers_.end()) {
        return;
    }

    // Remove all actors owned by this manager
    for (const auto& actor : it->second.actors) {
        auto actor_it = actor_to_managers_.find(actor);
        if (actor_it != actor_to_managers_.end()) {
            actor_it->second.erase(manager_id);
            if (actor_it->second.empty()) {
                actor_to_managers_.erase(actor_it);
            }
        }
    }

    managers_.erase(it);
    if (logging_enabled_) {
        std::cout << "[RegistryActor] Manager " << manager_id << " removed" << std::endl;
    }
}

void RegistryActor::check_heartbeat_timeouts() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> timed_out;

    for (const auto& [manager_id, info] : managers_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - info.last_heartbeat
        );
        if (elapsed > heartbeat_timeout_) {
            timed_out.push_back(manager_id);
        }
    }

    for (const auto& manager_id : timed_out) {
        if (logging_enabled_) {
            std::cout << "[RegistryActor] TIMEOUT: Manager " << manager_id
                      << " (no heartbeat for " << heartbeat_timeout_.count() << "s)" << std::endl;
        }
        unregister_manager_internal(manager_id);
    }
}

std::pair<std::string, std::string> RegistryActor::parse_actor_name(const std::string& name) {
    auto pos = name.find(".");
    if (pos != std::string::npos) {
        return {name.substr(0, pos), name.substr(pos + 1)};
    }
    return {"", name};
}

LookupResult RegistryActor::do_lookup(const std::string& name) const {
    auto [manager_id, actor_name] = parse_actor_name(name);

    // ENFORCE QUALIFIED NAMES
    if (manager_id.empty()) {
        if (logging_enabled_) {
            std::cout << "[RegistryActor]   REJECTED: Unqualified name '" << name
                      << "' - must use qualified format 'manager.actor'" << std::endl;
        }
        return LookupResult::not_found(name);
    }

    // Qualified lookup: manager.actor
    auto mgr_it = managers_.find(manager_id);
    if (mgr_it == managers_.end()) {
        return LookupResult::not_found(name);
    }

    // Check if this manager has this actor
    if (mgr_it->second.actors.find(actor_name) != mgr_it->second.actors.end()) {
        return LookupResult::found_at(name, mgr_it->second.endpoint, manager_id);
    }

    return LookupResult::not_found(name);
}

size_t RegistryActor::actor_count() const {
    size_t count = 0;
    for (const auto& [actor, managers] : actor_to_managers_) {
        count += managers.size();
    }
    return count;
}

bool RegistryActor::is_manager_registered(const std::string& manager_id) const {
    return managers_.find(manager_id) != managers_.end();
}

bool RegistryActor::is_actor_registered(const std::string& actor_name) const {
    return actor_to_managers_.find(actor_name) != actor_to_managers_.end();
}

std::string RegistryActor::get_actor_endpoint(const std::string& name) const {
    auto result = do_lookup(name);
    if (result.found && !result.ambiguous) {
        return result.endpoint;
    }
    return "";
}

std::vector<std::string> RegistryActor::get_manager_ids() const {
    std::vector<std::string> ids;
    ids.reserve(managers_.size());
    for (const auto& [id, info] : managers_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> RegistryActor::get_manager_actors(const std::string& manager_id) const {
    auto it = managers_.find(manager_id);
    if (it == managers_.end()) {
        return {};
    }
    return std::vector<std::string>(it->second.actors.begin(), it->second.actors.end());
}

void RegistryActor::handle_list_managers(const std::string& identity, const ListManagers& msg) {
    (void)msg;

    if (logging_enabled_) {
        std::cout << "[RegistryActor] LIST_MANAGERS query from " << identity << std::endl;
    }

    // Build response with all managers and endpoints
    ManagersResponse response;
    for (const auto& [manager_id, info] : managers_) {
        response.manager_ids.push_back(manager_id);
        response.endpoints.push_back(info.endpoint);
    }

    if (logging_enabled_) {
        std::cout << "[RegistryActor]   Returning " << response.manager_ids.size()
                  << " managers" << std::endl;
    }

    auto response_data = MessageSerializer::serialize(MsgType::MANAGERS_RESPONSE, response);
    send_zmq(identity, MsgType::MANAGERS_RESPONSE, response_data);
}

void RegistryActor::handle_list_all_actors(const std::string& identity, const ListAllActors& msg) {
    (void)msg;

    if (logging_enabled_) {
        std::cout << "[RegistryActor] LIST_ALL_ACTORS query from " << identity << std::endl;
    }

    // Build response with all actors and their managers
    AllActorsResponse response;
    for (const auto& [manager_id, info] : managers_) {
        for (const auto& actor_name : info.actors) {
            response.actor_names.push_back(actor_name);
            response.manager_ids.push_back(manager_id);
        }
    }

    if (logging_enabled_) {
        std::cout << "[RegistryActor]   Returning " << response.actor_names.size()
                  << " actors" << std::endl;
    }

    auto response_data = MessageSerializer::serialize(MsgType::ALL_ACTORS_RESPONSE, response);
    send_zmq(identity, MsgType::ALL_ACTORS_RESPONSE, response_data);
}

void RegistryActor::send_zmq(const std::string& identity, MsgType msg_type,
                             const std::vector<uint8_t>& data) {
    // Send via ZmqRouterSender
    zmq_router_ref_->send(new OutgoingZmqMessage(identity, msg_type, data), this);
}

std::string RegistryActor::get_timestamp_str() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::ostringstream oss;
    oss << ms;
    return oss.str();
}

} // namespace actors::registry
