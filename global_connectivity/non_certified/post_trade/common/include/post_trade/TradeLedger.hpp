/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// TradeLedger: the trades of one trading day as the venue's post-trade reports
// describe them, applied idempotently.
//
// A plain class, not an actor: a post-trade client owns one and calls it only
// from its own actor thread, so it has no locking. It is the post-trade
// counterpart of frame::som::BrokerOrderBook, and is tested on its own.
//
// Reports may arrive more than once (a resend after a reconnect, a replay of
// the whole day at logon, the venue's daily replay) and, after a reconnect, out
// of order. apply() makes every one of these cases converge on the same state:
//
//   - a report whose report_id was applied before is a Duplicate and changes
//     nothing;
//   - New adds the trade;
//   - Amend replaces the trade's economics, if it is newer (report_seq) than
//     what the ledger holds; an amendment of a trade not yet seen adds it, so
//     its New, arriving later, is Stale;
//   - Cancel and Bust remove the trade and leave a tombstone, so that a late
//     New or Amend of a removed trade is Stale rather than a resurrection; a
//     removal of a trade not yet seen leaves the tombstone only (Orphan);
//   - a report_seq of 0 means the venue gives no order: arrival order is used.
//
// The ledger is rebuilt from a replay, never added to: replaying a day into a
// ledger that already holds it changes nothing, and replaying it into an empty
// one (after a restart) gives the same trades.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "post_trade/msg/TradeCapture.hpp"

namespace post_trade
{
  enum class Applied
  {
    Added,     // a new trade (or an amendment of a trade not yet seen)
    Amended,   // the trade's economics were replaced
    Removed,   // cancelled or busted
    Duplicate, // this report_id was applied before
    Stale,     // older than what the ledger holds for the trade, or the trade was removed
    Orphan,    // cancel or bust of a trade not yet seen: tombstone only
    Invalid,   // the report does not fit (no trade id, a second New for a live trade)
  };
  const char *to_string(Applied a);

  struct ApplyResult
  {
    Applied what = Applied::Invalid;
    // Added, Amended, Removed: the event to pass downstream, as a complete
    // record. For a removal whose report leaves out the economics (quantity,
    // side, symbol, price), they are taken from the trade it removes.
    msg::TradeCapture out;
    bool emit() const
    {
      return what == Applied::Added || what == Applied::Amended || what == Applied::Removed;
    }
  };

  class TradeLedger
  {
  public:
    ApplyResult apply(const msg::TradeCapture &r);

    // The live trade with this id, or nullptr (unknown or removed).
    const msg::TradeCapture *find(const std::string &trade_id) const;
    bool removed(const std::string &trade_id) const;
    size_t live() const;
    size_t reports() const { return seen_.size(); }

    // Net signed quantity of the live trades in a venue symbol.
    int64_t net_qty(const std::string &symbol) const;

    // Forget everything: call at the start of a new trading day (a new
    // venue session), never on a reconnect within the day.
    void clear();

  private:
    struct State
    {
      msg::TradeCapture trade; // current economics (live) or last known (removed)
      uint64_t seq = 0;        // report_seq last applied
      uint64_t arrival = 0;    // arrival counter of the last applied report
      bool live = false;
    };

    // true when r (seq, arrival) is newer than what s holds
    bool newer(const msg::TradeCapture &r, const State &s) const;

    std::unordered_map<std::string, State> trades_;
    std::unordered_set<std::string> seen_; // report ids applied
    uint64_t arrivals_ = 0;
  };
}
