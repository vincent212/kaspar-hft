#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace kraken
{
  /**
   * KrakenBookSync -- Kraken spot WebSocket v2 `book` (price levels) and
   * `trade` channels for one symbol, into Mbp2Mbo (feed.md section 5). No
   * network: the feed actor, the verify tool and the tests hand it each frame,
   * parsed with kraken::parse_raw so that decimal text is kept.
   *
   *   book snapshot   replaces the book (Mbp2Mbo snapshot of the window)
   *   book update     absolute quantities; qty 0 removes the level; several
   *                   updates of one level in one message are applied in order
   *   after each message the book is truncated to the subscribed depth (Kraken
   *                   sends no qty 0 for a level pushed out of scope), then
   *                   the CRC32 `checksum` is checked against the top 10 levels
   *   trade update    `side` is the taker's: "sell" hit the bid. trade_id is
   *                   checked for continuity (gaps are counted, not recovered)
   *
   * There is no sequence number on `book`. The checksum is the only integrity
   * check: a mismatch returns ChecksumMismatch and the caller resubscribes
   * (the feed reconnects), which brings a new snapshot.
   *
   * Whenever a synchronised book is invalidated (a checksum mismatch, an
   * unreadable frame, reset() after a lost connection) on_gap is called once:
   * the owner sends l3_gap_v2_t to the OB, whose book is stale until the next
   * snapshot reaches it as recovery records.
   *
   * out may be null: the sync then only maintains and checks its own book.
   */
  class KrakenBookSync
  {
  public:
    enum class Result
    {
      Ok,               // applied (or not ours)
      ChecksumMismatch, // book applied, checksum differs: resubscribe
      Error,            // a frame the sync could not read: resubscribe
    };

    struct Stats
    {
      uint64_t snapshots = 0, updates = 0, levels_applied = 0;
      uint64_t checksum_ok = 0, checksum_bad = 0, checksum_missing = 0;
      uint64_t truncated = 0;         // levels dropped below the subscribed depth
      uint64_t crossed = 0;           // updates after which best bid >= best ask
      uint64_t trades = 0, trade_id_gaps = 0, trade_id_missing = 0, trades_before_sync = 0;
      uint64_t errors = 0;
      uint64_t heartbeats = 0;
    };

    struct Level
    {
      std::string px, qty; // text as sent
      int64_t qty_fp = 0;  // fixed point, kraken::kScale decimals
    };
    using Side = std::map<int64_t, Level>; // key: price, fixed point

    // depth: the depth subscribed (10, 25, 100, 500 or 1000). window: levels
    // per side passed to Mbp2Mbo.
    KrakenBookSync(std::string symbol, size_t depth, sim::Mbp2Mbo *out = nullptr, size_t window = 0);

    Result on_message(const nlohmann::json &msg);

    // A new connection: wait for a snapshot (on_gap if the book was synced).
    void reset();

    // Called once each time a synchronised book is invalidated.
    void on_gap(std::function<void()> f) { on_gap_ = std::move(f); }

    bool synced() const { return synced_; }
    const Stats &stats() const { return st_; }
    const Side &bids() const { return bids_; }
    const Side &asks() const { return asks_; }
    uint64_t last_book_time() const { return last_book_tx_; }
    uint64_t last_trade_id() const { return last_trade_id_; }

    // Checksum of the current book, by the documented algorithm.
    uint32_t checksum() const;
    static uint32_t checksum(const Side &bids, const Side &asks);

    // The best n levels: (price, qty) fixed point, best first.
    std::vector<std::pair<int64_t, int64_t>> top(bool bid, size_t n) const;

  private:
    std::string symbol_;
    size_t depth_;
    sim::Mbp2Mbo *out_;
    std::unique_ptr<sim::LevelWindow> window_;
    Side bids_, asks_; // asks ascending; bids iterated from rbegin
    bool synced_ = false;
    bool started_ = false;
    uint64_t last_book_tx_ = 0;
    uint64_t last_trade_id_ = 0;
    Stats st_;
    std::function<void()> on_gap_;

    void invalidate();
    Result on_book(const nlohmann::json &d, bool snapshot);
    void on_trade(const nlohmann::json &t);
    void set_level(bool bid, const nlohmann::json &e);
    void truncate(bool bid);
  };
}
