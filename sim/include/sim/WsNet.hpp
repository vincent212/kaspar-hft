#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// TLS and WebSocket plumbing shared by the live crypto feeds (BinanceFeed,
// CoinbaseFeed, HyperliquidFeed).

#include <chrono>
#include <cstdint>
#include <string>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace sim::wsnet
{
  namespace beast = boost::beast;
  namespace http = beast::http;
  namespace websocket = beast::websocket;
  namespace net = boost::asio;
  namespace ssl = net::ssl;
  using tcp = net::ip::tcp;
  using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

  inline uint64_t now_ns()
  {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
  }

  inline ssl::context make_tls()
  {
    ssl::context ctx(ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    return ctx;
  }

  // Run one asynchronous operation to completion. Every network step goes
  // through here rather than the blocking calls, because only the async ones
  // honour a deadline (tcp_stream::expires_after, the WebSocket idle timeout):
  // a blocking read on a connection that died without a FIN or RST waits
  // forever, and SO_RCVTIMEO does not help -- Asio retries the read with an
  // untimed poll.
  template <class Start>
  void await(net::io_context &io, Start start)
  {
    beast::error_code ec;
    bool done = false;
    start([&ec, &done](beast::error_code e, auto &&...) { ec = e; done = true; });
    // Until this operation completes, not until the context runs dry: the
    // WebSocket keeps its idle timer armed between reads.
    io.restart();
    while (!done && io.run_one())
      ;
    if (ec)
      throw beast::system_error(ec);
  }

  constexpr auto kConnectTimeout = std::chrono::seconds(10);
  constexpr auto kRestTimeout = std::chrono::seconds(10);

  // TLS stream connected to host:port, SNI and certificate name checked.
  template <class Stream>
  void tls_connect(net::io_context &io, Stream &s, const std::string &host, const std::string &port)
  {
    tcp::resolver resolver(io);
    auto results = resolver.resolve(host, port);
    if (!SSL_set_tlsext_host_name(s.native_handle(), host.c_str()))
      throw beast::system_error(beast::error_code(int(::ERR_get_error()), net::error::get_ssl_category()));
    s.set_verify_callback(ssl::host_name_verification(host));
    auto &tcp_layer = beast::get_lowest_layer(s);
    tcp_layer.expires_after(kConnectTimeout);
    await(io, [&](auto h) { tcp_layer.async_connect(results, std::move(h)); });
    tcp_layer.expires_after(kConnectTimeout);
    await(io, [&](auto h) { s.async_handshake(ssl::stream_base::client, std::move(h)); });
  }

  // A WebSocket over TLS, handshake done. From here the WebSocket's idle
  // timeout governs, not the TCP layer's: silence for `idle` fails the next
  // read, and Beast pings at half of it.
  inline void ws_connect(net::io_context &io, WsStream &ws, const std::string &host,
                         const std::string &port, const std::string &target,
                         std::chrono::seconds idle)
  {
    tls_connect(io, ws.next_layer(), host, port);
    beast::get_lowest_layer(ws).expires_never();
    auto to = websocket::stream_base::timeout::suggested(beast::role_type::client);
    to.idle_timeout = idle;
    to.keep_alive_pings = true;
    ws.set_option(to);
    await(io, [&](auto h) { ws.async_handshake(host + ":" + port, target, std::move(h)); });
  }

  inline void ws_write(net::io_context &io, WsStream &ws, const std::string &text)
  {
    ws.text(true);
    await(io, [&](auto h) { ws.async_write(net::buffer(text), std::move(h)); });
  }

  inline std::string ws_read(net::io_context &io, WsStream &ws, beast::flat_buffer &buf)
  {
    buf.clear();
    await(io, [&](auto h) { ws.async_read(buf, std::move(h)); });
    return beast::buffers_to_string(buf.data());
  }
}
