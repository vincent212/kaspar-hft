#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OKX V5 transport: a WebSocket client that keeps the connection alive the way
// OKX asks, and a one-shot HTTPS request for REST. Shared by the market-data
// feed, the recorder and verify tools, the order manager and the post-trade
// client. The socket and the HTTPS request are crypto_common's PollingWs and
// https_request (market_data/crypto_common, shared with Deribit, Kraken and
// Bybit); this header adds OKX's text ping and keeps Beast out of its
// includers.
//
// OKX closes a WebSocket connection that has had no data pushed for 30 s. The
// client is to send the text "ping" when it has received nothing for N < 30 s
// and expect the text "pong" within N s (OKX V5 docs, "WebSocket / Connect").
// WsClient::read() does this with N = ping_after (20 s by default): it returns
// every frame, "pong" included, and throws when a ping goes unanswered.

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "crypto_common/PollingWs.hpp"

namespace okx
{
  using crypto_common::Headers;
  using crypto_common::HttpResult;
  using crypto_common::https_request;

  class WsClient
  {
  public:
    // host, e.g. "ws.okx.com"; target, e.g. "/ws/v5/public". headers are added
    // to the HTTP upgrade request (the demo-trading header goes here).
    WsClient(std::string host, std::string port, std::string target, Headers headers = {},
             std::chrono::seconds ping_after = std::chrono::seconds(20));
    ~WsClient();

    void connect();                      // throws on failure
    // Sends one text frame; a read in progress stays in progress. Throws on
    // failure.
    void send(const std::string &text);
    // One text frame ("pong" included). Sends "ping" after ping_after of
    // silence and throws if nothing comes back within ping_after.
    std::string read();
    // One text frame if one arrives within `wait`, else nothing; the read
    // stays outstanding for the next call. No keep-alive: a caller that polls
    // with read_for sends "ping" itself. Throws on failure.
    std::optional<std::string> read_for(std::chrono::milliseconds wait);
    void close() noexcept;

    uint64_t pings_sent() const { return pings_; }
    const std::string &host() const { return host_; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string host_, port_, target_;
    Headers headers_;
    std::chrono::seconds ping_after_;
    uint64_t pings_ = 0;
  };

  // Nanoseconds since the epoch, system clock.
  uint64_t now_ns();
}
