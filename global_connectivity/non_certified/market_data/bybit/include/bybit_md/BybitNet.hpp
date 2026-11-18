#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The network side of Bybit public market data, shared by BybitFeed, the
// recorder and the verify tool: one public WebSocket per category (TLS, the
// application-level ping Bybit asks for), and HTTPS GETs for the REST book.
// Built on sim/WsNet.hpp (Boost.Beast), like the other crypto feeds.

#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sim/WsNet.hpp"

namespace bybit::bnet
{
  using namespace sim::wsnet;

  struct Hosts
  {
    std::string ws_host = "stream.bybit.com";
    std::string rest_host = "api.bybit.com";
  };

  // GET https://host/target. Throws unless HTTP 200.
  inline std::string https_get(const std::string &host, const std::string &target,
                               std::chrono::seconds timeout = std::chrono::seconds(10))
  {
    net::io_context io;
    auto tls = make_tls();
    beast::ssl_stream<beast::tcp_stream> s(io, tls);
    tls_connect(io, s, host, "443");
    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "kaspar-hft");
    beast::get_lowest_layer(s).expires_after(timeout);
    await(io, [&](auto h) { http::async_write(s, req, std::move(h)); });
    beast::flat_buffer buf;
    http::response_parser<http::string_body> parser;
    parser.body_limit(64 * 1024 * 1024);
    beast::get_lowest_layer(s).expires_after(timeout);
    await(io, [&](auto h) { http::async_read(s, buf, parser, std::move(h)); });
    auto res = parser.release();
    if (res.result() != http::status::ok)
      throw std::runtime_error("GET " + host + target + ": HTTP " + std::to_string(res.result_int()) +
                               " " + res.body().substr(0, 200));
    beast::error_code ec;
    s.shutdown(ec);
    return std::move(res.body());
  }

  // GET /v5/market/full_orderbook. Returns the whole response body; the book
  // is its "result". Throws on HTTP or retCode errors.
  inline std::string full_orderbook(const Hosts &h, const std::string &category,
                                    const std::string &symbol)
  {
    auto body = https_get(h.rest_host,
                          "/v5/market/full_orderbook?category=" + category + "&symbol=" + symbol);
    return body;
  }

  // Raw-frame recorder: one line per frame, "W <recv_ns> <frame>" for a
  // WebSocket frame and "R <recv_ns> <body>" for a REST book. Thread-safe.
  class Recorder
  {
  public:
    explicit Recorder(const std::string &path) : out_(path, std::ios::app) {}
    bool ok() const { return bool(out_); }
    void ws(uint64_t ns, const std::string &frame) { line('W', ns, frame); }
    void rest(uint64_t ns, const std::string &body) { line('R', ns, body); }

  private:
    std::ofstream out_;
    std::mutex mtx_;
    void line(char k, uint64_t ns, const std::string &s)
    {
      std::lock_guard<std::mutex> lk(mtx_);
      out_ << k << ' ' << ns << ' ' << s << '\n';
      out_.flush();
    }
  };

  // One public WebSocket: wss://stream.bybit.com/v5/public/{category}.
  class PublicWs
  {
  public:
    // Bybit recommends a ping every 20 s; a silent connection is closed.
    static constexpr auto kPingEvery = std::chrono::seconds(20);
    static constexpr auto kIdle = std::chrono::seconds(30);

    PublicWs(const Hosts &h, const std::string &category)
        : hosts_(h), category_(category), tls_(make_tls()), ws_(io_, tls_)
    {
    }

    void connect(const std::vector<std::string> &topics)
    {
      ws_connect(io_, ws_, hosts_.ws_host, "443", "/v5/public/" + category_, kIdle);
      nlohmann::json sub = {{"op", "subscribe"}, {"args", topics}};
      ws_write(io_, ws_, sub.dump());
      last_ping_ = std::chrono::steady_clock::now();
    }

    // The next frame. Sends the application ping when it is due.
    std::string read()
    {
      auto now = std::chrono::steady_clock::now();
      if (now - last_ping_ >= kPingEvery)
      {
        ws_write(io_, ws_, R"({"op":"ping"})");
        last_ping_ = now;
      }
      return ws_read(io_, ws_, buf_);
    }

    void close()
    {
      beast::error_code ec;
      ws_.close(websocket::close_code::normal, ec);
    }

  private:
    Hosts hosts_;
    std::string category_;
    net::io_context io_;
    ssl::context tls_;
    WsStream ws_;
    beast::flat_buffer buf_;
    std::chrono::steady_clock::time_point last_ping_{};
  };
}
