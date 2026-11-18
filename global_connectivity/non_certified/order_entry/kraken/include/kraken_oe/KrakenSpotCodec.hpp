#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Kraken spot WebSocket v2 order messages (feed.md section 4): the add_order
// and cancel_order requests, and the method responses and `executions`
// channel turned into Reports. Pure functions, tested on the documentation's
// examples.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "kraken_oe/KrakenSession.hpp"

namespace kraken::oe::spot
{
  // {"method":"add_order","params":{...,"token":...},"req_id":N}. Quantity and
  // price go out as JSON numbers with exactly the given decimal text.
  std::string add_order(const NewOrder &o, const std::string &token, uint64_t req_id);
  // By venue order id if given, else by client order id.
  std::string cancel_order(const std::string &order_id, const std::string &cl_ord_id, const std::string &token,
                           uint64_t req_id);
  std::string subscribe_executions(const std::string &token, bool snap_orders, bool snap_trades);
  std::string cancel_after(int timeout_s, const std::string &token);

  struct Parsed
  {
    std::vector<Report> reports;
    std::optional<uint64_t> sequence;   // executions `sequence`
    bool subscribed_executions = false; // the executions subscribe succeeded
    std::string subscribe_error;        // a subscribe that failed: its error
  };

  // req_id -> cl_ord_id of the add_order or cancel_order that used it (empty
  // if unknown). Responses to a failed add_order carry no client id.
  using ReqLookup = std::function<std::string(uint64_t)>;

  // One frame (parsed with kraken::parse_raw or nlohmann::json::parse).
  Parsed parse(const nlohmann::json &msg, const ReqLookup &lookup);
}
