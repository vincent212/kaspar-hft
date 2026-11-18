#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// WsConn: one TLS WebSocket on sim/WsNet.hpp, owned by one thread, with a
// read that waits at most a given time. The read stays outstanding between
// calls, and a write can be made while it is (Beast allows one read and one
// write in flight), so a session thread alternates between sending its queue
// and polling for frames.

#include <chrono>
#include <optional>
#include <string>

#include "sim/WsNet.hpp"

namespace kraken::oe
{
  class WsConn
  {
  public:
    WsConn() : tls_(sim::wsnet::make_tls()), ws_(io_, tls_) { ws_.read_message_max(16 * 1024 * 1024); }

    void connect(const std::string &host, const std::string &target)
    {
      sim::wsnet::ws_connect(io_, ws_, host, "443", target, std::chrono::seconds(10));
    }

    void send(const std::string &text)
    {
      ws_.text(true);
      bool done = false;
      sim::wsnet::beast::error_code ec;
      ws_.async_write(sim::wsnet::net::buffer(text), [&](sim::wsnet::beast::error_code e, std::size_t) {
        ec = e;
        done = true;
      });
      io_.restart();
      while (!done && io_.run_one())
        ;
      if (ec)
        throw sim::wsnet::beast::system_error(ec);
    }

    // One frame if one arrives within `wait`. Throws on a failed connection.
    std::optional<std::string> read_for(std::chrono::milliseconds wait)
    {
      if (!reading_)
      {
        reading_ = true;
        got_ = false;
        buf_.clear();
        ws_.async_read(buf_, [this](sim::wsnet::beast::error_code e, std::size_t) {
          ec_ = e;
          got_ = true;
        });
      }
      if (!got_)
      {
        io_.restart();
        io_.run_for(wait);
      }
      if (!got_)
        return std::nullopt;
      reading_ = false;
      if (ec_)
        throw sim::wsnet::beast::system_error(ec_);
      return sim::wsnet::beast::buffers_to_string(buf_.data());
    }

    void close() noexcept
    {
      sim::wsnet::beast::error_code ec;
      sim::wsnet::beast::get_lowest_layer(ws_).socket().close(ec);
    }

  private:
    sim::wsnet::net::io_context io_;
    sim::wsnet::ssl::context tls_;
    sim::wsnet::WsStream ws_;
    sim::wsnet::beast::flat_buffer buf_;
    sim::wsnet::beast::error_code ec_;
    bool reading_ = false, got_ = false;
  };
}
