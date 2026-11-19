#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Kraken Futures REST v3 order messages (feed.md section 5): the bodies of
// sendorder and cancelorder, and the responses of sendorder, cancelorder,
// fills and orders/status turned into Reports. Pure functions, tested on the
// documentation's examples.
//
// One source per fact, so nothing is counted twice:
//   - executions come only from GET /fills (fill_id is the exec_id); the
//     EXECUTION events in a sendorder response are not used;
//   - acceptance from the sendorder response (status placed, partiallyFilled,
//     filled) and from orders/status ENTERED_BOOK;
//   - the end of an order from orders/status (CANCELLED, REJECTED,
//     FULLY_EXECUTED) and from the cancelorder response (cancelled).

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "kraken_oe/KrakenSession.hpp"

namespace kraken::oe::futures
{
  // x-www-form-urlencoded; the exact text is what Authent signs.
  // orderType: market -> mkt; post_only -> post; IOC -> ioc; FOK -> fok;
  // else lmt.
  std::string send_order_body(const NewOrder &o);
  std::string cancel_order_body(const std::string &order_id, const std::string &cl_ord_id);
  // orderIds=<id>&orderIds=<id>... and cliOrdIds=... (a POST body)
  std::string orders_status_body(const std::vector<std::string> &order_ids,
                                 const std::vector<std::string> &cl_ord_ids);

  // cl_ord_id: the id the request was sent with (responses carry it only
  // when set, and a rejection may carry no order at all).
  std::vector<Report> parse_send(const nlohmann::json &resp, const std::string &cl_ord_id);
  std::vector<Report> parse_cancel(const nlohmann::json &resp, const std::string &cl_ord_id);
  std::vector<Report> parse_fills(const nlohmann::json &resp);
  std::vector<Report> parse_orders_status(const nlohmann::json &resp);

  // "result":"error": the error text; "" on success.
  std::string error_of(const nlohmann::json &resp);
}
