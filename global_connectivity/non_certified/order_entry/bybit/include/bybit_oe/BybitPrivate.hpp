#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Authenticated Bybit V5 transport, shared by the order manager and the
// post-trade client. NOT CERTIFIED; never run against production by the
// agent that wrote it.
//
//   RestClient   signed GET and POST over one keep-alive HTTPS connection
//   PrivateWs    wss://{host}/v5/private: auth, subscribe, read, ping
//   host guard   with testnet = true only Bybit's testnet and demo hosts are
//                accepted; everything else (api.bybit.com, api.bytick.com,
//                regional hosts) is refused before any connection is made

#include <atomic>
#include <chrono>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "bybit_md/BybitNet.hpp"
#include "bybit_oe/BybitAuth.hpp"
#include "sim/WsNet.hpp"

namespace bybit::priv
{
  using namespace sim::wsnet;

  // Bybit's test environments (v5/guide, v5/demo, v5/ws/connect).
  inline bool is_test_host(const std::string &h)
  {
    static const std::set<std::string> ok = {"api-testnet.bybit.com", "api-demo.bybit.com",
                                             "stream-testnet.bybit.com", "stream-demo.bybit.com"};
    return ok.count(h) > 0;
  }

  // Throws unless both hosts are test hosts, when testnet is set.
  inline void guard(bool testnet, const std::string &rest_host, const std::string &ws_host)
  {
    if (!testnet)
      return;
    if (!is_test_host(rest_host))
      throw std::runtime_error("testnet = true but REST host " + rest_host + " is not a Bybit test host");
    if (!is_test_host(ws_host))
      throw std::runtime_error("testnet = true but WebSocket host " + ws_host + " is not a Bybit test host");
  }

  inline uint64_t now_ms() { return now_ns() / 1000000ull; }

  struct Reply
  {
    int http = 0;
    int ret_code = -1;
    std::string ret_msg;
    nlohmann::json result;
    std::string raw;
    bool ok() const { return http == 200 && ret_code == 0; }
  };

  using bnet::read_with_ping;

  class RestClient
  {
  public:
    RestClient(auth::Credentials c, std::string host, bool testnet, int recv_window_ms = 5000)
        : cred_(std::move(c)), host_(std::move(host)), recv_window_(recv_window_ms)
    {
      guard(testnet, host_, testnet ? "stream-testnet.bybit.com" : host_);
    }

    Reply get(const std::string &path, const std::string &query)
    {
      return request(http::verb::get, path + (query.empty() ? "" : "?" + query), query);
    }
    Reply post(const std::string &path, const std::string &body)
    {
      return request(http::verb::post, path, body);
    }

  private:
    auth::Credentials cred_;
    std::string host_;
    int recv_window_;
    net::io_context io_;
    ssl::context tls_{make_tls()};
    std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> s_;

    void open()
    {
      s_ = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(io_, tls_);
      tls_connect(io_, *s_, host_, "443");
    }

    Reply request(http::verb verb, const std::string &target, const std::string &payload)
    {
      for (int attempt = 0; attempt < 2; ++attempt)
      {
        try
        {
          if (!s_)
            open();
          const uint64_t ts = now_ms();
          http::request<http::string_body> req{verb, target, 11};
          req.set(http::field::host, host_);
          req.set(http::field::user_agent, "kaspar-hft");
          req.set("X-BAPI-API-KEY", cred_.api_key);
          req.set("X-BAPI-TIMESTAMP", std::to_string(ts));
          req.set("X-BAPI-RECV-WINDOW", std::to_string(recv_window_));
          req.set("X-BAPI-SIGN", auth::rest_sign(cred_, ts, recv_window_, payload));
          if (verb == http::verb::post)
          {
            req.set(http::field::content_type, "application/json");
            req.body() = payload;
          }
          req.keep_alive(true);
          req.prepare_payload();
          beast::get_lowest_layer(*s_).expires_after(kRestTimeout);
          await(io_, [&](auto h) { http::async_write(*s_, req, std::move(h)); });
          beast::flat_buffer buf;
          http::response_parser<http::string_body> parser;
          parser.body_limit(16 * 1024 * 1024);
          beast::get_lowest_layer(*s_).expires_after(kRestTimeout);
          await(io_, [&](auto h) { http::async_read(*s_, buf, parser, std::move(h)); });
          auto res = parser.release();
          Reply r;
          r.http = res.result_int();
          r.raw = res.body();
          if (!res.keep_alive())
            s_.reset();
          try
          {
            auto j = nlohmann::json::parse(r.raw);
            r.ret_code = j.value("retCode", -1);
            r.ret_msg = j.value("retMsg", std::string());
            if (j.contains("result"))
              r.result = j["result"];
          }
          catch (const std::exception &)
          {
            r.ret_msg = "unparsable response";
          }
          return r;
        }
        catch (const std::exception &e)
        {
          s_.reset(); // a stale keep-alive connection: reopen once
          if (attempt == 1)
          {
            Reply r;
            r.ret_msg = std::string("transport: ") + e.what();
            return r;
          }
        }
      }
      return {};
    }
  };

  // The private stream. Not thread-safe except close(), which may be called
  // from another thread to end a blocked read().
  class PrivateWs
  {
  public:
    static constexpr auto kPingEvery = std::chrono::seconds(20);
    static constexpr auto kIdle = std::chrono::seconds(30);

    PrivateWs(auth::Credentials c, std::string host, bool testnet)
        : cred_(std::move(c)), host_(std::move(host)), ws_(io_, tls_)
    {
      guard(testnet, testnet ? "api-testnet.bybit.com" : host_, host_);
    }

    // Connect, authenticate, subscribe. Returns "" on success, else the reason.
    std::string open(const std::vector<std::string> &topics)
    {
      ws_connect(io_, ws_, host_, "443", "/v5/private", kIdle);
      ws_write(io_, ws_, auth::ws_auth_frame(cred_, now_ms() + 10000));
      for (int i = 0; i < 20; ++i)
      {
        auto j = nlohmann::json::parse(ws_read(io_, ws_, buf_));
        if (j.value("op", std::string()) == "auth")
        {
          if (!j.value("success", false))
            return "auth refused: " + j.value("ret_msg", std::string());
          break;
        }
      }
      nlohmann::json sub = {{"op", "subscribe"}, {"args", topics}};
      ws_write(io_, ws_, sub.dump());
      for (int i = 0; i < 20; ++i)
      {
        auto f = ws_read(io_, ws_, buf_);
        auto j = nlohmann::json::parse(f);
        if (j.value("op", std::string()) == "subscribe")
        {
          if (!j.value("success", false))
            return "subscribe refused: " + j.value("ret_msg", std::string());
          last_ping_ = std::chrono::steady_clock::now();
          return "";
        }
        early_.push_back(std::move(f));
      }
      return "no subscribe answer";
    }

    std::string read()
    {
      if (!early_.empty())
      {
        auto f = std::move(early_.front());
        early_.erase(early_.begin());
        return f;
      }
      return read_with_ping(io_, ws_, buf_, last_ping_, kPingEvery);
    }

    void close()
    {
      net::post(io_, [this] {
        beast::error_code ec;
        beast::get_lowest_layer(ws_).socket().close(ec);
      });
    }

  private:
    auth::Credentials cred_;
    std::string host_;
    net::io_context io_;
    ssl::context tls_{make_tls()};
    WsStream ws_;
    beast::flat_buffer buf_;
    std::vector<std::string> early_;
    std::chrono::steady_clock::time_point last_ping_{};
  };
}
