#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The WebSocket client for Deribit's JSON-RPC endpoint (wss://<host>/ws/api/v2)
// and the one-shot HTTPS request: crypto_common's PollingWs and
// https_request (market_data/crypto_common), with Deribit's port and path as
// defaults. Used by the order-entry session and the post-trade fills session.
//
// Keep-alive is Deribit's own: public/set_heartbeat makes the server send a
// test_request that must be answered with public/test (feed.md 2). Beast's
// idle timeout and protocol pings are off.

#include <string>

#include "crypto_common/PollingWs.hpp"

namespace deribit::oe
{
  using crypto_common::Headers;
  using crypto_common::HttpResult;
  using crypto_common::https_request;

  class RpcWs : public crypto_common::PollingWs
  {
  public:
    explicit RpcWs(std::string host, std::string port = "443", std::string target = "/ws/api/v2")
        : PollingWs(std::move(host), std::move(port), std::move(target))
    {
    }
  };
}
