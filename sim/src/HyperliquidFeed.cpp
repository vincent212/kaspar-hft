/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/act/HyperliquidFeed.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>

#include "frame/mda/msg/Data.hpp"
#include "sim/WsNet.hpp"

namespace sim
{

using namespace wsnet;

namespace
{
  constexpr const char *kHost = "api.hyperliquid.xyz";
  // A book arrives several times a second.
  constexpr auto kIdleTimeout = std::chrono::seconds(30);
  // Hyperliquid drops a connection that has sent nothing for 60 s.
  constexpr uint64_t kPingNs = 30ull * 1000000000ull;
}

HyperliquidFeed::Config HyperliquidFeed::config_from_ini(const boost::property_tree::ptree &section,
                                                         const std::string &coin, char venue)
{
  Config c;
  c.coin = coin;
  c.chunks.tick = section.get<double>("tick");
  c.chunks.chunk = section.get<double>("chunk");
  c.chunks.sec_id = section.get<int32_t>("sec_id");
  c.chunks.venue = venue;
  return c;
}

HyperliquidFeed::HyperliquidFeed(const Config &cfg, actors::Actor *ob)
    : cfg_(cfg),
      ob_(ob),
      mbp2mbo_(cfg.chunks, [this](const bfile::l3_t &r) { emit(r); }),
      sync_(cfg.coin, mbp2mbo_)
{
  snprintf(name, sizeof(name), "HyperliquidFeed_%s", cfg_.coin.c_str());
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

void HyperliquidFeed::emit(const bfile::l3_t &r)
{
  ++records_;
  if (!ob_)
    return;
  auto msg = new frame::mda::msg::Data(true);
  msg->l3 = r;
  ob_->send(msg, this);
}

void HyperliquidFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

// The read loop. Returns only at shutdown; a failed connection is retried.
void HyperliquidFeed::continue_handler(const actors::msg::Continue *) noexcept
{
  while (!terminate_called)
  {
    try
    {
      run_connection();
    }
    catch (const std::exception &e)
    {
      std::cerr << get_name() << ": " << e.what() << "; reconnecting in 1 s" << std::endl;
    }
    sync_.reset();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void HyperliquidFeed::run_connection()
{
  net::io_context io;
  auto tls = make_tls();
  WsStream ws(io, tls);
  ws_connect(io, ws, kHost, "443", "/ws", kIdleTimeout);
  // fast: a book about twice a second; without it, one every several seconds.
  ws_write(io, ws,
           nlohmann::json{{"method", "subscribe"},
                          {"subscription", {{"type", "l2Book"}, {"coin", cfg_.coin}, {"fast", true}}}}
               .dump());
  ws_write(io, ws,
           nlohmann::json{{"method", "subscribe"},
                          {"subscription", {{"type", "trades"}, {"coin", cfg_.coin}}}}
               .dump());
  std::cerr << get_name() << ": connected wss://" << kHost << "/ws" << std::endl;

  beast::flat_buffer buf;
  uint64_t last_ping = now_ns();
  while (!terminate_called)
  {
    if (now_ns() - last_ping >= kPingNs)
    {
      ws_write(io, ws, R"({"method":"ping"})");
      last_ping = now_ns();
    }
    sync_.on_message(nlohmann::json::parse(ws_read(io, ws, buf)));
    if (!ob_)
      dump(now_ns());
  }
  beast::error_code ec;
  ws.close(websocket::close_code::normal, ec);
}

void HyperliquidFeed::dump(uint64_t t)
{
  if (t - last_dump_ns_ < 1000000000ull)
    return;
  last_dump_ns_ = t;
  const auto bb = mbp2mbo_.best(true), ba = mbp2mbo_.best(false);
  const double tick = cfg_.chunks.tick;
  const int dp = std::max(0, int(std::ceil(-std::log10(tick) - 1e-9)));
  std::cout << std::fixed << std::setprecision(dp) << get_name()
            << (sync_.synced() ? " synced" : " syncing") << " bid " << bb * tick << " x "
            << mbp2mbo_.chunks(true, bb) << " chunks"
            << "  ask " << ba * tick << " x " << mbp2mbo_.chunks(false, ba) << " chunks"
            << "  books " << sync_.events_applied() << " trades " << sync_.trades_applied()
            << " records " << records_ << std::endl;
}

}
