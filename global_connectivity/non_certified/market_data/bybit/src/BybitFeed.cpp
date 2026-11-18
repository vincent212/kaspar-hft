/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_md/act/BybitFeed.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>

#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"

namespace bybit
{

namespace
{
  BybitSync::Config sync_config(const BybitFeed::Config &c)
  {
    BybitSync::Config s;
    s.mode = c.mode;
    s.symbol = c.symbol;
    s.tick = c.tick;
    s.print_unit = c.print_unit;
    s.window = c.window;
    s.sec_id = c.sec_id;
    s.venue = char(c.category == "spot" ? en::x::BYBIT : en::x::BYBITF);
    return s;
  }
}

BybitFeed::Config BybitFeed::config_from_ini(const boost::property_tree::ptree &s,
                                             const std::string &category,
                                             const std::string &symbol)
{
  Config c;
  c.category = category;
  c.symbol = symbol;
  c.tick = s.get<double>("tick");
  c.chunk = s.get<double>("chunk");
  c.print_unit = s.get<double>("print_unit");
  c.window = s.get<size_t>("window", 1000);
  c.sec_id = s.get<int32_t>("sec_id");
  c.mode = s.get<std::string>("mode", "full") == "depth" ? BybitSync::Mode::DEPTH
                                                         : BybitSync::Mode::FULL;
  c.depth = s.get<int>("depth", 1000);
  c.record_path = s.get<std::string>("record", "");
  return c;
}

BybitFeed::BybitFeed(const Config &cfg, actors::Actor *ob)
    : cfg_(cfg),
      ob_(ob),
      mbp2mbo_(sim::Mbp2Mbo::Config{cfg.sec_id,
                                    char(cfg.category == "spot" ? en::x::BYBIT : en::x::BYBITF),
                                    cfg.tick, cfg.chunk},
               [this](const bfile::l3_t &r) { emit(r); }),
      sync_(sync_config(cfg), mbp2mbo_, [this](const bfile::l3_t &r) { emit(r); })
{
  snprintf(name, sizeof(name), "BybitFeed_%s_%s", cfg_.category.c_str(), cfg_.symbol.c_str());
  if (!cfg_.record_path.empty())
    rec_ = std::make_unique<bnet::Recorder>(cfg_.record_path);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

void BybitFeed::emit(const bfile::l3_t &r)
{
  ++records_;
  if (!ob_)
    return;
  auto msg = new frame::mda::msg::Data(true);
  msg->l3 = r;
  ob_->send(msg, this);
}

void BybitFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

void BybitFeed::continue_handler(const actors::msg::Continue *) noexcept
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

void BybitFeed::run_connection()
{
  using sim::wsnet::now_ns;
  const std::string book_topic = cfg_.mode == BybitSync::Mode::FULL
                                     ? "orderbook.full." + cfg_.symbol
                                     : "orderbook." + std::to_string(cfg_.depth) + "." + cfg_.symbol;
  bnet::PublicWs ws(cfg_.hosts, cfg_.category);
  ws.connect({book_topic, "publicTrade." + cfg_.symbol});
  std::cerr << get_name() << ": connected, " << book_topic << std::endl;
  while (!terminate_called)
  {
    const auto frame = ws.read();
    const uint64_t t = now_ns();
    if (rec_)
      rec_->ws(t, frame);
    if (!sync_.on_message(nlohmann::json::parse(frame)))
      throw std::runtime_error("gap in a depth-limited stream: resubscribing");

    // FULL mode: a snapshot once deltas are buffered to line it up with, at
    // most one a second (a refused snapshot leaves need_snapshot() set).
    if (sync_.need_snapshot() && sync_.buffering() && t - last_snapshot_ns_ >= 1000000000ull)
    {
      last_snapshot_ns_ = t;
      const auto body = bnet::full_orderbook(cfg_.hosts, cfg_.category, cfg_.symbol);
      if (rec_)
        rec_->rest(now_ns(), body);
      const auto r = sync_.on_snapshot(nlohmann::json::parse(body).at("result"));
      std::cerr << get_name() << ": snapshot "
                << (r == BybitSync::SnapResult::Applied ? "applied" : "refused, will refetch")
                << " (gaps so far " << sync_.stats().gaps << ")" << std::endl;
    }
    if (!ob_)
      dump(t);
  }
  ws.close();
}

void BybitFeed::dump(uint64_t t)
{
  if (t - last_dump_ns_ < 1000000000ull)
    return;
  last_dump_ns_ = t;
  const auto bb = mbp2mbo_.best(true), ba = mbp2mbo_.best(false);
  const int dp = std::max(0, int(std::ceil(-std::log10(cfg_.tick) - 1e-9)));
  const auto &st = sync_.stats();
  std::cout << std::fixed << std::setprecision(dp) << get_name()
            << (sync_.synced() ? " synced" : " syncing") << " u " << sync_.last_u() << " bid "
            << bb * cfg_.tick << " x " << mbp2mbo_.chunks(true, bb) << " ask " << ba * cfg_.tick
            << " x " << mbp2mbo_.chunks(false, ba) << " deltas " << st.deltas << " trades "
            << st.trades << " gaps " << st.gaps << " records " << records_ << std::endl;
}

}
