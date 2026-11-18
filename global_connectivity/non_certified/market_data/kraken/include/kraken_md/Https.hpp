#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// One HTTPS request over Boost.Beast, on sim/WsNet.hpp's TLS plumbing (peer
// and host name verified, 10 s deadline). Used by the verify tool for public
// REST snapshots and by the order-entry and post-trade REST clients.

#include <string>
#include <utility>
#include <vector>

#include "sim/WsNet.hpp"

namespace kraken
{
  struct HttpsResponse
  {
    int status = 0;
    std::string body;
  };

  inline HttpsResponse https_request(const std::string &method, const std::string &host, const std::string &target,
                                     const std::vector<std::pair<std::string, std::string>> &headers = {},
                                     const std::string &body = {})
  {
    using namespace sim::wsnet;
    net::io_context io;
    auto tls = make_tls();
    beast::ssl_stream<beast::tcp_stream> s(io, tls);
    tls_connect(io, s, host, "443");
    http::request<http::string_body> req{http::string_to_verb(method), target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "kaspar-hft");
    for (const auto &[k, v] : headers)
      req.set(k, v);
    if (!body.empty() || method == "POST")
    {
      req.body() = body;
      req.prepare_payload();
    }
    beast::get_lowest_layer(s).expires_after(kRestTimeout);
    await(io, [&](auto h) { http::async_write(s, req, std::move(h)); });
    beast::flat_buffer buf;
    http::response_parser<http::string_body> parser;
    parser.body_limit(64 * 1024 * 1024);
    await(io, [&](auto h) { http::async_read(s, buf, parser, std::move(h)); });
    auto res = parser.release();
    beast::error_code ec;
    s.shutdown(ec); // a missing TLS close_notify is not an error
    return {int(res.result_int()), std::move(res.body())};
  }
}
