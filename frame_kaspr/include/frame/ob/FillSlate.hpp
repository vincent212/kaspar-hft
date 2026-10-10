#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

//
// FillSlate -- decides which fills in a CME MBO trade are RESTING fills and
// which one is the AGGRESSOR's own fill. Shared by TachBook and OB.
//
// WHY
//   An MBO trade record (bfile::l3_mbo_trd_v2_t) carries orderID and lastQty
//   only: no price, no side. Both come from the order's entry in the book.
//
//   CME lists EVERY filled order in the trade, the aggressor included. A NEW
//   aggressor is not in the book yet (its book record follows the trade), so
//   callers skip it. But an EXISTING order modified to cross is in the book at
//   its OLD price and side, because CME sends its Change AFTER the trade
//   records of the same transaction. Priced from the book, its fill prints at
//   the old limit -- outside the bid/offer, sometimes deep in the book -- and
//   its side says "resting buy, so the bid was hit" when the order actually
//   bought. Measured on the 2026-10-08 captures:
//     trade record before the order's book record: 340,161 of 340,164 (ZN)
//     ZN order 8419050632586: bid 104.6875 raised to 104.71875, bought 17 ->
//       printed as a resting bid filled at 104.6875.
//
//   So each fill is held ("slated") until its own order's book record arrives,
//   and released with a role:
//     RESTING    a real fill at the order's price; count it
//     AGGRESSOR  the incoming order's own fill; do not count or print it --
//                the resting orders it traded against carry their own fills
//     AMBIGUOUS  cannot be decided from the feed; callers keep the old
//                behaviour (count it). 1-vs-1, both orders at the touch, both
//                simply Deleted: ZN 0.19%, ES 0.54%, NQ 0.71% of fills. Both
//                prints are AT the bid or offer; the cost is one double-counted
//                volume with zero net flow.
//
// RULES, per transaction (one transactTime = one incoming order). Counted in
// distinct ORDERS, not fills: CME lists an order once per price level it
// trades at, so a modified bid sweeping two one-order levels is listed twice
// (1,044 such transactions on channel 314 2026-10-08).
//   all listed orders on one side       -> all RESTING (aggressor was new)
//   one side 1 order, other side >= 2   -> the lone order is the AGGRESSOR
//                                          (one incoming order per event, so a
//                                          side with several orders is resting)
//   both sides >= 2 orders              -> all RESTING (auction uncross; there
//                                          is no single aggressor)
//   1 vs 1, decided by the book records:
//     an order's Change moves its price -> that order is the AGGRESSOR
//     an order's Change keeps its price -> that order is RESTING (partly filled
//                                          where it rests), the other AGGRESSOR
//     neither Change decides, and exactly one order sits at the best
//     price on its side when the trade arrives -> that order is RESTING,
//     the other the AGGRESSOR (a resting order fills at the touch; a
//     modified aggressor's stored price is its old limit, often far off)
//     otherwise (both Deleted, both or neither at the touch) -> AMBIGUOUS,
//     known once both orders' records are in
//
//   The decision is taken at the transaction's first book record that settles
//   it, and is then FIXED for the rest of the transaction. Trade records that
//   arrive after book records of the same transaction would otherwise change
//   the counts and flip the verdict, crediting both sides; measured on the
//   2026-10-08 captures that happens in 3 of 87,215 ZN transactions, 0 of
//   259,493 NQ, 2 of 301,958 ES.
//
// USE
//   advance(tx): call for EVERY trade record (before the caller checks whether
//               the order is in its book). A later transaction closes out the
//               previous one, so held fills never wait for this instrument's
//               next book record.
//   on_trade(): for each trade record whose order IS in the book. Pass
//               at_touch = the order's stored price is the best price on its
//               side; the overload without it assumes true (no information).
//   on_book():  for each LIVE (recovery == 0) book record, BEFORE applying it,
//               with CME's OrderUpdateAction (0 New, 1 Change, 2 Delete).
//   flush():    end of data / shutdown / channel reset: release everything.
//   clear():    book clear: drop held fills without releasing.
//
//   Fills are released through the callback `release(const Fill&, FillRole)`.
//   When, is set per book:
//     Release::AT_DECISION    every fill of the transaction goes out at the
//                             record that decides it (usually the first book
//                             record after the trade records), and any later
//                             fill of a decided transaction at once. An
//                             undecided 1 vs 1 goes out, AMBIGUOUS, as soon as
//                             both orders' records are in. For a book whose
//                             prints and volume only depend on the verdict
//                             (TachBook): no fill waits longer than it must.
//     Release::AT_OWN_RECORD  each fill goes out at its own order's book
//                             record, BEFORE the caller applies it -- AMBIGUOUS
//                             if still undecided. For OB: an EXEC must be
//                             processed before its order's CANC/CANCD removes
//                             the order, or the EXEC finds no order.
//   Anything left goes out, in trade order, when the transaction closes.
//
//   Cost: O(1) per record via an orderID index that does not allocate once
//   warm (entries are erased one by one at close, never clear()ed); one pass
//   over the transaction's fills when it is decided and one when it closes.
//
// Side convention: the caller passes FillSlate::BUY or FillSlate::SEL for the
// order's STORED side.
//

#include <cstdint>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>
#include <boost/unordered/unordered_flat_map.hpp>

namespace frame::ob
{

  enum class FillRole : int8_t
  {
    RESTING,
    AGGRESSOR,
    AMBIGUOUS
  };

  inline const char *fill_role_name(FillRole r) noexcept
  {
    switch (r)
    {
    case FillRole::RESTING:   return "RESTING";
    case FillRole::AGGRESSOR: return "AGGRESSOR";
    case FillRole::AMBIGUOUS: return "AMBIGUOUS";
    }
    return "?";
  }

  enum class Release : int8_t
  {
    AT_DECISION,
    AT_OWN_RECORD
  };

  template <class Payload>
  class FillSlate
  {
  public:
    static constexpr int BUY = 0;
    static constexpr int SEL = 1;

    struct Fill
    {
      uint64_t oid;
      uint64_t tx;       // transactTime of the trade record
      double   px;       // the order's STORED price at the trade record
      uint32_t qty;      // lastQty
      int      side;     // BUY / SEL, the order's STORED side
      Payload  pl;       // caller's payload (e.g. the EXEC), released as-is
      int32_t  next;     // next fill of the same order in this transaction, -1 none
      bool     at_touch; // stored price was the best on its side at the trade
      bool     released;
    };

    explicit FillSlate(Release mode = Release::AT_DECISION) : mode_(mode)
    {
      fills_.reserve(64);
      idx_.reserve(64);
    }

    // diagnostics, cumulative
    uint64_t n_resting = 0;
    uint64_t n_aggressor = 0;
    uint64_t n_ambiguous = 0;

    template <class F>
    void advance(uint64_t tx, F &&release)
    {
      if (tx_open_ && tx != tx_)
        close_tx(release);
    }

    template <class F>
    void on_trade(uint64_t oid, uint64_t tx, double px, int side, uint32_t qty,
                  Payload pl, F &&release)
    {
      on_trade(oid, tx, px, side, qty, true, std::move(pl), std::forward<F>(release));
    }

    template <class F>
    void on_trade(uint64_t oid, uint64_t tx, double px, int side, uint32_t qty,
                  bool at_touch, Payload pl, F &&release)
    {
      advance(tx, release);
      if (!tx_open_)
      {
        tx_open_ = true;
        tx_ = tx;
      }
      const auto i = static_cast<int32_t>(fills_.size());
      fills_.push_back(Fill{oid, tx, px, qty, side, std::move(pl), -1, at_touch, false});
      ++held_;
      auto [it, is_new] = idx_.try_emplace(oid, Ord{i, i, false});
      if (is_new)
      {
        // a new ORDER on this side; a repeat listing of the same order
        // (one per price level it traded at) is not
        (side == BUY ? nb_ : ns_)++;
        (side == BUY ? buy_at_touch_ : sel_at_touch_) = at_touch;
      }
      else
      {
        fills_[it->second.last].next = i;
        it->second.last = i;
      }
      // A decided transaction (AT_DECISION) releases its later fills at once;
      // so does a fill whose order's record already came.
      if (aside_ != SIDE_UNKNOWN && (mode_ == Release::AT_DECISION || it->second.seen))
        release_one(fills_[i], role_for(fills_[i]), release);
    }

    // action: CME OrderUpdateAction -- 0 New, 1 Change, 2 Delete.
    template <class F>
    void on_book(uint64_t oid, uint64_t tx, int action, double px, F &&release)
    {
      if (!tx_open_)
        return;
      if (tx != tx_)
      {
        close_tx(release);
        return;
      }
      if (action != 1 && action != 2)
        return;
      auto it = idx_.find(oid);
      if (it == idx_.end() || it->second.seen)
        return;
      Ord &o = it->second;
      o.seen = true;
      ++seen_orders_;
      const Fill &first = fills_[o.first];
      if (action == 1)
        (same_px(px, first.px) ? kept_side_ : moved_side_) = first.side;
      if (aside_ == SIDE_UNKNOWN)
      {
        aside_ = compute_side();
        if (aside_ != SIDE_UNKNOWN)
        {
          // just decided
          for (auto &g : fills_)
            if (!g.released && (mode_ == Release::AT_DECISION || idx_.find(g.oid)->second.seen))
              release_one(g, role_for(g), release);
          return;
        }
        // still undecided (1 vs 1)
        if (mode_ == Release::AT_OWN_RECORD)
          release_order(o, FillRole::AMBIGUOUS, release);
        else if (seen_orders_ == nb_ + ns_)
          // every listed order's record is in: nothing more can decide it
          for (auto &g : fills_)
            if (!g.released)
              release_one(g, FillRole::AMBIGUOUS, release);
        return;
      }
      if (mode_ == Release::AT_OWN_RECORD)
        release_order(o, FillRole::AMBIGUOUS /* unused: decided */, release);
    }

    template <class F>
    void flush(F &&release) { close_tx(release); }

    void clear() noexcept
    {
      for (auto &f : fills_)
        idx_.erase(f.oid);
      fills_.clear();
      held_ = 0;
      reset_tx();
    }
    bool empty() const noexcept { return held_ == 0; }
    std::size_t size() const noexcept { return held_; }

  private:
    static constexpr int SIDE_NONE = -2;     // no aggressor among the listed orders
    static constexpr int SIDE_UNKNOWN = -1;  // 1 vs 1, not decided yet

    struct Ord
    {
      int32_t first, last;   // its fills in fills_, chained through Fill::next
      bool    seen;          // its book record has arrived
    };

    Release mode_;
    std::vector<Fill> fills_;   // this transaction's fills, in trade order
    boost::unordered_flat_map<uint64_t, Ord> idx_;   // this transaction's orders
    std::size_t held_ = 0;      // fills not released yet

    // Facts about the CURRENT transaction. Fixed for the whole transaction,
    // independent of which fills have already been released.
    bool        tx_open_ = false;
    uint64_t    tx_ = 0;
    std::size_t nb_ = 0, ns_ = 0;           // distinct orders listed on each side
    std::size_t seen_orders_ = 0;           // of those, with their book record in
    int         moved_side_ = SIDE_UNKNOWN; // side of an order whose Change moved its price
    int         kept_side_ = SIDE_UNKNOWN;  // side of an order whose Change kept its price
    bool        buy_at_touch_ = true;       // 1 vs 1: the BUY order was at the best bid
    bool        sel_at_touch_ = true;       // 1 vs 1: the SEL order was at the best offer
    int         aside_ = SIDE_UNKNOWN;      // the decision; once set, never changes

    void reset_tx() noexcept
    {
      tx_open_ = false;
      nb_ = ns_ = 0;
      seen_orders_ = 0;
      moved_side_ = kept_side_ = SIDE_UNKNOWN;
      buy_at_touch_ = sel_at_touch_ = true;
      aside_ = SIDE_UNKNOWN;
    }

    static bool same_px(double a, double b) noexcept
    {
      return std::fabs(a - b) <= 1e-9 * std::fmax(1.0, std::fmax(std::fabs(a), std::fabs(b)));
    }

    int compute_side() const noexcept
    {
      if (nb_ == 0 || ns_ == 0)
        return SIDE_NONE;          // one-sided: the aggressor was a new order
      if (nb_ >= 2 && ns_ >= 2)
        return SIDE_NONE;          // auction uncross: no single aggressor
      if (nb_ == 1 && ns_ >= 2)
        return BUY;
      if (ns_ == 1 && nb_ >= 2)
        return SEL;
      if (moved_side_ != SIDE_UNKNOWN) // 1 vs 1: the book records decide
        return moved_side_;
      if (kept_side_ != SIDE_UNKNOWN)
        return kept_side_ == BUY ? SEL : BUY;
      if (buy_at_touch_ != sel_at_touch_)   // the one off the touch moved: aggressor
        return buy_at_touch_ ? SEL : BUY;
      return SIDE_UNKNOWN;
    }

    FillRole role_for(const Fill &f) const noexcept
    {
      if (aside_ == SIDE_NONE)
        return FillRole::RESTING;
      if (aside_ == SIDE_UNKNOWN)
        return FillRole::AMBIGUOUS;
      return f.side == aside_ ? FillRole::AGGRESSOR : FillRole::RESTING;
    }

    template <class F>
    void release_one(Fill &f, FillRole r, F &release)
    {
      if (f.released)
        return;
      f.released = true;
      --held_;
      if (r == FillRole::RESTING)        ++n_resting;
      else if (r == FillRole::AGGRESSOR) ++n_aggressor;
      else                               ++n_ambiguous;
      release(static_cast<const Fill &>(f), r);
    }

    // Release all of one order's fills: with the decided role, or `undecided`.
    template <class F>
    void release_order(const Ord &o, FillRole undecided, F &release)
    {
      for (int32_t i = o.first; i >= 0; i = fills_[i].next)
        release_one(fills_[i], aside_ == SIDE_UNKNOWN ? undecided : role_for(fills_[i]), release);
    }

    // Release everything still held, in trade order, and start afresh.
    template <class F>
    void close_tx(F &release)
    {
      if (aside_ == SIDE_UNKNOWN)
        aside_ = compute_side();   // e.g. no book record came at all
      for (auto &f : fills_)
        if (!f.released)
          release_one(f, role_for(f), release);
      clear();
    }
  };

} // namespace frame::ob
