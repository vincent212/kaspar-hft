/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_md/act/OkxFeed.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>

#include "frame/mda/msg/Data.hpp"
#include "okx_md/OkxWs.hpp"

namespace okx::md
{

OkxFeed::Config OkxFeed::config_from_ini(const boost::property_tree::ptree &s, char venue)
{
  Config c;
  c.inst_id = s.get<std::string>("inst_id");
  c.host = s.get<std::string>("host", "ws.okx.com");
  c.window_depth = s.get<size_t>("window_depth", 50);
  c.record_path = s.get<std::string>("record_path", "");
  c.chunks.tick = s.get<double>("tick");
  c.chunks.chunk = s.get<double>("chunk");
  c.chunks.sec_id = s.get<int32_t>("sec_id");
  c.chunks.venue = venue;
  return c;
}

OkxFeed::OkxFeed(const Config &cfg, actors::Actor *ob)
    : cfg_(cfg),
      ob_(ob),
      mbp2mbo_(cfg.chunks, [this](const bfile::l3_t &r) { emit(r); }),
      sync_(cfg.inst_id, mbp2mbo_, cfg.window_depth)
{
  snprintf(name, sizeof(name), "OkxFeed_%s", cfg_.inst_id.c_str());
  // A synchronised book invalidated (sequence gap, checksum mismatch, lost
  // connection): OB's book is stale until the next snapshot.
  sync_.on_gap([this] {
    bfile::l3_gap_v2_t g{};
    g.typ = en::l3::GAP_V2;
    g.venue = cfg_.chunks.venue;
    emit(g);
  });
  if (!cfg_.record_path.empty())
    rec_ = std::make_unique<std::ofstream>(cfg_.record_path, std::ios::app);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

void OkxFeed::emit(const bfile::l3_t &r)
{
  ++records_;
  if (!ob_)
    return;
  auto msg = new frame::mda::msg::Data(true);
  msg->l3 = r;
  ob_->send(msg, this);
}

void OkxFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

void OkxFeed::continue_handler(const actors::msg::Continue *) noexcept
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
    // OKX allows 3 connection requests per second per IP.
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void OkxFeed::run_connection()
{
  WsClient ws(cfg_.host, "443", "/ws/v5/public");
  ws.connect();
  nlohmann::json args = nlohmann::json::array(
      {{{"channel", "books"}, {"instId", cfg_.inst_id}}, {{"channel", "trades"}, {"instId", cfg_.inst_id}}});
  ws.send(nlohmann::json{{"op", "subscribe"}, {"args", args}}.dump());
  std::cerr << get_name() << ": connected wss://" << cfg_.host << "/ws/v5/public" << std::endl;

  while (!terminate_called)
  {
    const auto text = ws.read();
    if (rec_)
      *rec_ << "W " << now_ns() << ' ' << text << '\n';
    if (text == "pong")
      continue;
    const auto msg = nlohmann::json::parse(text);
    if (auto ev = msg.find("event"); ev != msg.end())
    {
      const auto e = ev->get<std::string>();
      if (e == "error")
        throw std::runtime_error("OKX error " + msg.value("code", std::string()) + ": " +
                                 msg.value("msg", std::string()));
      if (e == "notice")   // e.g. 64008: service upgrade, reconnect
        throw std::runtime_error("OKX notice " + msg.value("code", std::string()) + ": " +
                                 msg.value("msg", std::string()));
      continue;
    }
    if (sync_.on_message(msg) == OkxSync::Status::Gap)
      throw std::runtime_error("sequence gap");
    if (!ob_)
      dump(now_ns());
  }
}

void OkxFeed::dump(uint64_t t)
{
  if (t - last_dump_ns_ < 1000000000ull)
    return;
  last_dump_ns_ = t;
  const auto bb = mbp2mbo_.best(true), ba = mbp2mbo_.best(false);
  const double tick = cfg_.chunks.tick;
  const int dp = std::max(0, int(std::ceil(-std::log10(tick) - 1e-9)));
  const auto &s = sync_.stats();
  std::cout << std::fixed << std::setprecision(dp) << get_name() << (sync_.synced() ? " synced" : " syncing")
            << " seqId " << sync_.seq_id() << " bid " << bb * tick << " x " << mbp2mbo_.chunks(true, bb)
            << " ask " << ba * tick << " x " << mbp2mbo_.chunks(false, ba) << " updates " << s.updates
            << " trades " << s.trades << " gaps " << s.gaps << " records " << records_ << std::endl;
}

}
