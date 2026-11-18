/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_md/act/KrakenFeed.hpp"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "frame/mda/msg/Data.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "sim/WsNet.hpp"

namespace kraken
{
  using namespace sim::wsnet;

  namespace
  {
    // Kraken sends {"channel":"heartbeat"} about once a second when nothing
    // else is sent; 10 s of silence is a dead connection.
    constexpr auto kIdleTimeout = std::chrono::seconds(10);
    constexpr size_t kMaxMessage = 16 * 1024 * 1024;
  }

  KrakenFeed::Config KrakenFeed::config_from_ini(const boost::property_tree::ptree &section,
                                                 const std::string &symbol, char venue)
  {
    Config c;
    c.symbol = symbol;
    c.depth = section.get<int>("depth", 1000);
    c.window = section.get<int>("window", c.depth);
    c.record = section.get<std::string>("record", "");
    c.chunks.tick = section.get<double>("tick");
    c.chunks.chunk = section.get<double>("chunk");
    c.chunks.sec_id = section.get<int32_t>("sec_id");
    c.chunks.venue = venue;
    return c;
  }

  KrakenFeed::KrakenFeed(const Config &cfg, actors::Actor *ob)
      : cfg_(cfg), ob_(ob), mbp2mbo_(cfg.chunks, [this](const bfile::l3_t &r) { emit(r); }),
        sync_(cfg.symbol, size_t(cfg.depth), &mbp2mbo_, size_t(cfg.window))
  {
    snprintf(name, sizeof(name), "KrakenFeed_%s", cfg_.symbol.c_str());
    if (!cfg_.record.empty())
    {
      recorder_ = std::make_unique<FrameRecorder>(cfg_.record);
      if (!recorder_->ok())
        throw std::runtime_error("KrakenFeed: cannot write " + cfg_.record);
    }
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
  }

  void KrakenFeed::emit(const bfile::l3_t &r)
  {
    ++records_;
    if (!ob_)
      return;
    auto msg = new frame::mda::msg::Data(true);
    msg->l3 = r;
    ob_->send(msg, this);
  }

  void KrakenFeed::start_handler(const actors::msg::Start *) noexcept { send(new actors::msg::Continue(), this); }

  void KrakenFeed::continue_handler(const actors::msg::Continue *) noexcept
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
      ++reconnects_;
      sync_.reset();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void KrakenFeed::run_connection()
  {
    net::io_context io;
    auto tls = make_tls();
    WsStream ws(io, tls);
    ws.read_message_max(kMaxMessage);
    ws_connect(io, ws, cfg_.host, "443", "/v2", kIdleTimeout);
    const auto sym = nlohmann::json::array({cfg_.symbol});
    ws_write(io, ws,
             nlohmann::json{{"method", "subscribe"},
                            {"params", {{"channel", "book"}, {"symbol", sym}, {"depth", cfg_.depth}}}}
                 .dump());
    ws_write(io, ws,
             nlohmann::json{{"method", "subscribe"},
                            {"params", {{"channel", "trade"}, {"symbol", sym}, {"snapshot", false}}}}
                 .dump());
    std::cerr << get_name() << ": connected wss://" << cfg_.host << "/v2" << std::endl;

    beast::flat_buffer buf;
    while (!terminate_called)
    {
      const std::string frame = ws_read(io, ws, buf);
      if (recorder_)
        recorder_->write(now_ns(), frame);
      const auto r = sync_.on_message(parse_raw(frame));
      if (r == KrakenBookSync::Result::ChecksumMismatch)
        throw std::runtime_error("book checksum mismatch");
      if (r == KrakenBookSync::Result::Error)
        throw std::runtime_error("unreadable frame");
      if (!ob_)
        dump(now_ns());
    }
    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);
  }

  void KrakenFeed::dump(uint64_t t)
  {
    if (t - last_dump_ns_ < 1000000000ull)
      return;
    last_dump_ns_ = t;
    const auto &s = sync_.stats();
    const auto bb = sync_.top(true, 1), ba = sync_.top(false, 1);
    std::cout << get_name() << (sync_.synced() ? " synced" : " syncing");
    if (!bb.empty() && !ba.empty())
      std::cout << std::fixed << std::setprecision(1) << " bid " << fixed_to_double(bb[0].first) << " ask "
                << fixed_to_double(ba[0].first);
    std::cout << " updates " << s.updates << " checksum ok " << s.checksum_ok << " bad " << s.checksum_bad
              << " trades " << s.trades << " records " << records_ << std::endl;
  }
}
