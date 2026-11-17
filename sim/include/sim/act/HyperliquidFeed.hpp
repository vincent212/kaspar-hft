#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "sim/HyperliquidSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace sim
{
  /**
   * HyperliquidFeed -- live Hyperliquid perpetuals market data for one coin,
   * as MBO records to that coin's OB.
   *
   *   WebSocket (TLS)  wss://api.hyperliquid.xyz/ws, subscriptions l2Book
   *                    (with "fast": about two books a second, against one
   *                    every several seconds without) and trades; no API key
   *        -> HyperliquidSync (book snapshots diffed, trades)
   *        -> Mbp2Mbo         (levels -> chunk orders)
   *        -> frame::mda::msg::Data to the OB
   *
   * Hyperliquid closes a connection that has sent nothing for 60 seconds, so
   * the feed sends {"method":"ping"} every 30. Runs its own thread, as
   * BinanceFeed does; a dropped connection reconnects after a second, and the
   * first book on the new one goes to OB as recovery records.
   *
   * With no OB it runs as a monitor, printing the top of the synthetic book
   * once a second (bnsim --exchange hyperliquid --dump).
   */
  class HyperliquidFeed : public actors::Actor
  {
  public:
    struct Config
    {
      std::string coin;   // e.g. BTC
      Mbp2Mbo::Config chunks;
    };

    HyperliquidFeed(const Config &cfg, actors::Actor *ob);

    // A coin's section of hyperliquid.ini (sim/CRYPTO.md).
    static Config config_from_ini(const boost::property_tree::ptree &section,
                                  const std::string &coin, char venue);

  private:
    Config cfg_;
    actors::Actor *ob_;
    Mbp2Mbo mbp2mbo_;
    HyperliquidSync sync_;
    uint64_t records_ = 0;
    uint64_t last_dump_ns_ = 0;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;

    void emit(const bfile::l3_t &r);
    void run_connection();   // throws on error
    void dump(uint64_t now_ns);
  };
}
