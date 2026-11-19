#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The transport the crypto venues share, on sim/WsNet.hpp (Boost.Beast over
// OpenSSL):
//
//   PollingWs       one TLS WebSocket owned by one thread, which both polls it
//                   (read_for) and writes on it (send). A read is started once
//                   and stays outstanding until a frame completes it, so a
//                   send can be made while it is pending (Beast allows one
//                   read and one write in flight on one stream). Keep-alive is
//                   the caller's: Beast's idle timeout and protocol pings are
//                   off, because each venue has its own application ping.
//   https_request   one HTTPS request on a fresh connection.
//
// Used by the OKX WebSocket client (okx_md/OkxWs.hpp wraps it and adds OKX's
// text ping), the Deribit JSON-RPC client (deribit_oe/RpcWs.hpp), the Kraken
// spot sessions, and every REST call the order-entry and post-trade clients
// make.

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "sim/WsNet.hpp"

namespace crypto_common
{
  using Headers = std::map<std::string, std::string>;

  class PollingWs
  {
  public:
    // host, e.g. "ws.okx.com"; target, e.g. "/ws/v5/public". headers are added
    // to the HTTP upgrade request.
    PollingWs(std::string host, std::string port, std::string target, Headers headers = {})
        : host_(std::move(host)), port_(std::move(port)), target_(std::move(target)),
          headers_(std::move(headers))
    {
    }
    ~PollingWs() { close(); }
    PollingWs(const PollingWs &) = delete;
    PollingWs &operator=(const PollingWs &) = delete;

    // Throws on failure.
    void connect()
    {
      namespace wn = sim::wsnet;
      impl_ = std::make_unique<Impl>();
      auto &ws = impl_->ws;
      ws.read_message_max(kMaxMessage);
      wn::tls_connect(impl_->io, ws.next_layer(), host_, port_);
      wn::beast::get_lowest_layer(ws).expires_never();
      auto to = wn::websocket::stream_base::timeout::suggested(wn::beast::role_type::client);
      to.idle_timeout = wn::websocket::stream_base::none();
      to.keep_alive_pings = false;
      ws.set_option(to);
      const Headers h = headers_;
      ws.set_option(wn::websocket::stream_base::decorator([h](wn::websocket::request_type &req) {
        req.set(wn::http::field::user_agent, "kaspar-hft");
        for (const auto &[k, v] : h)
          req.set(k, v);
      }));
      wn::await(impl_->io, [&](auto cb) { ws.async_handshake(host_ + ":" + port_, target_, std::move(cb)); });
    }

    // Sends one text frame; a read in progress stays in progress. Throws on
    // failure.
    void send(const std::string &text)
    {
      namespace wn = sim::wsnet;
      if (!impl_)
        throw std::runtime_error("PollingWs::send: not connected");
      auto &ws = impl_->ws;
      bool done = false;
      wn::beast::error_code ec;
      ws.text(true);
      ws.async_write(wn::net::buffer(text), [&](wn::beast::error_code e, size_t) {
        ec = e;
        done = true;
      });
      impl_->io.restart();
      while (!done)
        impl_->io.run_one();
      if (ec)
        throw wn::beast::system_error(ec);
    }

    // One text frame if one arrives within `wait`, else nothing; the read
    // stays outstanding for the next call. Throws on failure.
    std::optional<std::string> read_for(std::chrono::milliseconds wait)
    {
      if (!impl_)
        throw std::runtime_error("PollingWs::read_for: not connected");
      impl_->start_read();
      const auto deadline = std::chrono::steady_clock::now() + wait;
      impl_->io.restart();
      while (!impl_->rdone && std::chrono::steady_clock::now() < deadline)
      {
        impl_->io.run_one_until(deadline);
        if (impl_->io.stopped())
          impl_->io.restart();
      }
      if (!impl_->rdone)
        return std::nullopt;
      return impl_->take();
    }

    void close() noexcept
    {
      namespace wn = sim::wsnet;
      if (!impl_)
        return;
      try
      {
        wn::beast::get_lowest_layer(impl_->ws).cancel();
        wn::beast::error_code ec;
        wn::beast::get_lowest_layer(impl_->ws).expires_after(std::chrono::seconds(2));
        if (!impl_->reading)
          impl_->ws.close(wn::websocket::close_code::normal, ec);
      }
      catch (...)
      {
      }
      impl_.reset();
    }

    const std::string &host() const { return host_; }

  private:
    static constexpr size_t kMaxMessage = 16 * 1024 * 1024;

    struct Impl
    {
      sim::wsnet::net::io_context io;
      sim::wsnet::ssl::context tls = sim::wsnet::make_tls();
      sim::wsnet::WsStream ws{io, tls};
      sim::wsnet::beast::flat_buffer buf;
      bool reading = false, rdone = false;
      sim::wsnet::beast::error_code rec;

      void start_read()
      {
        if (reading || rdone)
          return;
        buf.clear();
        reading = true;
        ws.async_read(buf, [this](sim::wsnet::beast::error_code e, size_t) {
          rec = e;
          rdone = true;
          reading = false;
        });
      }

      std::string take()
      {
        rdone = false;
        if (rec)
          throw sim::wsnet::beast::system_error(rec);
        return sim::wsnet::beast::buffers_to_string(buf.data());
      }
    };

    std::unique_ptr<Impl> impl_;
    std::string host_, port_, target_;
    Headers headers_;
  };

  struct HttpResult
  {
    int status = 0;
    std::string body;
  };

  // One HTTPS request on a fresh connection. method "GET" or "POST"; target
  // includes the query string. A body is sent as application/json unless the
  // headers name a Content-Type. Throws on a network failure, not on an HTTP
  // error status.
  inline HttpResult https_request(const std::string &host, const std::string &method, const std::string &target,
                                  const std::string &body = {}, const Headers &headers = {})
  {
    namespace wn = sim::wsnet;
    wn::net::io_context io;
    auto tls = wn::make_tls();
    wn::beast::ssl_stream<wn::beast::tcp_stream> s(io, tls);
    wn::tls_connect(io, s, host, "443");

    wn::http::request<wn::http::string_body> req{wn::http::string_to_verb(method), target, 11};
    req.set(wn::http::field::host, host);
    req.set(wn::http::field::user_agent, "kaspar-hft");
    for (const auto &[k, v] : headers)
      req.set(k, v);
    if (!body.empty() || method == "POST")
    {
      if (!body.empty() && headers.find("Content-Type") == headers.end())
        req.set(wn::http::field::content_type, "application/json");
      req.body() = body;
      req.prepare_payload();
    }
    wn::beast::get_lowest_layer(s).expires_after(wn::kRestTimeout);
    wn::await(io, [&](auto h) { wn::http::async_write(s, req, std::move(h)); });

    wn::beast::flat_buffer buf;
    wn::http::response_parser<wn::http::string_body> parser;
    parser.body_limit(64 * 1024 * 1024);
    wn::beast::get_lowest_layer(s).expires_after(wn::kRestTimeout);
    wn::await(io, [&](auto h) { wn::http::async_read(s, buf, parser, std::move(h)); });
    auto res = parser.release();
    wn::beast::error_code ec;
    s.shutdown(ec); // a missing TLS close_notify is not an error here
    return {int(res.result_int()), std::move(res.body())};
  }
}
