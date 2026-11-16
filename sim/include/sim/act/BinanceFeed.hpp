#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <mutex>
#include <string>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "sim/BinanceSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace sim
{
  /**
   * BinanceFeed -- live Binance market data for one symbol, as MBO records to
   * that symbol's OB.
   *
   *   WebSocket (TLS)  spot:    <symbol>@depth@100ms + <symbol>@trade, one connection
   *                    futures: <symbol>@depth@100ms on /public,
   *                             <symbol>@aggTrade on /market, a second connection
   *   REST (TLS)       depth snapshot, at start and after a gap
   *        -> BinanceSync (Binance's local-book procedure)
   *        -> Mbp2Mbo     (levels -> chunk orders)
   *        -> frame::mda::msg::Data to the OB
   *
   * Runs its own thread, like mcast_recv's SocketReader: Start sends a
   * Continue to itself and the Continue handler is the read loop, which only
   * returns at shutdown. A dropped connection (Binance closes every connection
   * after 24 hours) is reopened after a second, with a new snapshot.
   *
   * With no OB it runs as a monitor: once a second it prints the top of the
   * synthetic book (sim/binance --dump).
   */
  class BinanceFeed : public actors::Actor
  {
  public:
    struct Config
    {
      BinanceSync::Market market = BinanceSync::Market::SPOT;
      std::string symbol;            // e.g. BTCUSDT
      int depth_limit = 1000;        // REST snapshot levels
      Mbp2Mbo::Config chunks;
    };

    BinanceFeed(const Config &cfg, actors::Actor *ob);

  private:
    Config cfg_;
    actors::Actor *ob_;
    Mbp2Mbo mbp2mbo_;
    BinanceSync sync_;
    std::mutex mtx_;   // sync_ and mbp2mbo_, when trades have their own connection
    uint64_t records_ = 0;
    uint64_t last_dump_ns_ = 0;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;

    void emit(const bfile::l3_t &r);
    void run_connection(const std::string &target, bool book);   // throws on error
    std::string fetch_snapshot();
    void dump(uint64_t now_ns);
  };
}
