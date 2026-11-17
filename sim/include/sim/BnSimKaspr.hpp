#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "actors/act/Group.hpp"
#include "actors/act/Manager.hpp"
#include "light/if/PCoord.hpp"
#include "light/if/QCoord.hpp"
#include "logger/act/Logger.hpp"
#include "sim/act/BinanceFeed.hpp"
#include "sim/act/CoinbaseFeed.hpp"
#include "sim/act/HyperliquidFeed.hpp"

namespace sim
{
  struct SlippageProbe;

  /**
   * Live crypto simulator: the replay simulator's books, lights and order
   * manager, fed live from Binance, Coinbase or Hyperliquid instead of from a
   * capture.
   *
   *   <Exchange>Feed (own thread) -> OB -> light22 (shadow) -> SOM (sim fills)
   *                                ^                         |
   *                             Timer (market time)       PositionManager
   *                                                       SlippageProbe
   *
   * One symbol per process. Market time is the exchange's event time, so OB's
   * latency model and the probe's windows run on the exchange's clock.
   *
   * The lights and the PositionManager exist only with the probe, which is
   * what gives the lights a position to work.
   *
   * With mqport set, MTD takes orders and serves the BBO and fills over zmq
   * (MQ0 -> CONS -> MTD), as in kaspr; sim/binance/kaspar_client.py is a
   * Python client for it.
   */
  struct BnSimKaspr : public actors::Manager
  {
    struct Options
    {
      std::string exchange = "binance";   // binance, coinbase or hyperliquid
      std::string config_dir;      // <exchange>.ini, lights.ini, som.ini
      std::string symbol;          // a section of <exchange>.ini: BTCUSDT, BTC-USD, BTC
      bool futures = false;        // Binance USD-M futures instead of spot
      int probe_size = 0;          // chunks per leg; 0 = no probe, so no orders
      int probe_window_min = 10;   // minimum window length
      int probe_days = 7;          // how long the probe runs from start
      std::string probe_out;
      int ob_delay_us = -1;        // -1 = OB's default
      int ob_feed_delay_us = 0;
      uint32_t rng_seed = 0;
      int mqport = 7777;           // MQ0 (zmq) console port; 0 = none
    };

    /**
     * The exchange-specific part, read from <config_dir>/<exchange>.ini: the
     * venue, the chunking, the OB ladder, and the feed actor to build.
     */
    struct FeedSpec
    {
      std::string exchange;
      en::x venue = en::x::BINANCE;
      std::string asset_name;   // BTCUSDT, BTCUSDT_F, BTC-USD, BTC
      int ladder = 1 << 19;
      Mbp2Mbo::Config chunks;
      BinanceFeed::Config binance;
      CoinbaseFeed::Config coinbase;
      HyperliquidFeed::Config hyperliquid;

      static FeedSpec from_config(const Options &opt);
      // The feed actor, sending to `ob` (nullptr: the --dump monitor).
      actors::Actor *make_feed(actors::Actor *ob) const;
    };

    explicit BnSimKaspr(const Options &opt);
    ~BnSimKaspr() override = default;

  private:
    Options opt_;
    FeedSpec spec_;
    en::x venue_;
    uint asset_id_ = 0;

    actors::Group *group_ = nullptr;
    polonaise::logger::act::Logger *logger_ = nullptr;
    SlippageProbe *probe_ = nullptr;
    std::vector<std::vector<actor_ptr>> order_books_;   // [venue][asset id], as SOM wants
    actor_ptr ob_ = nullptr;
    actor_ptr timer_ = nullptr;
    actor_ptr som_ = nullptr;
    actor_ptr feed_ = nullptr;
    std::vector<actor_ptr> buy_lights_, sel_lights_;
    std::map<std::string, light::PCoord *> pcoord_map_;
    light::PCoord *pcoord_buy_ = nullptr, *pcoord_sel_ = nullptr;

    void load_config();
    void create_order_book();
    void create_probe();
    void create_som();
    void create_lights();
    void create_feed();
    void create_console();
  };
}
