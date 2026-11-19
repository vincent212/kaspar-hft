#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitBookSync: Deribit's public JSON-RPC order book and trades for one
// instrument, into Kaspar records through sim::LevelWindow and sim::Mbp2Mbo.
// NOT CERTIFIED. No network: the feed actor (or a test, or the live check)
// hands it each parsed WebSocket message.
//
//   book.<instrument>.<interval>    the first notification is the whole book
//                                   (every level, no depth limit); each later
//                                   one lists [action, price, amount] with
//                                   action new / change / delete. Amounts are
//                                   absolute. Every notification carries
//                                   change_id; every one after the first carries
//                                   prev_change_id, which must equal the
//                                   previous change_id (feed.md 5).
//   trades.<instrument>.<interval>  public trades; `direction` is the TAKER's
//                                   side (feed.md 6), so a "sell" hit the bid.
//
// The chain is per subscription. A prev_change_id that does not match is a
// gap: on_message returns false and the caller resubscribes, which brings a
// new full snapshot. Whenever a synchronised book is invalidated (that gap,
// or reset() after a lost connection) on_gap is called once: the owner sends
// l3_gap_v2_t to the OB, whose book is stale until the next snapshot reaches
// it. The class keeps the venue's full book itself (for the checks and the
// REST comparison); LevelWindow passes only the best `depth` levels to
// Mbp2Mbo.
//
// Quantities: for perpetuals and futures `amount` is USD; Mbp2Mbo divides it
// into chunks of Config::chunk USD.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace deribit
{
  class DeribitBookSync
  {
  public:
    // `out` may be null: the class then only keeps the venue's book (the
    // live check uses this to verify the sync rule on its own).
    DeribitBookSync(std::string instrument, double tick, sim::Mbp2Mbo *out, size_t depth);

    // Called before a snapshot that replaces a book already sent to Mbp2Mbo
    // (a resubscription). The owner sends l3_chr_v2_t to the OB: Mbp2Mbo's
    // recovery records clear OB only when OB is not still in recovery from
    // the previous snapshot, which it is when no live record came between the
    // two (seen in a recording of two subscriptions; feed.md 10).
    void on_resync(std::function<void(uint64_t tx_ns)> f) { on_resync_ = std::move(f); }

    // Called once each time a synchronised book is invalidated.
    void on_gap(std::function<void()> f) { on_gap_ = std::move(f); }

    // One WebSocket message (any: responses and other channels are ignored).
    // false: a change_id gap; resubscribe.
    bool on_message(const nlohmann::json &msg);

    // A new subscription: wait for its snapshot (on_gap if the book was
    // synced).
    void reset();

    // The venue's book, price in ticks -> amount.
    using Side = std::map<int64_t, double>;
    const Side &bids() const { return bids_; }
    const Side &asks() const { return asks_; }
    int64_t ticks(double px) const;

    bool synced() const { return synced_; }
    int64_t change_id() const { return last_change_id_; }

    // The book's best `n` levels per side after each notification, kept for
    // the last `keep` notifications, so that a REST snapshot carrying a
    // change_id can be compared with the book at exactly that change_id.
    struct Top
    {
      int64_t change_id = 0;
      uint64_t ts_ms = 0;
      std::vector<std::pair<int64_t, double>> bids, asks;
    };
    void keep_history(size_t n_levels, size_t keep)
    {
      hist_levels_ = n_levels;
      hist_keep_ = keep;
    }
    const Top *at_change_id(int64_t id) const;
    // The kept books just before and just after change_id `id` (the last
    // with change_id <= id, the first with change_id > id); null where none.
    std::pair<const Top *, const Top *> around(int64_t id) const;
    Top top(size_t n) const;

    struct Stats
    {
      uint64_t snapshots = 0;       // full books applied (first, and after each resync)
      uint64_t notifications = 0;   // change notifications applied
      uint64_t level_updates = 0;   // [action, price, amount] entries applied
      uint64_t gaps = 0;            // prev_change_id mismatches
      uint64_t bad_actions = 0;     // "new" on an existing level, "change"/"delete" of a missing one
      uint64_t crossed = 0;         // notifications after which best bid >= best ask
      uint64_t trades = 0;          // trades passed on
      uint64_t trades_skipped = 0;  // before the first snapshot, or not this instrument
      uint64_t unknown_action = 0;
    };
    const Stats &stats() const { return st_; }

    // Parse a notification's level list into (bid?, price, amount, action).
    struct LevelUpdate
    {
      std::string action;
      double px = 0, amount = 0;
    };

  private:
    std::string instrument_;
    std::string book_prefix_, trades_prefix_;
    double tick_;
    sim::Mbp2Mbo *out_;
    std::unique_ptr<sim::LevelWindow> window_;
    Side bids_, asks_;
    bool synced_ = false;
    bool started_ = false;
    int64_t last_change_id_ = -1;
    Stats st_;
    size_t hist_levels_ = 0, hist_keep_ = 0;
    std::deque<Top> hist_;
    std::function<void(uint64_t)> on_resync_;
    std::function<void()> on_gap_;

    void invalidate();
    void on_book(const nlohmann::json &data);
    void on_trades(const nlohmann::json &data);
    void apply_side(bool bid, const nlohmann::json &levels, bool snapshot);
    void remember(uint64_t ts_ms);
  };
}
