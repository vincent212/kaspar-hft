/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/registry/RegistryQueryActor.hpp"
#include <sstream>
#include <iostream>

namespace actors {
namespace registry {

RegistryQueryActor::RegistryQueryActor(RegistryActor* registry)
    : registry_(registry)
{
    MESSAGE_HANDLER(frame::cons::msg::Get, on_get);
}

void RegistryQueryActor::init() {
    std::cout << "RegistryQueryActor: Initialized" << std::endl;
}

void RegistryQueryActor::end() {
    std::cout << "RegistryQueryActor: Shutdown" << std::endl;
}

void RegistryQueryActor::on_get(const frame::cons::msg::Get* m) noexcept {
    std::string response;

    try {
        const std::string& what = m->what;

        if (what == "managers") {
            response = handle_managers();
        } else if (what == "actors") {
            response = handle_actors();
        } else if (what == "status") {
            response = handle_status();
        } else if (what == "info") {
            response = handle_info();
        } else if (what == "lookup") {
            response = handle_lookup(m->kv);
        } else if (what == "manager") {
            response = handle_manager(m->kv);
        } else {
            response = "{\"error\": \"Unknown query: " + what + "\", \"available\": [\"managers\", \"actors\", \"status\", \"info\", \"lookup\", \"manager\"]}";
        }
    } catch (const std::exception& e) {
        response = "{\"error\": \"" + std::string(e.what()) + "\"}";
    }

    reply(new frame::cons::msg::Page(response));
}

std::string RegistryQueryActor::handle_managers() {
    auto manager_ids = registry_->get_manager_ids();

    std::ostringstream oss;
    oss << "{\"managers\": [";
    for (size_t i = 0; i < manager_ids.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "\"" << manager_ids[i] << "\"";
    }
    oss << "], \"count\": " << manager_ids.size() << "}";

    return oss.str();
}

std::string RegistryQueryActor::handle_actors() {
    auto manager_ids = registry_->get_manager_ids();

    std::ostringstream oss;
    oss << "{\"actors\": [";

    bool first = true;
    size_t total_actors = 0;

    for (const auto& mgr_id : manager_ids) {
        auto actors = registry_->get_manager_actors(mgr_id);
        for (const auto& actor_name : actors) {
            if (!first) oss << ", ";
            oss << "{\"name\": \"" << actor_name << "\", \"manager\": \"" << mgr_id << "\"}";
            first = false;
            ++total_actors;
        }
    }

    oss << "], \"count\": " << total_actors << "}";

    return oss.str();
}

std::string RegistryQueryActor::handle_status() {
    size_t mgr_count = registry_->manager_count();
    size_t actor_count = registry_->actor_count();

    std::ostringstream oss;
    oss << "{"
        << "\"managers\": " << mgr_count << ", "
        << "\"actors\": " << actor_count << ", "
        << "\"running\": " << (registry_->is_running() ? "true" : "false")
        << "}";

    return oss.str();
}

std::string RegistryQueryActor::handle_info() {
    auto manager_ids = registry_->get_manager_ids();
    size_t total_actors = 0;

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"registry_running\": " << (registry_->is_running() ? "true" : "false") << ",\n";
    oss << "  \"manager_count\": " << manager_ids.size() << ",\n";
    oss << "  \"managers\": [\n";

    for (size_t i = 0; i < manager_ids.size(); ++i) {
        const auto& mgr_id = manager_ids[i];
        auto actors = registry_->get_manager_actors(mgr_id);
        total_actors += actors.size();

        if (i > 0) oss << ",\n";
        oss << "    {\n";
        oss << "      \"id\": \"" << mgr_id << "\",\n";
        oss << "      \"actor_count\": " << actors.size() << ",\n";
        oss << "      \"actors\": [";

        for (size_t j = 0; j < actors.size(); ++j) {
            if (j > 0) oss << ", ";
            oss << "\"" << actors[j] << "\"";
        }

        oss << "]\n";
        oss << "    }";
    }

    oss << "\n  ],\n";
    oss << "  \"total_actors\": " << total_actors << "\n";
    oss << "}";

    return oss.str();
}

std::string RegistryQueryActor::handle_lookup(const std::map<std::string, std::string>& params) {
    auto it = params.find("actor");
    if (it == params.end()) {
        return "{\"error\": \"Missing 'actor' parameter\"}";
    }

    std::string actor_name = it->second;
    std::string endpoint = registry_->get_actor_endpoint(actor_name);

    std::ostringstream oss;
    if (endpoint.empty()) {
        oss << "{\"actor\": \"" << actor_name << "\", \"found\": false}";
    } else {
        oss << "{\"actor\": \"" << actor_name << "\", \"found\": true, \"endpoint\": \"" << endpoint << "\"}";
    }

    return oss.str();
}

std::string RegistryQueryActor::handle_manager(const std::map<std::string, std::string>& params) {
    auto it = params.find("id");
    if (it == params.end()) {
        return "{\"error\": \"Missing 'id' parameter\"}";
    }

    std::string manager_id = it->second;

    if (!registry_->is_manager_registered(manager_id)) {
        return "{\"manager\": \"" + manager_id + "\", \"found\": false}";
    }

    auto actors = registry_->get_manager_actors(manager_id);

    std::ostringstream oss;
    oss << "{\"manager\": \"" << manager_id << "\", \"found\": true, \"actors\": [";
    for (size_t i = 0; i < actors.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "\"" << actors[i] << "\"";
    }
    oss << "], \"count\": " << actors.size() << "}";

    return oss.str();
}

} // namespace registry
} // namespace actors
