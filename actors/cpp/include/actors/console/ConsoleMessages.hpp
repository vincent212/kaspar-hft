#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <map>
#include "actors/Message.hpp"

namespace frame {
namespace cons {
namespace msg {

/**
 * Get - Query message requesting information from an actor
 *
 * Matches Python Get message and C++ MTD frame::cons::msg::Get
 *
 * Example:
 *   Get status_query("status");
 *   Get pnl_query("pnl", {{"owner", "SIMULATOR"}, {"date", "2024-01-01"}});
 */
struct Get : public actors::Message_N<200> {
    std::string what;                          // Query type: "status", "pnl", "positions", etc.
    std::map<std::string, std::string> kv;     // Optional key-value parameters

    // Constructors
    explicit Get(const std::string& w)
        : what(w)
    {}

    Get(const std::string& w, const std::map<std::string, std::string>& params)
        : what(w)
        , kv(params)
    {}
};

/**
 * Page - Response message containing query results
 *
 * Matches Python Page message and C++ MTD frame::cons::msg::Page
 *
 * The value can be:
 * - JSON string (modern format)
 * - Tab-delimited string (legacy format)
 * - Plain text
 *
 * Example:
 *   Page response(table.to_json());
 *   Page error("ERROR: Not found");
 */
struct Page : public actors::Message_N<201> {
    std::string val;  // Response value (JSON, tab-delimited, or text)

    explicit Page(const std::string& v)
        : val(v)
    {}
};

/**
 * Cmd - Command message for console operations
 *
 * Matches Python Cmd message and C++ MTD frame::cons::msg::Cmd
 *
 * Used by MQ0ServerActor to forward commands to ConsoleActor.
 *
 * Example:
 *   Cmd command("get actors");
 *   Cmd query("get pnl owner=SIMULATOR");
 */
struct Cmd : public actors::Message_N<202> {
    std::string buf;  // Command buffer (e.g., "get pnl owner=SIMULATOR")

    explicit Cmd(const std::string& b)
        : buf(b)
    {}
};

/**
 * CmdR - Command response message
 *
 * Matches Python CmdR message and C++ MTD frame::cons::msg::CmdR
 *
 * Returned by ConsoleActor after processing a Cmd.
 *
 * Example:
 *   CmdR response(json_result);
 */
struct CmdR : public actors::Message_N<203> {
    std::string res;  // Response result

    explicit CmdR(const std::string& r)
        : res(r)
    {}
};

} // namespace msg
} // namespace cons
} // namespace frame
