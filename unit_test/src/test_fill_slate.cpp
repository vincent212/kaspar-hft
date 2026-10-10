/*
 * Copyright (c) 2026 M2 Tech (16425640 Canada Inc.). All rights reserved.
 *
 * PROPRIETARY AND CONFIDENTIAL — TRADE SECRET.
 *
 * This file contains unpublished proprietary source code of
 * M2 Tech (16425640 Canada Inc.) and constitutes a trade secret.
 * No license, express or implied, is granted. Unauthorized copying,
 * use, distribution, modification, reverse engineering, or disclosure,
 * in whole or in part, is strictly prohibited and will be prosecuted
 * to the fullest extent permitted by law.
 */

//
// FillSlate: which fills listed in a CME MBO trade are resting fills, and
// which is the aggressor's own fill. See frame/ob/FillSlate.hpp.
//
// Scenarios marked "ZN capture" replay real records from
// 344.20261008.1791493630571958019.21.7.1.bin (ZNZ6, secid 42001080) in the
// order they arrived on the wire: all trade records of a transaction first,
// then that transaction's book records.
//

#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include "frame/ob/FillSlate.hpp"

using frame::ob::FillRole;
using Slate = frame::ob::FillSlate<int>;   // payload: an id we can check

namespace
{
  constexpr int BUY = Slate::BUY;
  constexpr int SEL = Slate::SEL;
  constexpr int NEW = 0, CHANGE = 1, DELETE = 2;

  struct Released
  {
    uint64_t oid;
    FillRole role;
    double px;
    uint32_t qty;
    int side;
    int payload;
  };

  // Collects every released fill, and the hit/tak a book would publish:
  // resting BUY filled -> the bid was hit; resting SEL filled -> offer taken.
  // AMBIGUOUS is counted, as callers keep the old behaviour for it.
  struct Sink
  {
    std::vector<Released> out;
    uint64_t hit = 0, tak = 0;
    auto fn()
    {
      return [this](const Slate::Fill &f, FillRole r) {
        out.push_back({f.oid, r, f.px, f.qty, f.side, f.pl});
        if (r == FillRole::AGGRESSOR)
          return;
        (f.side == BUY ? hit : tak) += f.qty;
      };
    }
    const Released *find(uint64_t oid) const
    {
      for (const auto &r : out)
        if (r.oid == oid)
          return &r;
      return nullptr;
    }
  };
} // namespace

// ---------------------------------------------------------------------------
// The bug: an existing bid modified up to the offer, lifting four offers.
// ZN capture, transaction 1791505471758112103, order 8419050632586.
//   NEW BUY 104.6875 x127 (rested 15.9 s)
//   TRADE 8419050632586 x17          <- the aggressor, still a BUY @104.6875
//   TRADE 4 offers @104.71875 x14,1,1,1
//   CHANGE 8419050632586 BUY 104.71875 x49   <- arrives AFTER the trades
//   DELETE the 4 offers
// Before the fix TachBook2 published hit=17 tak=17 (signed flow 0) and an
// EXEC at 104.6875. Correct: tak 17, hit 0, nothing printed at 104.6875.
// ---------------------------------------------------------------------------
TEST(FillSlate, ModifiedAggressorPartialFill_ZN_8419050632586)
{
  Slate s;
  Sink k;
  const uint64_t tx = 1791505471758112103ULL;
  s.on_trade(8419050632586ULL, tx, 104.6875, BUY, 17, 1, k.fn());
  s.on_trade(8419050632915ULL, tx, 104.71875, SEL, 14, 2, k.fn());
  s.on_trade(8419050632930ULL, tx, 104.71875, SEL, 1, 3, k.fn());
  s.on_trade(8419050633353ULL, tx, 104.71875, SEL, 1, 4, k.fn());
  s.on_trade(8419050633438ULL, tx, 104.71875, SEL, 1, 5, k.fn());
  EXPECT_TRUE(k.out.empty()) << "nothing may be released at the trade record";

  s.on_book(8419050632586ULL, tx, CHANGE, 104.71875, k.fn());
  s.on_book(8419050632915ULL, tx, DELETE, 104.71875, k.fn());
  s.on_book(8419050632930ULL, tx, DELETE, 104.71875, k.fn());
  s.on_book(8419050633353ULL, tx, DELETE, 104.71875, k.fn());
  s.on_book(8419050633438ULL, tx, DELETE, 104.71875, k.fn());

  ASSERT_EQ(k.out.size(), 5u);
  ASSERT_NE(k.find(8419050632586ULL), nullptr);
  EXPECT_EQ(k.find(8419050632586ULL)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.hit, 0u);
  EXPECT_EQ(k.tak, 17u);
  for (const auto &r : k.out)
  {
    if (r.role == FillRole::RESTING)
    {
      EXPECT_DOUBLE_EQ(r.px, 104.71875) << "every counted fill is at the real trade price";
    }
  }
  EXPECT_TRUE(s.empty());
}

// Same transaction, but the aggressor's Change arrives LAST. The lone-order
// rule (one incoming order per transaction) must still identify it.
TEST(FillSlate, ModifiedAggressorChangeArrivesLast)
{
  Slate s;
  Sink k;
  const uint64_t tx = 100;
  s.on_trade(1, tx, 104.6875, BUY, 17, 1, k.fn());
  s.on_trade(2, tx, 104.71875, SEL, 14, 2, k.fn());
  s.on_trade(3, tx, 104.71875, SEL, 3, 3, k.fn());
  s.on_book(2, tx, DELETE, 104.71875, k.fn());
  s.on_book(3, tx, DELETE, 104.71875, k.fn());
  EXPECT_EQ(k.tak, 17u) << "resting fills go out at their own records";
  EXPECT_EQ(k.hit, 0u);
  EXPECT_EQ(k.find(1), nullptr) << "aggressor not released before its own record";
  s.on_book(1, tx, CHANGE, 104.71875, k.fn());
  ASSERT_NE(k.find(1), nullptr);
  EXPECT_EQ(k.find(1)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.hit, 0u);
}

// ---------------------------------------------------------------------------
// Modified aggressor that fills COMPLETELY: no Change ever arrives, only a
// Delete at its OLD price. 1 vs 1, so the other order's record decides.
// ZN capture, transaction 1791496878091999311, order 8419050415013.
//   NEW SEL 104.71875 x3 (rested 8.55 s at the offer)
//   TRADE 8419050415013 x3      <- still a SEL @104.71875 in the book
//   TRADE 8419050358635 x3      <- resting BUY @104.703125
//   CHANGE 8419050358635 BUY 104.703125 x135   <- same price: resting
//   DELETE 8419050415013 SEL 104.71875 x3      <- old price, filled out
// The sell came down to the bid and sold 3. Correct: hit 3, tak 0.
// ---------------------------------------------------------------------------
TEST(FillSlate, ModifiedAggressorFullFill_ZN_8419050415013)
{
  Slate s;
  Sink k;
  const uint64_t tx = 1791496878091999311ULL;
  s.on_trade(8419050415013ULL, tx, 104.71875, SEL, 3, 1, k.fn());
  s.on_trade(8419050358635ULL, tx, 104.703125, BUY, 3, 2, k.fn());
  s.on_book(8419050358635ULL, tx, CHANGE, 104.703125, k.fn());
  s.on_book(8419050415013ULL, tx, DELETE, 104.71875, k.fn());

  ASSERT_EQ(k.out.size(), 2u);
  EXPECT_EQ(k.find(8419050358635ULL)->role, FillRole::RESTING);
  EXPECT_EQ(k.find(8419050415013ULL)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.hit, 3u);
  EXPECT_EQ(k.tak, 0u);
}

// 1 vs 1 where the aggressor's own Change (to a new price) arrives first.
TEST(FillSlate, OneVsOneAggressorChangeMovesPrice)
{
  Slate s;
  Sink k;
  s.on_trade(1, 7, 104.75, BUY, 2, 1, k.fn());     // resting bid
  s.on_trade(2, 7, 104.765625, SEL, 2, 2, k.fn()); // offer modified down to 104.75
  s.on_book(2, 7, CHANGE, 104.75, k.fn());         // moved: aggressor
  s.on_book(1, 7, DELETE, 104.75, k.fn());
  EXPECT_EQ(k.find(2)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.find(1)->role, FillRole::RESTING);
  EXPECT_EQ(k.hit, 2u);
  EXPECT_EQ(k.tak, 0u);
}

// 1 vs 1, both fully filled and BOTH at the touch: two Deletes, nothing
// tells them apart. Released as AMBIGUOUS when the transaction closes.
TEST(FillSlate, OneVsOneBothDeletedIsAmbiguous)
{
  Slate s;
  Sink k;
  s.on_trade(1, 7, 104.70, BUY, 5, 1, k.fn());
  s.on_trade(2, 7, 104.72, SEL, 5, 2, k.fn());
  s.on_book(1, 7, DELETE, 104.70, k.fn());
  s.on_book(2, 7, DELETE, 104.72, k.fn());
  EXPECT_TRUE(k.out.empty()) << "undecided: held until the transaction closes";
  s.on_book(99, 8, NEW, 104.70, k.fn());  // a record of the next transaction
  ASSERT_EQ(k.out.size(), 2u);
  EXPECT_EQ(k.out[0].role, FillRole::AMBIGUOUS);
  EXPECT_EQ(k.out[1].role, FillRole::AMBIGUOUS);
  EXPECT_EQ(s.n_ambiguous, 2u);
}

// ---------------------------------------------------------------------------
// The normal case, which must be unchanged: a NEW aggressor (not in the book,
// so never passed to on_trade) sweeps resting orders on one side.
// ---------------------------------------------------------------------------
TEST(FillSlate, NewAggressorSweepIsAllResting)
{
  Slate s;
  Sink k;
  s.on_trade(1, 5, 104.71875, SEL, 14, 1, k.fn());
  s.on_trade(2, 5, 104.71875, SEL, 3, 2, k.fn());
  s.on_book(1, 5, DELETE, 104.71875, k.fn());
  ASSERT_EQ(k.out.size(), 1u) << "each fill released at its own record";
  EXPECT_EQ(k.out[0].oid, 1u);
  s.on_book(2, 5, CHANGE, 104.71875, k.fn());  // partial fill, same price
  ASSERT_EQ(k.out.size(), 2u);
  EXPECT_EQ(k.out[1].role, FillRole::RESTING);
  EXPECT_EQ(k.tak, 17u);
  EXPECT_EQ(k.hit, 0u);
  EXPECT_EQ(s.n_aggressor, 0u);
}

// A resting order partly filled keeps its price: its Change is not a modify.
TEST(FillSlate, RestingPartialFillChangeSamePrice)
{
  Slate s;
  Sink k;
  s.on_trade(1, 5, 104.703125, BUY, 1, 1, k.fn());
  s.on_book(1, 5, CHANGE, 104.703125, k.fn());
  ASSERT_EQ(k.out.size(), 1u);
  EXPECT_EQ(k.out[0].role, FillRole::RESTING);
  EXPECT_EQ(k.hit, 1u);
}

// Auction uncross: several orders on BOTH sides, no single aggressor.
TEST(FillSlate, AuctionManyVsManyIsAllResting)
{
  Slate s;
  Sink k;
  const uint64_t tx = 1791496800000000000ULL;  // the 2026-10-08 open
  s.on_trade(1, tx, 104.71875, BUY, 11, 1, k.fn());
  s.on_trade(2, tx, 104.71875, BUY, 50, 2, k.fn());
  s.on_trade(3, tx, 104.71875, SEL, 12, 3, k.fn());
  s.on_trade(4, tx, 104.71875, SEL, 49, 4, k.fn());
  for (uint64_t o = 1; o <= 4; ++o)
    s.on_book(o, tx, DELETE, 104.71875, k.fn());
  ASSERT_EQ(k.out.size(), 4u);
  for (const auto &r : k.out)
    EXPECT_EQ(r.role, FillRole::RESTING);
  EXPECT_EQ(s.n_aggressor, 0u);
}

// A fill whose order never gets a book record in its transaction is released
// (as before: resting) when the next transaction starts. Nothing leaks.
TEST(FillSlate, UnmatchedFillReleasedAtNextTransaction)
{
  Slate s;
  Sink k;
  s.on_trade(1, 5, 104.70, BUY, 4, 11, k.fn());
  EXPECT_TRUE(k.out.empty());
  s.on_trade(2, 6, 104.70, BUY, 1, 12, k.fn());  // next transaction
  ASSERT_EQ(k.out.size(), 1u);
  EXPECT_EQ(k.out[0].oid, 1u);
  EXPECT_EQ(k.out[0].role, FillRole::RESTING);
  EXPECT_EQ(k.out[0].payload, 11) << "the caller's payload comes back unchanged";
  EXPECT_EQ(s.size(), 1u);
  s.flush(k.fn());
  EXPECT_TRUE(s.empty());
}

// Order of release follows arrival order of the trade records.
TEST(FillSlate, ReleaseOrderIsArrivalOrder)
{
  Slate s;
  Sink k;
  s.on_trade(1, 5, 1.0, SEL, 1, 1, k.fn());
  s.on_trade(2, 5, 1.0, SEL, 1, 2, k.fn());
  s.on_trade(3, 5, 1.0, SEL, 1, 3, k.fn());
  s.flush(k.fn());
  ASSERT_EQ(k.out.size(), 3u);
  EXPECT_EQ(k.out[0].oid, 1u);
  EXPECT_EQ(k.out[1].oid, 2u);
  EXPECT_EQ(k.out[2].oid, 3u);
}

// Channel reset drops held fills without releasing them.
TEST(FillSlate, ClearDropsWithoutRelease)
{
  Slate s;
  Sink k;
  s.on_trade(1, 5, 1.0, BUY, 1, 1, k.fn());
  s.clear();
  s.flush(k.fn());
  EXPECT_TRUE(k.out.empty());
}

// New-order records in the same transaction do not mark anything seen.
TEST(FillSlate, NewRecordDoesNotDecide)
{
  Slate s;
  Sink k;
  s.on_trade(1, 5, 1.0, BUY, 1, 1, k.fn());
  s.on_book(1, 5, NEW, 1.0, k.fn());
  EXPECT_TRUE(k.out.empty());
  s.on_book(1, 5, DELETE, 1.0, k.fn());
  ASSERT_EQ(k.out.size(), 1u);
}

// ---------------------------------------------------------------------------
// 1 vs 1, both orders Deleted, decided by the touch.
// ZN capture, transaction 1791550962227076887, order 8419054553801: a sell the
// owner kept moving between 104.578125 and 105.4375, last live Change to
// 105.4375; it then traded 1 against a buy resting at the best bid
// 104.578125 (market 104.578125 / 104.59375). Both Deleted, no Change.
// The buy is at the touch, the sell 27 ticks off it: the sell moved, it is
// the aggressor. Before: AMBIGUOUS, printed at 105.4375 and counted twice.
// ---------------------------------------------------------------------------
TEST(FillSlate, OneVsOneDecidedByTouch_ZN_8419054553801)
{
  Slate s;
  Sink k;
  const uint64_t tx = 1791550962227076887ULL;
  s.on_trade(8419054553801ULL, tx, 105.4375, SEL, 1, /*at_touch=*/false, 1, k.fn());
  s.on_trade(8419054635428ULL, tx, 104.578125, BUY, 1, /*at_touch=*/true, 2, k.fn());
  s.on_book(8419054635428ULL, tx, DELETE, 104.578125, k.fn());
  s.on_book(8419054553801ULL, tx, DELETE, 105.4375, k.fn());
  ASSERT_EQ(k.out.size(), 2u);
  EXPECT_EQ(k.find(8419054553801ULL)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.find(8419054635428ULL)->role, FillRole::RESTING);
  EXPECT_EQ(k.hit, 1u) << "a sell of 1";
  EXPECT_EQ(k.tak, 0u);
  EXPECT_EQ(s.n_ambiguous, 0u);
}

// A Change outranks the touch: the order whose Change moved its price is the
// aggressor even if the touch flags say otherwise.
TEST(FillSlate, ChangeOutranksTouch)
{
  Slate s;
  Sink k;
  s.on_trade(1, 7, 104.75, BUY, 2, /*at_touch=*/false, 1, k.fn());
  s.on_trade(2, 7, 104.765625, SEL, 2, /*at_touch=*/true, 2, k.fn());
  s.on_book(2, 7, CHANGE, 104.75, k.fn());   // the offer moved: aggressor
  s.on_book(1, 7, DELETE, 104.75, k.fn());
  EXPECT_EQ(k.find(2)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.find(1)->role, FillRole::RESTING);
}

// The touch flag does nothing outside 1 vs 1: a lone order against several is
// the aggressor whatever its flag says.
TEST(FillSlate, TouchIgnoredWhenCountsDecide)
{
  Slate s;
  Sink k;
  s.on_trade(1, 7, 104.6875, BUY, 2, /*at_touch=*/true, 1, k.fn());
  s.on_trade(2, 7, 104.71875, SEL, 1, /*at_touch=*/false, 2, k.fn());
  s.on_trade(3, 7, 104.71875, SEL, 1, /*at_touch=*/false, 3, k.fn());
  s.on_book(2, 7, DELETE, 104.71875, k.fn());
  s.on_book(3, 7, DELETE, 104.71875, k.fn());
  s.on_book(1, 7, CHANGE, 104.71875, k.fn());
  EXPECT_EQ(k.find(1)->role, FillRole::AGGRESSOR);
  EXPECT_EQ(k.tak, 2u);
  EXPECT_EQ(k.hit, 0u);
}

// ---------------------------------------------------------------------------
// Code review findings (dde1435..4c6b3c0/5d23596)
// ---------------------------------------------------------------------------

// Review #2: trade records arriving after book records of the same
// transaction must not flip a decided verdict and credit both sides.
// B1 vs S1, S1's Change keeps its price -> decided: S1 resting, B1 aggressor.
// Then B2, B3 of the SAME transaction arrive late. Before: the counts became
// 3 vs 1, the verdict flipped to "SEL is the aggressor", and the buys were
// credited too -- hit AND tak.
TEST(FillSlate, DecisionIsStickyWhenLateTradesArrive)
{
  Slate s;
  Sink k;
  s.on_trade(1, 9, 104.70, BUY, 2, 1, k.fn());
  s.on_trade(2, 9, 104.72, SEL, 2, 2, k.fn());
  s.on_book(2, 9, CHANGE, 104.72, k.fn());        // kept its price: resting
  s.on_trade(3, 9, 104.70, BUY, 1, 3, k.fn());    // late trade records
  s.on_trade(4, 9, 104.70, BUY, 1, 4, k.fn());
  s.on_book(1, 9, CHANGE, 104.72, k.fn());
  s.on_book(3, 9, DELETE, 104.70, k.fn());
  s.on_book(4, 9, DELETE, 104.70, k.fn());
  s.flush(k.fn());
  EXPECT_TRUE(k.hit == 0 || k.tak == 0) << "hit " << k.hit << " tak " << k.tak
                                         << ": both sides credited";
  EXPECT_EQ(k.find(2)->role, FillRole::RESTING);
}

// Review #1: in RELEASE_AT_OWN_RECORD mode (OB), an undecided fill goes out
// at its own record, before the caller processes that record -- OB must run
// the EXEC before the CANCD removes the order. Before: held until the other
// order's record, after OB had erased the order, so the EXEC was dropped.
TEST(FillSlate, ReleaseAtOwnRecordModeReleasesUndecidedImmediately)
{
  frame::ob::FillSlate<int> s(frame::ob::Undecided::RELEASE_AT_OWN_RECORD);
  Sink k;
  s.on_trade(1, 9, 104.70, BUY, 5, /*at_touch=*/true, 1, k.fn());
  s.on_trade(2, 9, 104.72, SEL, 5, /*at_touch=*/true, 2, k.fn());
  s.on_book(1, 9, DELETE, 104.70, k.fn());
  ASSERT_EQ(k.out.size(), 1u) << "released at its own record, not later";
  EXPECT_EQ(k.out[0].oid, 1u);
  EXPECT_EQ(k.out[0].role, FillRole::AMBIGUOUS);
  s.on_book(2, 9, DELETE, 104.72, k.fn());
  ASSERT_EQ(k.out.size(), 2u);
}

// ...while the default HOLD mode keeps holding the same undecided fill.
TEST(FillSlate, HoldModeKeepsUndecided)
{
  Slate s;
  Sink k;
  s.on_trade(1, 9, 104.70, BUY, 5, true, 1, k.fn());
  s.on_trade(2, 9, 104.72, SEL, 5, true, 2, k.fn());
  s.on_book(1, 9, DELETE, 104.70, k.fn());
  EXPECT_TRUE(k.out.empty());
}

// Review #3: advance() closes the previous transaction on any later trade
// record, so a held fill does not wait for this instrument's next book record
// (a thin book may not have one for minutes).
TEST(FillSlate, AdvanceClosesPreviousTransaction)
{
  Slate s;
  Sink k;
  s.on_trade(1, 9, 104.70, BUY, 5, true, 1, k.fn());
  s.on_trade(2, 9, 104.72, SEL, 5, true, 2, k.fn());
  s.advance(9, k.fn());                 // same transaction: nothing happens
  EXPECT_TRUE(k.out.empty());
  s.advance(10, k.fn());                // a trade record of a later one
  EXPECT_EQ(k.out.size(), 2u);
  EXPECT_TRUE(s.empty());
}

// Review #6 (OB::clear): clear() also resets the transaction, so fills of a
// new transaction are not decided with the old one's counts.
TEST(FillSlate, ClearResetsTransactionState)
{
  Slate s;
  Sink k;
  s.on_trade(1, 9, 104.70, BUY, 5, 1, k.fn());   // one buy listed...
  s.clear();
  s.on_trade(2, 9, 104.72, SEL, 3, 2, k.fn());   // ...forgotten: this is one-sided
  s.on_book(2, 9, DELETE, 104.72, k.fn());
  ASSERT_EQ(k.out.size(), 1u);
  EXPECT_EQ(k.out[0].role, FillRole::RESTING);
}

// Review #8 (cost): a 50,000-order sweep must be linear. Measured: the
// quadratic version took 251 ms for 20,000 orders (it grows with N^2); the
// linear one takes a few ms for 50,000.
TEST(FillSlate, LargeSweepIsLinear)
{
  Slate s;
  Sink k;
  const int N = 50000;
  for (int i = 0; i < N; ++i)
    s.on_trade(i + 1, 9, 104.71875, SEL, 1, i, k.fn());
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < N; ++i)
    s.on_book(i + 1, 9, DELETE, 104.71875, k.fn());
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
  EXPECT_EQ(k.out.size(), size_t(N));
  EXPECT_EQ(k.tak, uint64_t(N));
  EXPECT_LT(ms, 200) << "on_book over a " << N << "-order sweep took " << ms << " ms";
}

