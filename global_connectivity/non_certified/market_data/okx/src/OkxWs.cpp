/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_md/OkxWs.hpp"

#include <stdexcept>

#include "crypto_common/PollingWs.hpp"

namespace okx
{
  uint64_t now_ns() { return sim::wsnet::now_ns(); }

  // The shared polling client; this file keeps Beast out of OkxWs.hpp.
  struct WsClient::Impl
  {
    crypto_common::PollingWs ws;
    Impl(const std::string &host, const std::string &port, const std::string &target, const Headers &h)
        : ws(host, port, target, h)
    {
    }
  };

  WsClient::WsClient(std::string host, std::string port, std::string target, Headers headers,
                     std::chrono::seconds ping_after)
      : host_(std::move(host)), port_(std::move(port)), target_(std::move(target)),
        headers_(std::move(headers)), ping_after_(ping_after)
  {
  }

  WsClient::~WsClient() { close(); }

  void WsClient::connect()
  {
    impl_ = std::make_unique<Impl>(host_, port_, target_, headers_);
    impl_->ws.connect();
  }

  void WsClient::send(const std::string &text)
  {
    if (!impl_)
      throw std::runtime_error("WsClient::send: not connected");
    impl_->ws.send(text);
  }

  std::optional<std::string> WsClient::read_for(std::chrono::milliseconds wait)
  {
    if (!impl_)
      throw std::runtime_error("WsClient::read: not connected");
    return impl_->ws.read_for(wait);
  }

  std::string WsClient::read()
  {
    auto f = read_for(std::chrono::duration_cast<std::chrono::milliseconds>(ping_after_));
    if (f)
      return *f;
    send("ping");
    ++pings_;
    f = read_for(std::chrono::duration_cast<std::chrono::milliseconds>(ping_after_));
    if (f)
      return *f;
    throw std::runtime_error("nothing received within " + std::to_string(ping_after_.count()) +
                             " s of a ping");
  }

  void WsClient::close() noexcept
  {
    impl_.reset();
  }
}
