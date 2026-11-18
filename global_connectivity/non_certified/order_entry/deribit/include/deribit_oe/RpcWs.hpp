#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// A WebSocket client for Deribit's JSON-RPC endpoint (wss://<host>/ws/api/v2)
// that one thread can both poll and write on, and a one-shot HTTPS request.
// Built on sim/WsNet.hpp (Boost.Beast). Used by the order-entry session and
// the post-trade fills session.
//
// Keep-alive is Deribit's own: public/set_heartbeat makes the server send a
// test_request that must be answered with public/test (feed.md 2). Beast's
// idle timeout and protocol pings are off.

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace deribit::oe
{
  using Headers = std::map<std::string, std::string>;

  class RpcWs
  {
  public:
    RpcWs(std::string host, std::string port = "443", std::string target = "/ws/api/v2");
    ~RpcWs();

    void connect();                  // throws on failure
    void send(const std::string &text);   // throws on failure
    // One text frame if one arrives within `wait`; the read stays outstanding
    // for the next call. Throws on failure.
    std::optional<std::string> read_for(std::chrono::milliseconds wait);
    void close() noexcept;
    const std::string &host() const { return host_; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string host_, port_, target_;
  };

  struct HttpResult
  {
    int status = 0;
    std::string body;
  };

  // One HTTPS request on a fresh connection; target includes the query.
  // Throws on a network failure, not on an HTTP error status.
  HttpResult https_request(const std::string &host, const std::string &method, const std::string &target,
                           const std::string &body = {}, const Headers &headers = {});
}
