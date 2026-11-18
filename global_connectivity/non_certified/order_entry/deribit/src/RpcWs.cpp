/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_oe/RpcWs.hpp"

#include <stdexcept>

#include "sim/WsNet.hpp"

namespace deribit::oe
{
  using namespace sim::wsnet;

  // One io_context, used only by the thread that calls RpcWs. A read is
  // started once and stays outstanding until it completes, so read_for() can
  // poll and send() can write while it is pending (Beast allows one read and
  // one write in flight on one stream).
  struct RpcWs::Impl
  {
    net::io_context io;
    ssl::context tls = make_tls();
    WsStream ws{io, tls};
    beast::flat_buffer buf;
    bool reading = false, rdone = false;
    beast::error_code rec;

    void start_read()
    {
      if (reading || rdone)
        return;
      buf.clear();
      reading = true;
      ws.async_read(buf, [this](beast::error_code e, size_t) {
        rec = e;
        rdone = true;
        reading = false;
      });
    }

    std::string take()
    {
      rdone = false;
      if (rec)
        throw beast::system_error(rec);
      return beast::buffers_to_string(buf.data());
    }
  };

  RpcWs::RpcWs(std::string host, std::string port, std::string target)
      : host_(std::move(host)), port_(std::move(port)), target_(std::move(target))
  {
  }

  RpcWs::~RpcWs() { close(); }

  void RpcWs::connect()
  {
    impl_ = std::make_unique<Impl>();
    auto &ws = impl_->ws;
    tls_connect(impl_->io, ws.next_layer(), host_, port_);
    beast::get_lowest_layer(ws).expires_never();
    auto to = websocket::stream_base::timeout::suggested(beast::role_type::client);
    to.idle_timeout = websocket::stream_base::none();
    to.keep_alive_pings = false;
    ws.set_option(to);
    ws.set_option(websocket::stream_base::decorator(
        [](websocket::request_type &req) { req.set(http::field::user_agent, "kaspar-hft"); }));
    await(impl_->io, [&](auto cb) { ws.async_handshake(host_ + ":" + port_, target_, std::move(cb)); });
  }

  void RpcWs::send(const std::string &text)
  {
    if (!impl_)
      throw std::runtime_error("RpcWs::send: not connected");
    auto &ws = impl_->ws;
    bool done = false;
    beast::error_code ec;
    ws.text(true);
    ws.async_write(net::buffer(text), [&](beast::error_code e, size_t) {
      ec = e;
      done = true;
    });
    impl_->io.restart();
    while (!done)
      impl_->io.run_one();
    if (ec)
      throw beast::system_error(ec);
  }

  std::optional<std::string> RpcWs::read_for(std::chrono::milliseconds wait)
  {
    if (!impl_)
      throw std::runtime_error("RpcWs::read_for: not connected");
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

  void RpcWs::close() noexcept
  {
    if (!impl_)
      return;
    try
    {
      beast::get_lowest_layer(impl_->ws).cancel();
      beast::error_code ec;
      beast::get_lowest_layer(impl_->ws).expires_after(std::chrono::seconds(2));
      if (!impl_->reading)
        impl_->ws.close(websocket::close_code::normal, ec);
    }
    catch (...)
    {
    }
    impl_.reset();
  }

  HttpResult https_request(const std::string &host, const std::string &method, const std::string &target,
                           const std::string &body, const Headers &headers)
  {
    net::io_context io;
    auto tls = make_tls();
    beast::ssl_stream<beast::tcp_stream> s(io, tls);
    tls_connect(io, s, host, "443");
    http::request<http::string_body> req{method == "POST" ? http::verb::post : http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "kaspar-hft");
    for (const auto &[k, v] : headers)
      req.set(k, v);
    if (!body.empty())
    {
      req.set(http::field::content_type, "application/json");
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
    s.shutdown(ec);
    return {int(res.result_int()), std::move(res.body())};
  }
}
