/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/act/CoinbaseFeed.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "frame/mda/msg/Data.hpp"
#include "sim/WsNet.hpp"

namespace sim
{

using namespace wsnet;

namespace
{
  constexpr const char *kHost = "advanced-trade-ws.coinbase.com";
  // Coinbase sends a heartbeat every second on the heartbeats channel.
  constexpr auto kIdleTimeout = std::chrono::seconds(30);
  // The level2 snapshot is the whole book: tens of thousands of levels.
  constexpr size_t kMaxMessage = 64 * 1024 * 1024;
}

CoinbaseFeed::Config CoinbaseFeed::config_from_ini(const boost::property_tree::ptree &section,
                                                   const std::string &product, char venue)
{
  Config c;
  c.product = product;
  c.depth = section.get<int>("depth", 1000);
  c.chunks.tick = section.get<double>("tick");
  c.chunks.chunk = section.get<double>("chunk");
  c.chunks.sec_id = section.get<int32_t>("sec_id");
  c.chunks.venue = venue;
  return c;
}

CoinbaseFeed::CoinbaseFeed(const Config &cfg, actors::Actor *ob)
    : cfg_(cfg),
      ob_(ob),
      mbp2mbo_(cfg.chunks, [this](const bfile::l3_t &r) { emit(r); }),
      sync_(cfg.product, mbp2mbo_, size_t(cfg.depth))
{
  snprintf(name, sizeof(name), "CoinbaseFeed_%s", cfg_.product.c_str());
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

void CoinbaseFeed::emit(const bfile::l3_t &r)
{
  ++records_;
  if (!ob_)
    return;
  auto msg = new frame::mda::msg::Data(true);
  msg->l3 = r;
  ob_->send(msg, this);
}

void CoinbaseFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

// The read loop. Returns only at shutdown; a failed connection is retried.
void CoinbaseFeed::continue_handler(const actors::msg::Continue *) noexcept
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

void CoinbaseFeed::run_connection()
{
  net::io_context io;
  auto tls = make_tls();
  WsStream ws(io, tls);
  ws.read_message_max(kMaxMessage);
  ws_connect(io, ws, kHost, "443", "/", kIdleTimeout);
  for (const char *channel : {"level2", "market_trades", "heartbeats"})
    ws_write(io, ws,
             nlohmann::json{{"type", "subscribe"},
                            {"product_ids", nlohmann::json::array({cfg_.product})},
                            {"channel", channel}}
                 .dump());
  std::cerr << get_name() << ": connected wss://" << kHost << std::endl;

  beast::flat_buffer buf;
  while (!terminate_called)
  {
    const auto msg = nlohmann::json::parse(ws_read(io, ws, buf));
    if (!sync_.on_message(msg))
      throw std::runtime_error("sequence gap");
    if (!ob_)
      dump(now_ns());
  }
  beast::error_code ec;
  ws.close(websocket::close_code::normal, ec);
}

void CoinbaseFeed::dump(uint64_t t)
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
            << "  events " << sync_.events_applied() << " trades " << sync_.trades_applied()
            << " gaps " << sync_.gaps() << " records " << records_ << std::endl;
}

}
