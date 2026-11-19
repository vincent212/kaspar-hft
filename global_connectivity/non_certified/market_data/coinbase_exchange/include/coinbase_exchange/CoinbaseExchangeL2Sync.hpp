#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <map>
#include <memory>

#include <nlohmann/json.hpp>

#include "coinbase_exchange/CoinbaseExchangeSync.hpp"
#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace coinbase_exchange
{
  /**
   * CoinbaseExchangeL2Sync -- the price-level book from the channels that need
   * no account: `level2_batch` (the whole book as a `snapshot`, then
   * `l2update` batches every 50 ms with absolute sizes, "0" removing a level)
   * and `matches` (trades; `side` is the maker's). It feeds sim::Mbp2Mbo
   * through sim::LevelWindow, as the Advanced Trade feed does, and keeps the
   * whole book for the comparisons.
   *
   * This is the fallback when no API key is configured; with a key the L3
   * CoinbaseExchangeSync is used instead. level2_batch messages carry no
   * sequence number, so a dropped message cannot be detected on the book;
   * the `matches` trade_id (consecutive per product) and the heartbeat's
   * last_trade_id show dropped trades, which are counted.
   */
  class CoinbaseExchangeL2Sync
  {
  public:
    struct Stats
    {
      uint64_t snapshots = 0, updates = 0, level_changes = 0, crossed_updates = 0;
      uint64_t trades = 0, trade_dups = 0, trades_missing = 0, bad_message = 0;
    };

    // `depth` levels per side reach Mbp2Mbo (LevelWindow). `chunk` is
    // Mbp2Mbo's synthetic order size, in the base currency.
    CoinbaseExchangeL2Sync(const ProductSpec &spec, sim::Mbp2Mbo &out, size_t depth);

    void on_message(const nlohmann::json &m, uint64_t recv_ns);
    void reset() { synced_ = false; }

    bool synced() const { return synced_; }
    const Stats &stats() const { return st_; }
    // tick -> size in base increments
    const std::map<int64_t, int64_t> &side(bool bid) const { return bid ? bids_ : asks_; }
    int64_t best(bool bid) const;
    uint64_t book_time() const { return book_time_; }   // venue time of the last book message
    uint64_t book_recv() const { return book_recv_; }   // when it was received
    int64_t last_trade_id() const { return last_trade_; }
    int64_t heartbeat_last_trade_id() const { return hb_last_trade_; }

  private:
    ProductSpec spec_;
    sim::Mbp2Mbo &out_;
    sim::LevelWindow window_;
    std::map<int64_t, int64_t> bids_, asks_;
    bool synced_ = false, started_ = false;
    int64_t last_trade_ = -1, hb_last_trade_ = -1;
    uint64_t book_time_ = 0, book_recv_ = 0;
    Stats st_;

    double px(int64_t ticks) const;
    double qty(int64_t size) const;
  };
}
