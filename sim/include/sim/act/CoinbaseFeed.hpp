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
#include "sim/CoinbaseSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace sim
{
  /**
   * CoinbaseFeed -- live Coinbase spot market data for one product, as MBO
   * records to that product's OB.
   *
   *   WebSocket (TLS)  wss://advanced-trade-ws.coinbase.com, channels level2,
   *                    market_trades and heartbeats; no API key needed
   *        -> CoinbaseSync (snapshot, updates, sequence check)
   *        -> LevelWindow  (the best `depth` levels of Coinbase's full book)
   *        -> Mbp2Mbo      (levels -> chunk orders)
   *        -> frame::mda::msg::Data to the OB
   *
   * Runs its own thread, as BinanceFeed does. A sequence gap or a dropped
   * connection reconnects after a second; the new subscription starts with a
   * new snapshot, which Mbp2Mbo sends to OB as recovery records.
   *
   * With no OB it runs as a monitor, printing the top of the synthetic book
   * once a second (bnsim --exchange coinbase --dump).
   */
  class CoinbaseFeed : public actors::Actor
  {
  public:
    struct Config
    {
      std::string product;   // e.g. BTC-USD
      int depth = 1000;      // levels per side kept in the synthetic book
      Mbp2Mbo::Config chunks;
    };

    CoinbaseFeed(const Config &cfg, actors::Actor *ob);

    // A product's section of coinbase.ini (sim/CRYPTO.md).
    static Config config_from_ini(const boost::property_tree::ptree &section,
                                  const std::string &product, char venue);

  private:
    Config cfg_;
    actors::Actor *ob_;
    Mbp2Mbo mbp2mbo_;
    CoinbaseSync sync_;
    uint64_t records_ = 0;
    uint64_t last_dump_ns_ = 0;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;

    void emit(const bfile::l3_t &r);
    void run_connection();   // throws on error or a sequence gap
    void dump(uint64_t now_ns);
  };
}
