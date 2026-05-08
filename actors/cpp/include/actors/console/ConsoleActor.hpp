#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/console/ConsoleMessages.hpp"
#include "actors/console/Table.hpp"
#include <memory>

namespace actors {
    class Manager;
}

namespace frame {
namespace cons {

/**
 * ConsoleActor - Central monitoring and query handler
 *
 * Part of the C++ actor framework. Routes Get queries to actors and
 * provides built-in monitoring commands.
 *
 * Matches Python ConsoleActor API.
 *
 * Built-in queries:
 * - actors   : List all managed actors
 * - qlen     : Show queue lengths
 * - msg      : Show message counts
 * - help     : Show available commands
 *
 * Example:
 *   auto console = new ConsoleActor(manager);
 *   manager->manage("Console", console);
 *
 *   // Query from another actor
 *   msg::Get query("actors");
 *   console->send(&query, this);
 */
class ConsoleActor : public actors::Actor {
public:
    explicit ConsoleActor(actors::Manager* manager);
    virtual ~ConsoleActor() = default;

    const char* get_name() const override { return "Console"; }

    void init() override;
    void end() override;

private:
    // Message handlers
    void on_get(const msg::Get* m) noexcept;
    void on_cmd(const msg::Cmd* m) noexcept;

    // Synchronous query handling
    std::string handle_get_sync(const msg::Get& msg);

    // Built-in queries
    std::string get_actors();
    std::string get_queue_lengths();
    std::string get_message_counts();
    std::string get_help();

    // Command parsing (supports both formats)
    std::string handle_colon_command(const std::string& cmd_buf);
    std::string handle_json_command(const std::string& cmd_buf);
    std::string route_to_actor(const std::string& actor_name,
                                const std::string& what,
                                const std::map<std::string, std::string>& kv);

    // Helper methods
    std::vector<std::string> tokenize(const std::string& str, const std::string& delim);

    // Parse command buffer (deprecated)
    msg::Get parse_command(const std::string& cmd_buf);

    actors::Manager* manager_;
};

} // namespace cons
} // namespace frame
