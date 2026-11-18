/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_md/act/DeribitFeed.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "deribit_md/DeribitWs.hpp"
#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "sim/WsNet.hpp"

namespace deribit
{

using namespace sim::wsnet;

namespace
{
  // With set_heartbeat(30) the server sends a test_request every 30 s even on a
  // quiet book, so this much silence is a dead connection.
  constexpr auto kIdleTimeout = std::chrono::seconds(75);
  // The first book notification is every level of the book.
  constexpr size_t kMaxMessage = 64 * 1024 * 1024;
}

DeribitFeed::Config DeribitFeed::config_from_ini(const boost::property_tree::ptree &section,
                                                 const std::string &instrument, char venue)
{
  Config c;
  c.instrument = instrument;
  c.interval = section.get<std::string>("interval", "100ms");
  c.host = section.get<std::string>("host", "www.deribit.com");
  c.depth = section.get<int>("depth", 50);
  c.record_path = section.get<std::string>("record", "");
  c.chunks.tick = section.get<double>("tick");
  c.chunks.chunk = section.get<double>("chunk");
  c.chunks.sec_id = section.get<int32_t>("sec_id");
  c.chunks.venue = venue;
  return c;
}

DeribitFeed::DeribitFeed(const Config &cfg, actors::Actor *ob)
    : cfg_(cfg),
      ob_(ob),
      mbp2mbo_(cfg.chunks, [this](const bfile::l3_t &r) { emit(r); }),
      sync_(cfg.instrument, cfg.chunks.tick, &mbp2mbo_, size_t(cfg.depth))
{
  snprintf(name, sizeof(name), "DeribitFeed_%s", cfg_.instrument.c_str());
  if (!cfg_.record_path.empty())
    rec_ = std::make_unique<FrameRecorder>(cfg_.record_path);
  sync_.on_resync([this](uint64_t) {
    bfile::l3_chr_v2_t c{};
    c.typ = en::l3::CHR_V2;
    c.venue = cfg_.chunks.venue;
    emit(c);
  });
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

void DeribitFeed::emit(const bfile::l3_t &r)
{
  ++records_;
  if (!ob_)
    return;
  auto msg = new frame::mda::msg::Data(true);
  msg->l3 = r;
  ob_->send(msg, this);
}

void DeribitFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

void DeribitFeed::continue_handler(const actors::msg::Continue *) noexcept
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

void DeribitFeed::run_connection()
{
  net::io_context io;
  auto tls = make_tls();
  WsStream ws(io, tls);
  ws.read_message_max(kMaxMessage);
  ws_connect(io, ws, cfg_.host, "443", ws::kPath, kIdleTimeout);
  ws_write(io, ws, ws::set_heartbeat(next_id_++, cfg_.heartbeat_s).dump());
  ws_write(io, ws, ws::subscribe(next_id_++, ws::channels(cfg_.instrument, cfg_.interval)).dump());
  std::cerr << get_name() << ": connected wss://" << cfg_.host << ws::kPath << std::endl;

  beast::flat_buffer buf;
  while (!terminate_called)
  {
    const auto text = ws_read(io, ws, buf);
    if (rec_)
      rec_->write(now_ns(), text);
    const auto msg = nlohmann::json::parse(text);
    if (auto reply = ws::heartbeat_reply(msg, next_id_); !reply.is_null())
    {
      ++next_id_;
      ws_write(io, ws, reply.dump());
      continue;
    }
    if (msg.contains("error"))
      throw std::runtime_error("deribit error: " + msg["error"].dump());
    if (!sync_.on_message(msg))
      throw std::runtime_error("change_id gap");
    if (!ob_)
      dump(now_ns());
  }
  beast::error_code ec;
  ws.close(websocket::close_code::normal, ec);
}

void DeribitFeed::dump(uint64_t t)
{
  if (t - last_dump_ns_ < 1000000000ull)
    return;
  last_dump_ns_ = t;
  const auto bb = mbp2mbo_.best(true), ba = mbp2mbo_.best(false);
  const double tick = cfg_.chunks.tick;
  const int dp = std::max(0, int(std::ceil(-std::log10(tick) - 1e-9)));
  const auto &s = sync_.stats();
  std::cout << std::fixed << std::setprecision(dp) << get_name()
            << (sync_.synced() ? " synced" : " syncing") << " bid " << bb * tick << " x "
            << mbp2mbo_.chunks(true, bb) << " chunks  ask " << ba * tick << " x "
            << mbp2mbo_.chunks(false, ba) << " chunks  notifications " << s.notifications
            << " trades " << s.trades << " gaps " << s.gaps << " records " << records_ << std::endl;
}

}
