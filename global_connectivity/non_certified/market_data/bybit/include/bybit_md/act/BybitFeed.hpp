#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitFeed: live Bybit V5 public market data for one symbol of one category
// (spot or linear), as Kaspar records to that symbol's OB. NOT CERTIFIED.
//
//   WebSocket (TLS)  wss://stream.bybit.com/v5/public/{category}
//                    orderbook.full.{symbol} (or orderbook.{depth}.{symbol})
//                    and publicTrade.{symbol}, one connection
//   REST (TLS)       GET /v5/market/full_orderbook, at start, after a gap and
//                    after u = 1 (FULL mode only), at most once a second
//        -> BybitSync -> LevelWindow -> Mbp2Mbo -> frame::mda::msg::Data to OB
//
// Runs its own thread like sim::BinanceFeed: Start sends a Continue to
// itself, and the Continue handler is the read loop, which returns only at
// shutdown. A dropped connection is reopened after a second and the book is
// synchronised again. Sends {"op":"ping"} every 20 s (Bybit's guidance).
// With record_path set, every frame and REST body is appended to that file
// (bybit::bnet::Recorder format) for replay and tests.

#include <cstdint>
#include <memory>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "bybit_md/BybitNet.hpp"
#include "bybit_md/BybitSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace bybit
{
  class BybitFeed : public actors::Actor
  {
  public:
    struct Config
    {
      std::string category = "linear"; // spot | linear
      std::string symbol = "BTCUSDT";
      BybitSync::Mode mode = BybitSync::Mode::FULL;
      int depth = 1000;                // DEPTH mode: 1, 50, 200 or 1000
      double tick = 0;
      double chunk = 0;
      double print_unit = 0;
      size_t window = 1000;
      int32_t sec_id = 0;
      std::string record_path;         // empty: no recording
      bnet::Hosts hosts;
    };

    // A [<category>_<symbol>] section (no dots: property_tree paths): tick, chunk, print_unit, window,
    // sec_id, mode (full|depth), depth, record. nullptr-safe: throws
    // boost::property_tree errors on a missing key.
    static Config config_from_ini(const boost::property_tree::ptree &section,
                                  const std::string &category, const std::string &symbol);

    BybitFeed(const Config &cfg, actors::Actor *ob);

    const char *get_name() const override { return name; }
    const BybitSync &sync() const { return sync_; }

  private:
    Config cfg_;
    actors::Actor *ob_;
    sim::Mbp2Mbo mbp2mbo_;
    BybitSync sync_;
    std::unique_ptr<bnet::Recorder> rec_;
    uint64_t records_ = 0;
    uint64_t last_snapshot_ns_ = 0;
    uint64_t last_dump_ns_ = 0;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;
    void emit(const bfile::l3_t &r);
    void run_connection(); // throws on error
    void dump(uint64_t now_ns);
  };
}
