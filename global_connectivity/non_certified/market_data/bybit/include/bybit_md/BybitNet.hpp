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
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>

#include "crypto_common/PollingWs.hpp"
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
  inline std::string https_get(const std::string &host, const std::string &target)
  {
    auto r = crypto_common::https_request(host, "GET", target);
    if (r.status != 200)
      throw std::runtime_error("GET " + host + target + ": HTTP " + std::to_string(r.status) + " " +
                               r.body.substr(0, 200));
    return std::move(r.body);
  }

  // One frame from ws. A silent connection is closed by Bybit (10 minutes on
  // the private stream, sooner on a public one with no subscription), so
  // {"op":"ping"} is written from a timer while the read waits, every
  // `every` after last_ping, not only between frames.
  inline std::string read_with_ping(net::io_context &io, WsStream &ws, beast::flat_buffer &buf,
                                    std::chrono::steady_clock::time_point &last_ping,
                                    std::chrono::steady_clock::duration every)
  {
    static const std::string kPing = R"({"op":"ping"})";
    buf.clear();
    beast::error_code ec;
    bool done = false, timer_pending = false, writing = false;
    net::steady_timer timer(io);
    std::function<void()> arm = [&] {
      timer.expires_at(last_ping + every);
      timer_pending = true;
      timer.async_wait([&](beast::error_code e) {
        timer_pending = false;
        if (e || done)
          return;
        if (!writing)
        {
          writing = true;
          last_ping = std::chrono::steady_clock::now();
          ws.text(true);
          ws.async_write(net::buffer(kPing), [&](beast::error_code, size_t) { writing = false; });
        }
        arm();
      });
    };
    ws.async_read(buf, [&](beast::error_code e, size_t) {
      ec = e;
      done = true;
    });
    arm();
    io.restart();
    while (!done && io.run_one())
      ;
    timer.cancel();
    while ((timer_pending || writing) && io.run_one())
      ;
    if (ec)
      throw beast::system_error(ec);
    return beast::buffers_to_string(buf.data());
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

    // The next frame. The application ping goes out every kPingEvery while
    // the read waits, so a quiet topic does not let the connection lapse.
    std::string read() { return read_with_ping(io_, ws_, buf_, last_ping_, kPingEvery); }

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
