/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/console/ConsoleActor.hpp"
#include "actors/act/Manager.hpp"
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace frame {
namespace cons {

ConsoleActor::ConsoleActor(actors::Manager* manager)
    : manager_(manager)
{
    MESSAGE_HANDLER(msg::Get, on_get);
    MESSAGE_HANDLER(msg::Cmd, on_cmd);
}

void ConsoleActor::init() {
    // Initialization if needed
}

void ConsoleActor::end() {
    // Cleanup if needed
}

void ConsoleActor::on_get(const msg::Get* m) noexcept {
    std::string response = handle_get_sync(*m);
    reply(new msg::Page(response));
}

void ConsoleActor::on_cmd(const msg::Cmd* m) noexcept {
    // Parse command buffer
    std::string cmd_buf = m->buf;

    // Trim whitespace
    cmd_buf.erase(0, cmd_buf.find_first_not_of(" \t\n\r"));
    cmd_buf.erase(cmd_buf.find_last_not_of(" \t\n\r") + 1);

    if (cmd_buf.empty()) {
        reply(new msg::CmdR("ERROR: Empty command"));
        return;
    }

    try {
        // Detect format: JSON (starts with '{') or colon-delimited
        std::string response;

        if (cmd_buf[0] == '{') {
            // JSON format
            response = handle_json_command(cmd_buf);
        } else {
            // Colon-delimited format (legacy Cons.hpp style)
            response = handle_colon_command(cmd_buf);
        }

        reply(new msg::CmdR(response));
    } catch (const std::exception& e) {
        reply(new msg::CmdR("ERROR: " + std::string(e.what())));
    }
}

std::string ConsoleActor::handle_get_sync(const msg::Get& msg) {
    const std::string& what = msg.what;

    if (what == "actors") {
        return get_actors();
    } else if (what == "qlen") {
        return get_queue_lengths();
    } else if (what == "msg") {
        return get_message_counts();
    } else if (what == "help") {
        return get_help();
    } else {
        return "ERROR: Unknown query '" + what + "'\nTry 'help' for available commands";
    }
}

std::string ConsoleActor::get_actors() {
    Table table("Actors", {"Name", "Type", "Queue Size"});

    auto& name_map = manager_->get_name_map();
    for (const auto& [name, actor] : name_map) {
        table.add_row();
        table.set_value(name);
        table.set_value(typeid(*actor).name());  // C++ type name
        table.set_value(static_cast<int>(actor->queue_length()));
    }

    return table.to_json(true);
}

std::string ConsoleActor::get_queue_lengths() {
    Table table("Queue Lengths", {"Actor", "Queue Size"});

    auto queue_lengths = manager_->get_queue_lengths();
    for (const auto& [name, qlen] : queue_lengths) {
        if (qlen > 0) {  // Only show non-empty queues
            table.add_row();
            table.set_value(name);
            table.set_value(static_cast<int>(qlen));
        }
    }

    return table.to_json(true);
}

std::string ConsoleActor::get_message_counts() {
    Table table("Message Counts", {"Actor", "TID", "Message Count"});

    auto msg_counts = manager_->get_message_counts();
    for (const auto& [name, tid_cnt] : msg_counts) {
        const auto& [tid, cnt] = tid_cnt;
        table.add_row();
        table.set_value(name);
        table.set_value(static_cast<int>(tid));
        table.set_value(cnt);
    }

    return table.to_json(true);
}

std::string ConsoleActor::get_help() {
    std::ostringstream oss;
    oss << "C++ Actor Framework Console\n"
        << "===============================\n"
        << "\n"
        << "Built-in Commands:\n"
        << "  actors      - List all managed actors\n"
        << "  qlen        - Show queue lengths for actors\n"
        << "  msg         - Show message counts by actor\n"
        << "  help        - Show this help message\n"
        << "\n"
        << "Custom Queries (via Get message):\n"
        << "  Send Get(what, kv) to query specific actors.\n"
        << "  Actors respond with Page(table.to_json())\n"
        << "\n"
        << "Example MQ0 Commands:\n"
        << "  get actors\n"
        << "  get qlen\n"
        << "  get msg\n"
        << "  get status         (forwarded to actor if implemented)\n"
        << "  get positions      (forwarded to actor if implemented)\n";

    return oss.str();
}

// Helper: Tokenize string by delimiter
std::vector<std::string> ConsoleActor::tokenize(const std::string& str, const std::string& delim) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delim);

    while (end != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delim.length();
        end = str.find(delim, start);
    }

    tokens.push_back(str.substr(start));
    return tokens;
}

// Handle colon-delimited commands (legacy Cons.hpp format)
// Format: ActorName:what:key1:val1:key2:val2
// Special: "list" shows all managed actors
std::string ConsoleActor::handle_colon_command(const std::string& cmd_buf) {
    auto tokens = tokenize(cmd_buf, ":");

    if (tokens.empty()) {
        return "ERROR: Empty command";
    }

    // Special built-in: "list"
    if (tokens.size() == 1 && tokens[0] == "list") {
        return get_actors();
    }

    // Must have odd number of tokens: actor:what:k1:v1:k2:v2
    if (tokens.size() % 2 != 0) {
        return "ERROR: Invalid format - expected actor:what:key1:val1:key2:val2";
    }

    // Extract actor name, what, and key-value pairs
    std::string actor_name = tokens[0];
    std::string what = tokens.size() > 1 ? tokens[1] : "";

    std::map<std::string, std::string> kv;
    for (size_t i = 2; i < tokens.size(); i += 2) {
        kv[tokens[i]] = tokens[i + 1];
    }

    // Route to actor
    return route_to_actor(actor_name, what, kv);
}

// Handle JSON commands
// Format: {"actor": "Name", "what": "command", "params": {"k1": "v1"}}
// Or: {"command": "list"}
std::string ConsoleActor::handle_json_command(const std::string& cmd_buf) {
    json j = json::parse(cmd_buf);

    // Special built-in: {"command": "list"}
    if (j.contains("command")) {
        std::string cmd = j["command"];
        if (cmd == "list") {
            return get_actors();
        } else if (cmd == "help") {
            return get_help();
        } else {
            return "ERROR: Unknown command '" + cmd + "'";
        }
    }

    // Route to actor: {"actor": "Name", "what": "command", "params": {...}}
    if (!j.contains("actor")) {
        return "ERROR: JSON must have 'actor' or 'command' field";
    }

    std::string actor_name = j["actor"];
    std::string what = j.contains("what") ? j["what"].get<std::string>() : "";

    std::map<std::string, std::string> kv;
    if (j.contains("params") && j["params"].is_object()) {
        for (auto& [key, val] : j["params"].items()) {
            kv[key] = val.is_string() ? val.get<std::string>() : val.dump();
        }
    }

    return route_to_actor(actor_name, what, kv);
}

// Route a query to a specific actor
std::string ConsoleActor::route_to_actor(const std::string& actor_name,
                                          const std::string& what,
                                          const std::map<std::string, std::string>& kv) {
    try {
        auto actor_ref = manager_->get_actor_by_name(actor_name);
        if (!actor_ref) {
            return "ERROR: Actor '" + actor_name + "' not found";
        }

        msg::Get get_msg(what, kv);
        auto reply_ptr = actor_ref.fast_send(&get_msg, this);

        if (!reply_ptr) {
            return "ERROR: Actor '" + actor_name + "' did not reply";
        }

        auto page = dynamic_cast<const msg::Page*>(reply_ptr.get());
        if (!page) {
            return "ERROR: Actor '" + actor_name + "' returned unexpected message type";
        }

        return page->val;

    } catch (const std::exception& e) {
        return "ERROR: " + std::string(e.what());
    }
}

msg::Get ConsoleActor::parse_command(const std::string& cmd_buf) {
    // Deprecated - kept for compatibility
    // Now using handle_colon_command and handle_json_command
    std::istringstream iss(cmd_buf);
    std::string cmd;
    iss >> cmd;

    // Convert to lowercase for comparison
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    // Direct commands (actors, qlen, msg, help)
    if (cmd == "actors" || cmd == "qlen" || cmd == "msg" || cmd == "help") {
        return msg::Get(cmd);
    }

    // "get <what> [key=value ...]" format
    if (cmd == "get") {
        std::string what;
        iss >> what;

        if (what.empty()) {
            return msg::Get("help");  // Default to help
        }

        // Parse key=value pairs
        std::map<std::string, std::string> kv;
        std::string param;
        while (iss >> param) {
            auto eq_pos = param.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = param.substr(0, eq_pos);
                std::string val = param.substr(eq_pos + 1);
                kv[key] = val;
            }
        }

        return msg::Get(what, kv);
    }

    // Unknown command
    return msg::Get("help");
}

} // namespace cons
} // namespace frame
