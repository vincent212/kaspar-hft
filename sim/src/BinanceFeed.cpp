/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/act/BinanceFeed.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#include "frame/mda/msg/Data.hpp"
#include "sim/WsNet.hpp"

namespace sim
{

using namespace wsnet;

namespace
{
  std::string lower(std::string s)
  {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  }

  // Spot sends depth and trades on one connection. USD-M futures splits them
  // since 2026: depth on /public (high-frequency data), aggTrade on /market;
  // the old /stream path no longer carries the trades.
  struct Endpoints
  {
    const char *ws_host, *ws_port, *rest_host, *rest_path;
    std::string book_target, trade_target;   // trade_target empty: trades share the book's
  };

  Endpoints endpoints(BinanceSync::Market m, const std::string &sym)
  {
    if (m == BinanceSync::Market::SPOT)
      return {"stream.binance.com", "9443", "api.binance.com", "/api/v3/depth",
              "/stream?streams=" + sym + "@depth@100ms/" + sym + "@trade", ""};
    return {"fstream.binance.com", "443", "fapi.binance.com", "/fapi/v1/depth",
            "/public/stream?streams=" + sym + "@depth@100ms",
            "/market/stream?streams=" + sym + "@aggTrade"};
  }

  // Binance sends a depth update every 100 ms, so this much silence is a dead
  // connection.
  constexpr auto kIdleTimeout = std::chrono::seconds(30);
}

BinanceFeed::Config BinanceFeed::config_from_ini(const boost::property_tree::ptree &section,
                                                 const std::string &symbol, bool futures,
                                                 char venue)
{
  Config c;
  c.market = futures ? BinanceSync::Market::FUTURES : BinanceSync::Market::SPOT;
  c.symbol = symbol;
  c.depth_limit = section.get<int>("depth_limit", 1000);
  c.chunks.tick = section.get<double>(futures ? "futures_tick" : "tick");
  c.chunks.chunk = section.get<double>(futures ? "futures_chunk" : "chunk");
  c.chunks.sec_id = section.get<int32_t>("sec_id");
  c.chunks.venue = venue;
  return c;
}

BinanceFeed::BinanceFeed(const Config &cfg, actors::Actor *ob)
    : cfg_(cfg),
      ob_(ob),
      mbp2mbo_(cfg.chunks, [this](const bfile::l3_t &r) { emit(r); }),
      sync_(cfg.market, mbp2mbo_)
{
  snprintf(name, sizeof(name), "BinanceFeed_%s%s", cfg_.symbol.c_str(),
           cfg_.market == BinanceSync::Market::FUTURES ? "_F" : "");
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

void BinanceFeed::emit(const bfile::l3_t &r)
{
  ++records_;
  if (!ob_)
    return;
  auto msg = new frame::mda::msg::Data(true);
  msg->l3 = r;
  ob_->send(msg, this);
}

void BinanceFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

// The read loop. Returns only at shutdown; a failed connection is retried.
// When trades come on a connection of their own (futures), a second thread
// reads it; sync_ and Mbp2Mbo are shared, behind mtx_.
void BinanceFeed::continue_handler(const actors::msg::Continue *) noexcept
{
  const auto ep = endpoints(cfg_.market, lower(cfg_.symbol));
  std::thread trades;
  if (!ep.trade_target.empty())
    trades = std::thread([this, ep] {
      while (!terminate_called)
      {
        try
        {
          run_connection(ep.trade_target, false);
        }
        catch (const std::exception &e)
        {
          std::cerr << get_name() << " trades: " << e.what() << "; reconnecting in 1 s" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    });

  while (!terminate_called)
  {
    try
    {
      run_connection(ep.book_target, true);
    }
    catch (const std::exception &e)
    {
      std::cerr << get_name() << ": " << e.what() << "; reconnecting in 1 s" << std::endl;
    }
    {
      std::lock_guard<std::mutex> lk(mtx_);
      sync_.reset();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  if (trades.joinable())
    trades.join();
}

std::string BinanceFeed::fetch_snapshot()
{
  const auto ep = endpoints(cfg_.market, lower(cfg_.symbol));
  net::io_context io;
  auto tls = make_tls();
  beast::ssl_stream<beast::tcp_stream> s(io, tls);
  tls_connect(io, s, ep.rest_host, "443");

  const std::string target = std::string(ep.rest_path) + "?symbol=" + cfg_.symbol +
                             "&limit=" + std::to_string(cfg_.depth_limit);
  http::request<http::string_body> req{http::verb::get, target, 11};
  req.set(http::field::host, ep.rest_host);
  req.set(http::field::user_agent, "kaspar-hft");
  beast::get_lowest_layer(s).expires_after(kRestTimeout);
  await(io, [&](auto h) { http::async_write(s, req, std::move(h)); });

  beast::flat_buffer buf;
  http::response_parser<http::string_body> parser;
  parser.body_limit(64 * 1024 * 1024);
  await(io, [&](auto h) { http::async_read(s, buf, parser, std::move(h)); });
  auto res = parser.release();
  if (res.result() != http::status::ok)
    throw std::runtime_error("snapshot " + target + ": HTTP " +
                             std::to_string(res.result_int()) + " " + res.body().substr(0, 200));
  beast::error_code ec;
  s.shutdown(ec);   // Binance often skips the TLS close_notify; not an error
  return std::move(res.body());
}

// One WebSocket session. `book`: this connection carries the depth stream and
// so drives the snapshots.
void BinanceFeed::run_connection(const std::string &target, bool book)
{
  const auto ep = endpoints(cfg_.market, lower(cfg_.symbol));
  net::io_context io;
  auto tls = make_tls();
  websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(io, tls);
  tls_connect(io, ws.next_layer(), ep.ws_host, ep.ws_port);
  // From here the WebSocket's own timeout governs, not the TCP layer's.
  beast::get_lowest_layer(ws).expires_never();
  auto to = websocket::stream_base::timeout::suggested(beast::role_type::client);
  to.idle_timeout = kIdleTimeout;
  to.keep_alive_pings = true;
  ws.set_option(to);
  await(io, [&](auto h) {
    ws.async_handshake(std::string(ep.ws_host) + ":" + ep.ws_port, target, std::move(h));
  });
  std::cerr << get_name() << ": connected wss://" << ep.ws_host << ":" << ep.ws_port << target
            << std::endl;

  beast::flat_buffer buf;
  while (!terminate_called)
  {
    buf.clear();
    // Binance's pings are answered inside the read; silence past
    // kIdleTimeout fails it, and the caller reconnects.
    await(io, [&](auto h) { ws.async_read(buf, std::move(h)); });
    const auto msg = nlohmann::json::parse(beast::buffers_to_string(buf.data()));
    auto it = msg.find("data");
    if (it == msg.end())
      continue;
    std::unique_lock<std::mutex> lk(mtx_);
    sync_.on_message(*it);
    if (!book)
      continue;

    // Snapshot once there is something buffered to line it up with. Fetched
    // without the lock, so trades keep flowing to the (unsynced) book's buffer.
    // At most one fetch a second: a snapshot that comes back too old leaves
    // need_snapshot() set, and fetching on every message would run into
    // Binance's request-weight limit (and then an IP ban).
    if (sync_.need_snapshot() && now_ns() - last_snapshot_ns_ >= 1000000000ull)
    {
      last_snapshot_ns_ = now_ns();
      lk.unlock();
      const auto body = nlohmann::json::parse(fetch_snapshot());
      lk.lock();
      sync_.on_snapshot(body, now_ns());
      std::cerr << get_name() << ": snapshot applied, "
                << (sync_.synced() ? "in sync" : "too old, will refetch")
                << " (gaps so far " << sync_.gaps() << ")" << std::endl;
    }
    if (!ob_)
      dump(now_ns());
  }
  beast::error_code ec;
  ws.close(websocket::close_code::normal, ec);
}

void BinanceFeed::dump(uint64_t t)
{
  if (t - last_dump_ns_ < 1000000000ull)
    return;
  last_dump_ns_ = t;
  const auto bb = mbp2mbo_.best(true), ba = mbp2mbo_.best(false);
  const double tick = cfg_.chunks.tick;
  // Prices to the tick's decimals: the default 6 significant digits print a
  // one-tick BTC spread as a locked book.
  const int dp = std::max(0, int(std::ceil(-std::log10(tick) - 1e-9)));
  std::cout << std::fixed << std::setprecision(dp)
            << get_name() << (sync_.synced() ? " synced" : " syncing")
            << " bid " << bb * tick << " x " << mbp2mbo_.chunks(true, bb) << " chunks"
            << "  ask " << ba * tick << " x " << mbp2mbo_.chunks(false, ba) << " chunks"
            << "  events " << sync_.events_applied() << " trades " << sync_.trades_applied()
            << " gaps " << sync_.gaps() << " records " << records_ << std::endl;
}

}
