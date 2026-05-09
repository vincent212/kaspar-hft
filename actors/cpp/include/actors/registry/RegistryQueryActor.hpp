#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/console/ConsoleMessages.hpp"
#include "actors/registry/RegistryActor.hpp"
#include <string>

namespace actors {
namespace registry {

/**
 * RegistryQueryActor - Queries RegistryActor state for monitoring
 *
 * Handles Get queries from ConsoleActor to inspect RegistryActor state.
 * Provides access to manager/actor lists, status, and endpoint lookups.
 *
 * Supported queries:
 * - Get("managers") → List all manager IDs
 * - Get("actors") → List all registered actors
 * - Get("status") → Show registry stats (manager count, actor count)
 * - Get("lookup", {actor: "NAME"}) → Get endpoint for actor
 * - Get("manager", {id: "MANAGER_ID"}) → Get actors for specific manager
 *
 * Example usage via MQ0:
 *   echo "RegistryQuery:managers" | zmq_req tcp://localhost:5557
 *   echo "RegistryQuery:lookup:actor:OB_ES" | zmq_req tcp://localhost:5557
 */
class RegistryQueryActor : public Actor {
public:
    /**
     * Create RegistryQueryActor
     * @param registry Pointer to RegistryActor instance to query
     */
    explicit RegistryQueryActor(RegistryActor* registry);
    virtual ~RegistryQueryActor() = default;

    const char* get_name() const override { return "RegistryQuery"; }

    void init() override;
    void end() override;

private:
    // Message handlers
    void on_get(const frame::cons::msg::Get* m) noexcept;

    // Query handlers
    std::string handle_managers();
    std::string handle_actors();
    std::string handle_status();
    std::string handle_info();
    std::string handle_lookup(const std::map<std::string, std::string>& params);
    std::string handle_manager(const std::map<std::string, std::string>& params);

    RegistryActor* registry_;
};

} // namespace registry
} // namespace actors
