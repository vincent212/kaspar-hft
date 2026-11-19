#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The real KrakenSession implementations, and the HTTP transport they use.
//
//   futures_session_factory   Kraken Futures REST v3: sendorder and
//                             cancelorder from a queue; GET /fills and POST
//                             /orders/status polled every poll_ms; optional
//                             cancelallordersafter. Refuses to start unless
//                             Endpoint::check() passes, and refuses every
//                             request to a host other than the configured one.
//   spot_session_factory      Kraken spot WebSocket v2 on ws-auth.kraken.com:
//                             add_order, cancel_order, the executions channel
//                             with order and trade snapshots, optional
//                             cancel_all_orders_after. Production only (no
//                             spot testnet); never run by the agent.
//
// The HTTP function is injectable so that the futures session is tested on
// the documentation's responses without a network (tests/FakeHttp.hpp).

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "kraken_md/Https.hpp"
#include "kraken_oe/KrakenConfig.hpp"
#include "kraken_oe/KrakenSession.hpp"

namespace kraken::oe
{
  using HttpHeaders = std::vector<std::pair<std::string, std::string>>;
  using HttpFn = std::function<kraken::HttpsResponse(const std::string &method, const std::string &host,
                                                     const std::string &target, const HttpHeaders &headers,
                                                     const std::string &body)>;

  HttpFn real_http();

  // Kraken Futures. Fills with a fillTime before `since_ns` (default: when
  // the factory is made) are not reported: they are the account's history,
  // not this process's orders.
  SessionFactory futures_session_factory(const Settings &s, HttpFn http = real_http(), uint64_t since_ns = 0);
  SessionFactory spot_session_factory(const Settings &s, HttpFn http = real_http());

  // The futures request signer, as the session uses it: headers for one
  // request (APIKey, Nonce, Authent; plus Content-Type for a POST).
  HttpHeaders futures_headers(const std::string &api_key, const std::string &secret_b64, const std::string &method,
                              const std::string &endpoint_path, const std::string &post_data,
                              const std::string &nonce);
}
