/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LmaxMdSync: one LMAX instrument's MarketDataSnapshotFullRefresh stream into
// sim::Mbp2Mbo, without the network. NOT CERTIFIED.
//
// LMAX sends the whole aggregated book (the best `depth` price levels a side,
// at most 20) in every W, and no incremental refresh (feed.md sections 4 and 6).
// Like sim::HyperliquidSync, each book is written into a LevelWindow and
// diffed against the previous one, so Mbp2Mbo sees only the levels that
// changed; the first book of a connection goes out as a snapshot (recovery
// records, which make OB clear its book first).
//
// Trades (MDEntryType 2) are the uncertain part (review.md, decisions 1 and 2):
//   - The spec does not say whether a W repeats the last trade until a new one
//     happens, or carries only trades since the previous W. Reading taken: a
//     trade entry identical in price, size, date and time to one in the
//     previous W of the same instrument is the same trade, and is not applied
//     again. The trades in the first W of a connection are not applied (they
//     may predate it).
//   - A trade entry has no aggressor side. Reading taken: a trade at or above
//     the previous book's best offer was a buy (the offer side was hit); at or
//     below the best bid, a sell; strictly inside the spread, unclassified and
//     not sent to Mbp2Mbo (counted).
//   A trade is applied before the book of the same W, so the book's levels,
//   which already reflect the trade, bring Mbp2Mbo's queue back to LMAX's size.
//
// There is no trading-status message: Mbp2Mbo::start sends status 17 once.
//
// After reset() the next W is preceded by an l3_chr_v2_t (book cleared) for
// this instrument. OB clears its book on the first recovery record only when
// it is not already in recovery, and it leaves recovery only on a
// non-recovery order record: a connection whose only W was the recovery
// snapshot would otherwise leave that snapshot's orders in OB when the next
// connection's snapshot arrives.

#include <cstdint>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

#include "lmax_md/MdParse.hpp"

namespace lmax::md
{
  class LmaxMdSync
  {
  public:
    struct Stats
    {
      uint64_t snapshots = 0;           // W applied
      uint64_t resyncs = 0;             // W sent as a Mbp2Mbo snapshot
      uint64_t trades_applied = 0;
      uint64_t trades_repeated = 0;     // same as in the previous W: not applied
      uint64_t trades_first_book = 0;   // in a connection's first W: not applied
      uint64_t trades_unclassified = 0; // inside the previous spread: not applied
      uint64_t crossed = 0;             // W whose best bid >= best offer
      uint64_t one_sided = 0;           // W with an empty side
      uint64_t bad_entries = 0;         // entry without price/size, or off the tick grid
      uint64_t wrong_instrument = 0;
    };

    using Levels = std::vector<std::pair<double, double>>; // price, size; best first

    using Emit = sim::Mbp2Mbo::Emit;

    // raw: where the book-clear record goes (the same place as Mbp2Mbo's
    // records); may be empty in tests that do not need it.
    LmaxMdSync(const Instrument &inst, sim::Mbp2Mbo &out, size_t depth = 20, Emit raw = {}, char venue = 0)
        : inst_(inst), out_(out), window_(out, depth), raw_(std::move(raw)), venue_(venue)
    {
    }

    // One W of this instrument. tx: the event time (SendingTime), ns.
    void on_snapshot(const MdSnapshot &s, uint64_t tx);

    // A new connection, or a book that may have missed messages: the next W
    // is sent as a snapshot.
    void reset()
    {
      if (synced_)
        clear_pending_ = true;
      synced_ = false;
    }

    bool synced() const { return synced_; }
    const Stats &stats() const { return st_; }
    const Instrument &instrument() const { return inst_; }
    // The last W's book as LMAX sent it, best first.
    const Levels &bids() const { return bids_; }
    const Levels &asks() const { return asks_; }

  private:
    using TradeKey = std::tuple<double, double, std::string, std::string>;

    Instrument inst_;
    sim::Mbp2Mbo &out_;
    sim::LevelWindow window_;
    Emit raw_;
    char venue_;
    bool synced_ = false;
    bool clear_pending_ = false;
    bool started_ = false;
    Levels bids_, asks_;
    std::set<TradeKey> last_trades_;
    Stats st_;
  };
}
