#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// JSON-RPC requests the public Deribit market-data connection sends, and the
// heartbeat answer. Shared by DeribitFeed and the live check (feed.md 2).

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace deribit::ws
{
  constexpr const char *kProdHost = "www.deribit.com";
  constexpr const char *kTestHost = "test.deribit.com";
  constexpr const char *kPath = "/ws/api/v2";

  inline nlohmann::json request(int64_t id, const std::string &method, nlohmann::json params)
  {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
  }

  // public/subscribe for the book and trades of one instrument. Without
  // authentication only the aggregated intervals are allowed: `raw` is
  // "only available to authorized users" (feed.md 2).
  inline std::vector<std::string> channels(const std::string &instrument, const std::string &interval)
  {
    return {"book." + instrument + "." + interval, "trades." + instrument + "." + interval};
  }

  inline nlohmann::json subscribe(int64_t id, const std::vector<std::string> &chans)
  {
    return request(id, "public/subscribe", {{"channels", chans}});
  }

  // The server then sends {"method":"heartbeat","params":{"type":"test_request"}}
  // every `seconds`, and closes the connection if it is not answered.
  inline nlohmann::json set_heartbeat(int64_t id, int seconds)
  {
    return request(id, "public/set_heartbeat", {{"interval", seconds}});
  }

  // The answer to a test_request (public/test), or null for anything else.
  inline nlohmann::json heartbeat_reply(const nlohmann::json &msg, int64_t id)
  {
    if (msg.value("method", std::string()) != "heartbeat")
      return nullptr;
    const auto p = msg.find("params");
    if (p == msg.end() || p->value("type", std::string()) != "test_request")
      return nullptr;
    return request(id, "public/test", nlohmann::json::object());
  }
}
