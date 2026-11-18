#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitSync: Bybit V5 public market data (one symbol of one category) into
// Kaspar records. NOT CERTIFIED. No network: the feed actor, the recorder's
// replay and the verify tool hand it parsed frames and REST snapshots.
//
// Two book modes (feed.md section 5):
//
//   FULL   topic orderbook.full.{symbol}: deltas only, every 200 ms, the whole
//          book. The book is initialised from GET /v5/market/full_orderbook,
//          whose (u, seq, ts) is that of one of the stream's deltas, so the
//          alignment is exact:
//            - deltas are buffered until a snapshot is applied;
//            - a snapshot with update id L contains every delta with u <= L;
//              the first delta applied after it must have u == L + 1;
//            - a buffered delta with u == L must carry the snapshot's seq,
//              or the snapshot is from another update stream (refetch);
//            - the oldest buffered delta having u > L + 1 means the snapshot
//              is older than the stream: refetch;
//            - afterwards u must be the previous u + 1; u <= previous is a
//              duplicate and skipped; u > previous + 1 is a gap: a GAP record,
//              then a new snapshot; u == 1 is Bybit's reset (restart, delist,
//              auction start or end, tick or lot change): a new snapshot.
//   DEPTH  topic orderbook.{N}.{symbol} (N = 1, 50, 200, 1000) and the public
//          archive (orderbook.200): a snapshot message, then deltas whose u
//          is the previous u + 1. Any snapshot message replaces the book. A
//          gap cannot be repaired from REST at the same depth: on_message
//          returns false and the caller resubscribes, which yields a snapshot.
//
// In both modes quantities are absolute; "0" removes the level.
//
// Trades (publicTrade.{symbol}): S is the TAKER's side, so S == "Sell" means
// the bid was hit. Trades are applied only while the book is synchronised.
// Each one gives a print (l3_mbp_trd_t); trades flagged RPI (against Retail
// Price Improvement orders, which are not in the public book) or BT (block
// trades, off book) give the print only and take nothing from the book.
//
// Output: the whole venue book is kept here (book() and the verify tool);
// the best `window` levels per side go through sim::LevelWindow to
// sim::Mbp2Mbo, which cuts them into chunk orders for OB. Prints, GAP and
// CHR records are emitted directly through `direct`.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace bybit
{
  class BybitSync
  {
  public:
    enum class Mode
    {
      FULL,
      DEPTH
    };

    enum class SnapResult
    {
      Applied,     // the book is synchronised from this snapshot
      TooOld,      // the stream has moved past it: fetch another
      SeqMismatch, // a delta with the snapshot's u carries another seq: fetch another
      Ignored,     // not waiting for a snapshot (DEPTH mode, or already synced)
    };

    struct Config
    {
      Mode mode = Mode::FULL;
      std::string symbol;       // e.g. BTCUSDT
      double tick = 0;          // price increment (instruments-info priceFilter.tickSize)
      double print_unit = 0;    // quantity of one unit in a print's sz (qtyStep / basePrecision)
      size_t window = 1000;     // levels per side passed to Mbp2Mbo
      int32_t sec_id = 0;       // Kaspar asset id
      char venue = 0;           // en::x::BYBIT (spot) or en::x::BYBITF (linear)
    };

    struct Stats
    {
      uint64_t deltas = 0;         // book deltas applied
      uint64_t snapshots = 0;      // snapshots applied (REST in FULL, WS in DEPTH)
      uint64_t trades = 0;         // trades applied to the book
      uint64_t prints_only = 0;    // RPI or block trades: print only
      uint64_t trades_unsynced = 0;// trades dropped while not synchronised
      uint64_t gaps = 0;           // u jumped
      uint64_t resets = 0;         // u == 1
      uint64_t duplicates = 0;     // u <= previous: skipped
      uint64_t seq_back = 0;       // seq lower than the previous delta's: skipped
      uint64_t too_old = 0;        // snapshots refused: older than the stream
      uint64_t seq_mismatch = 0;   // snapshots refused: seq differs at equal u
      uint64_t crossed = 0;        // book events after which best bid >= best ask
      uint64_t empty_resets = 0;   // resets to an empty book (CHR sent)
      size_t max_levels_bid = 0, max_levels_ask = 0;
    };

    using Book = std::map<int64_t, double>; // ticks -> quantity
    using Emit = std::function<void(const bfile::l3_t &)>;

    // Observers, for the verify tool. before_level is called with the
    // quantity a level held before each change (0: absent); after_event with
    // the update id once a delta or snapshot is applied.
    std::function<void(bool bid, int64_t ticks, double before)> before_level;
    std::function<void(int64_t u, bool snapshot)> after_event;

    BybitSync(const Config &cfg, sim::Mbp2Mbo &out, Emit direct);

    // One WebSocket frame, parsed. Frames other than this symbol's book and
    // trade topics (subscription acks, pongs) are ignored. false: DEPTH mode
    // gap; resubscribe.
    bool on_message(const nlohmann::json &msg);

    // The "result" object of GET /v5/market/full_orderbook (FULL mode).
    SnapResult on_snapshot(const nlohmann::json &result);

    // A new connection: forget the stream position, wait for a snapshot.
    // Sends a GAP record if the book was synchronised.
    void reset();

    bool synced() const { return synced_; }
    bool need_snapshot() const { return cfg_.mode == Mode::FULL && !synced_; }
    bool buffering() const { return !buffer_.empty(); }
    int64_t last_u() const { return prev_u_; }
    int64_t last_seq() const { return prev_seq_; }
    uint64_t last_cts_ms() const { return last_cts_ms_; }
    const Book &bids() const { return bids_; }
    const Book &asks() const { return asks_; }
    const Stats &stats() const { return stats_; }
    const Config &config() const { return cfg_; }
    int64_t ticks(double px) const;

    static double num(const nlohmann::json &v);

  private:
    Config cfg_;
    sim::Mbp2Mbo &out_;
    Emit direct_;
    sim::LevelWindow window_;
    Book bids_, asks_;
    std::deque<nlohmann::json> buffer_; // FULL mode: deltas waiting for a snapshot
    bool synced_ = false;
    bool started_ = false;
    int64_t prev_u_ = -1;
    int64_t prev_seq_ = -1;
    uint64_t last_cts_ms_ = 0;
    Stats stats_;

    void on_book(const nlohmann::json &msg);
    void on_trades(const nlohmann::json &msg);
    bool apply_delta(const nlohmann::json &msg); // false: gap
    void apply_levels(const nlohmann::json &data, bool snapshot);
    void replace_book(const nlohmann::json &data, uint64_t tx);
    void set_level(bool bid, double px, double qty);
    void after_book(int64_t u, bool snapshot);
    void gap(const nlohmann::json &msg, const char *why);
    void emit_gap();
    void emit_clear();
  };
}
