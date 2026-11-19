/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BrokerOrderBook: order bookkeeping shared by the broker order managers.

#include <gtest/gtest.h>

#include <chrono>
#include <climits>
#include <set>

#include "frame/som/BrokerOrderBook.hpp"

using namespace frame::som;
using Order = frame::som::msg::Order;

namespace
{
  Order order(int oid, int sz = 10, int px = 15000)
  {
    Order o;
    o.oid = oid;
    o.sz = sz;
    o.px = px;
    o.side = en::bs::BUY;
    return o;
  }

  class BrokerOrderBookTest : public ::testing::Test
  {
  protected:
    BrokerOrderBook book{ClientIdGen(34'200'000)}; // 2026-01-01 09:30:00.000 UTC
    ClientId place(int oid, int sz = 10)
    {
      auto r = book.add(order(oid, sz), nullptr);
      EXPECT_EQ(r.outcome, Outcome::Ok);
      return r.entry->cid;
    }
  };
} // namespace

// ---- client ids -----------------------------------------------------------

TEST(ClientIdGen, UniqueAndIncreasing)
{
  ClientIdGen g(1000);
  std::set<ClientId> ids;
  ClientId prev = 0;
  for (int i = 0; i < 10000; ++i)
  {
    auto id = g.next();
    EXPECT_GT(id, prev);
    prev = id;
    ids.insert(id);
  }
  EXPECT_EQ(ids.size(), 10000u);
}

// A restart a millisecond later never reuses an id from the earlier run.
TEST(ClientIdGen, RestartDoesNotReuseIds)
{
  ClientIdGen first(34'200'000), second(34'200'001);
  std::set<ClientId> a;
  for (int i = 0; i < 100000; ++i)
    a.insert(first.next());
  for (int i = 0; i < 100000; ++i)
    EXPECT_EQ(a.count(second.next()), 0u);
}

// The largest id of the day still fits a positive int64 (decimal ClOrdID).
// Generators made back to back in one process never share a millisecond.
TEST(ClientIdGen, FromClockNeverRepeatsInOneProcess)
{
  std::set<ClientId> ids;
  for (int i = 0; i < 1000; ++i)
  {
    auto g = ClientIdGen::from_clock();
    ids.insert(g.next());
  }
  EXPECT_EQ(ids.size(), 1000u);
}

TEST(ClientIdGen, FitsInSignedInt64)
{
  ClientIdGen g((1ULL << ClientIdGen::MS_BITS) - 1); // the last slot (late 2060)
  for (int i = 0; i < 10; ++i)
    EXPECT_LT(g.next(), uint64_t(INT64_MAX));
  EXPECT_LT(ClientIdGen::from_clock().next(), uint64_t(INT64_MAX));
}

// A restart that crosses 00:00 UTC (a trading day that spans it) still issues
// larger ids than the run before it.
TEST(ClientIdGen, RestartAcrossUtcMidnightIssuesLargerIds)
{
  const uint64_t midnight = 100 * ClientIdGen::MS_PER_DAY;
  ClientIdGen before(midnight - 1), after(midnight + 1);
  ClientId max_before = 0;
  for (int i = 0; i < 1000; ++i)
    max_before = std::max(max_before, before.next());
  for (int i = 0; i < 1000; ++i)
    EXPECT_GT(after.next(), max_before);
}

// from_clock() never hands out a slot later than the wall clock, however
// many generators are made at once, so a process started later starts above
// every id of this one.
TEST(ClientIdGen, FromClockNeverAheadOfWallClock)
{
  using namespace std::chrono;
  for (int i = 0; i < 50; ++i)
  {
    auto id = ClientIdGen::from_clock().next();
    uint64_t now = uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()) -
                   ClientIdGen::EPOCH_UNIX_MS;
    EXPECT_LE(id >> ClientIdGen::COUNTER_BITS, now);
  }
}

// A generator that uses up its counter moves to a later slot: ids stay unique
// and increasing.
TEST(ClientIdGen, CounterExhaustionKeepsIdsIncreasing)
{
  for (auto g : {ClientIdGen(5000), ClientIdGen::from_clock()})
  {
    ClientId prev = 0;
    bool increasing = true;
    for (uint64_t i = 0; i < ClientIdGen::COUNTER_MAX + 10; ++i)
    {
      auto id = g.next();
      increasing = increasing && id > prev;
      prev = id;
    }
    EXPECT_TRUE(increasing);
  }
}

// ---- lifecycle --------------------------------------------------------------

TEST_F(BrokerOrderBookTest, NewAcceptFillToDone)
{
  auto cid = place(1, 10);
  EXPECT_EQ(book.find_oid(1)->state, State::PendingNew);

  auto a = book.on_accepted(cid, 777);
  EXPECT_EQ(a.outcome, Outcome::Ok);
  EXPECT_EQ(a.entry->state, State::Working);
  EXPECT_EQ(a.entry->exch_oid, 777u);

  auto f1 = book.on_fill(cid, 4);
  EXPECT_EQ(f1.outcome, Outcome::Ok);
  EXPECT_EQ(f1.fill_qty, 4);
  EXPECT_FALSE(f1.done);
  EXPECT_EQ(f1.entry->leaves, 6);

  auto f2 = book.on_fill(cid, 6);
  EXPECT_TRUE(f2.done);
  EXPECT_EQ(f2.entry->state, State::Filled);
  EXPECT_EQ(f2.entry->leaves, 0);
  EXPECT_EQ(book.live(), 0u);
}

// Some venues send the fill before (or instead of) the ack.
TEST_F(BrokerOrderBookTest, FillBeforeAck)
{
  auto cid = place(2, 5);
  auto f = book.on_fill(cid, 2);
  EXPECT_EQ(f.outcome, Outcome::Ok);
  EXPECT_EQ(f.entry->state, State::Working);
  EXPECT_EQ(f.entry->leaves, 3);
}

TEST_F(BrokerOrderBookTest, Reject)
{
  auto cid = place(3);
  auto r = book.on_rejected(cid);
  EXPECT_TRUE(r.done);
  EXPECT_EQ(r.entry->state, State::Rejected);
  EXPECT_EQ(book.on_fill(cid, 1).outcome, Outcome::Late);
  EXPECT_EQ(book.request_cancel(3).outcome, Outcome::Late);
}

TEST_F(BrokerOrderBookTest, DuplicateAndInvalidAdd)
{
  place(4);
  EXPECT_EQ(book.add(order(4), nullptr).outcome, Outcome::Duplicate);
  EXPECT_EQ(book.add(order(5, 0), nullptr).outcome, Outcome::Invalid);
}

// Quantities are whole units: fractional and out-of-range sizes are refused.
TEST_F(BrokerOrderBookTest, NonIntegerSizeIsInvalid)
{
  Order o = order(6);
  o.sz = 0.5;
  EXPECT_EQ(book.add(o, nullptr).outcome, Outcome::Invalid);
  o.sz = 10.7;
  EXPECT_EQ(book.add(o, nullptr).outcome, Outcome::Invalid);
  o.sz = 3e9;
  EXPECT_EQ(book.add(o, nullptr).outcome, Outcome::Invalid);
  EXPECT_EQ(book.size(), 0u);
}

// ---- cancels ---------------------------------------------------------------

TEST_F(BrokerOrderBookTest, CancelRoundTripOnEitherId)
{
  auto cid = place(10);
  book.on_accepted(cid);
  auto c = book.request_cancel(10);
  ASSERT_EQ(c.outcome, Outcome::Ok);
  EXPECT_EQ(c.entry->state, State::PendingCancel);
  auto ccid = c.entry->pending_cid;
  EXPECT_NE(ccid, cid);
  // FIX acks the cancel under the cancel's ClOrdID
  auto done = book.on_canceled(ccid);
  EXPECT_TRUE(done.done);
  EXPECT_EQ(done.entry->oid, 10);
  EXPECT_EQ(done.entry->state, State::Canceled);

  // an API that cancels by the order's own id acks under it
  auto cid2 = place(11);
  book.on_accepted(cid2);
  book.request_cancel(11);
  EXPECT_EQ(book.on_canceled(cid2).entry->state, State::Canceled);
}

// A cancel the broker or exchange started (cancel on disconnect, risk, an
// admin) arrives on the order's id with no cancel of ours pending.
TEST_F(BrokerOrderBookTest, UnsolicitedCancel)
{
  auto cid = place(12);
  book.on_accepted(cid);
  auto r = book.on_canceled(cid);
  EXPECT_EQ(r.outcome, Outcome::Ok);
  EXPECT_TRUE(r.done);
}

TEST_F(BrokerOrderBookTest, CancelRejectedLeavesOrderWorking)
{
  auto cid = place(13);
  book.on_accepted(cid);
  auto c = book.request_cancel(13);
  auto r = book.on_cancel_rejected(c.entry->pending_cid);
  EXPECT_EQ(r.outcome, Outcome::Ok);
  EXPECT_EQ(r.entry->state, State::Working);
  EXPECT_EQ(r.entry->pending_cid, 0u);
  // and it can be cancelled again
  EXPECT_EQ(book.request_cancel(13).outcome, Outcome::Ok);
}

TEST_F(BrokerOrderBookTest, CancelWhilePendingIsBusy)
{
  auto cid = place(14);
  book.on_accepted(cid);
  EXPECT_EQ(book.request_cancel(14).outcome, Outcome::Ok);
  EXPECT_EQ(book.request_cancel(14).outcome, Outcome::Busy);
  EXPECT_EQ(book.request_replace(14, 20, 15000).outcome, Outcome::Busy);
}

TEST_F(BrokerOrderBookTest, CancelAfterFillIsLateNotFatal)
{
  auto cid = place(15, 1);
  book.on_accepted(cid);
  book.on_fill(cid, 1);
  EXPECT_EQ(book.request_cancel(15).outcome, Outcome::Late);
  EXPECT_EQ(book.request_cancel(999).outcome, Outcome::Unknown);
}

// The fill and the cancel cross: the order filled while our cancel was in
// flight. The fill counts and finishes the order; the cancel ack is late.
TEST_F(BrokerOrderBookTest, FillRacesCancel)
{
  auto cid = place(16, 5);
  book.on_accepted(cid);
  auto c = book.request_cancel(16);
  auto ccid = c.entry->pending_cid;
  auto f = book.on_fill(cid, 5);
  EXPECT_TRUE(f.done);
  EXPECT_EQ(f.entry->state, State::Filled);
  EXPECT_EQ(book.on_canceled(ccid).outcome, Outcome::Late);
}

// A partial fill reported after the cancel ack still happened and is counted.
TEST_F(BrokerOrderBookTest, FillAfterCancelAckStillCounts)
{
  auto cid = place(17, 5);
  book.on_accepted(cid);
  book.on_canceled(cid);
  auto f = book.on_fill(cid, 2);
  EXPECT_EQ(f.outcome, Outcome::Ok);
  EXPECT_FALSE(f.done);
  EXPECT_EQ(f.entry->cum, 2);
  EXPECT_EQ(f.entry->state, State::Canceled);
}

// A late Canceled on the id the order was replaced away from is not about the
// live order: it does not finish it.
TEST_F(BrokerOrderBookTest, CancelOnReplacedAwayIdIsLate)
{
  auto cid = place(18);
  book.on_accepted(cid);
  auto ncid = book.request_replace(18, 12, 15100).entry->pending_cid;
  book.on_replaced(ncid);
  auto r = book.on_canceled(cid);
  EXPECT_EQ(r.outcome, Outcome::Late);
  EXPECT_FALSE(r.done);
  EXPECT_EQ(r.entry->state, State::Working);
  EXPECT_EQ(book.live(), 1u);
  // an unsolicited cancel on the current id still finishes it
  EXPECT_TRUE(book.on_canceled(ncid).done);
}

// The cancel finished the order during a replace up: a fill after it may go
// up to the replace size, not just the old one.
TEST_F(BrokerOrderBookTest, FillAfterCancelDuringReplaceUpUsesReplaceSize)
{
  auto cid = place(19, 10);
  book.on_accepted(cid);
  auto ncid = book.request_replace(19, 20, 15000).entry->pending_cid;
  EXPECT_FALSE(book.on_fill(cid, 10).done);
  EXPECT_TRUE(book.on_canceled(cid).done);
  auto f = book.on_fill(ncid, 5);
  EXPECT_EQ(f.outcome, Outcome::Ok);
  EXPECT_EQ(f.entry->cum, 15);
  EXPECT_EQ(f.entry->state, State::Canceled);
  EXPECT_EQ(book.on_fill(ncid, 6).outcome, Outcome::Invalid); // beyond 20
}

// ---- restatement ---------------------------------------------------------------

// The venue cut the order's size (self-trade prevention): the rest of the new
// size is all the order can still fill, and a fill of it finishes the order.
TEST_F(BrokerOrderBookTest, RestatementCutsTheQuantity)
{
  auto cid = place(26, 10);
  book.on_accepted(cid);
  book.on_fill(cid, 3);
  auto r = book.on_restated(cid, 7);
  EXPECT_EQ(r.outcome, Outcome::Ok);
  EXPECT_FALSE(r.done);
  EXPECT_EQ(r.entry->qty, 7);
  EXPECT_EQ(r.entry->leaves, 4);
  EXPECT_EQ(r.entry->state, State::Working);
  auto f = book.on_fill(cid, 4);
  EXPECT_TRUE(f.done);
  EXPECT_EQ(f.entry->state, State::Filled);
  // restated to what is already filled: done
  auto cid2 = place(27, 10);
  book.on_fill(cid2, 5);
  auto d = book.on_restated(cid2, 5);
  EXPECT_TRUE(d.done);
  EXPECT_EQ(d.entry->state, State::Filled);
  EXPECT_EQ(book.on_restated(cid2, 5).outcome, Outcome::Late);
  // below what is filled, or nothing: invalid; unknown id
  auto cid3 = place(28, 10);
  book.on_fill(cid3, 5);
  EXPECT_EQ(book.on_restated(cid3, 4).outcome, Outcome::Invalid);
  EXPECT_EQ(book.on_restated(cid3, 0).outcome, Outcome::Invalid);
  EXPECT_EQ(book.on_restated(cid3, 10).entry->leaves, 5);
  EXPECT_EQ(book.on_restated(999, 1).outcome, Outcome::Unknown);
}

// ---- cancel/replace -----------------------------------------------------------

TEST_F(BrokerOrderBookTest, ReplaceMovesToNewIdAndKeepsOldOne)
{
  auto cid = place(20, 10);
  book.on_accepted(cid);
  book.on_fill(cid, 3);
  auto r = book.request_replace(20, 8, 15100);
  ASSERT_EQ(r.outcome, Outcome::Ok);
  auto ncid = r.entry->pending_cid;

  auto ok = book.on_replaced(ncid);
  EXPECT_EQ(ok.outcome, Outcome::Ok);
  EXPECT_EQ(ok.entry->cid, ncid);
  EXPECT_EQ(ok.entry->qty, 8);
  EXPECT_EQ(ok.entry->px, 15100);
  EXPECT_EQ(ok.entry->leaves, 5); // 8 total - 3 filled

  // a late fill on the old id still resolves to the same order
  auto f = book.on_fill(cid, 1);
  EXPECT_EQ(f.entry->oid, 20);
  EXPECT_EQ(f.entry->leaves, 4);
  // a cancel now goes out against the new id
  auto c = book.request_cancel(20);
  EXPECT_EQ(c.entry->cid, ncid);
}

// A fill between the replace request and its ack: leaves comes from the new
// total less everything filled, instead of asserting the requested size.
TEST_F(BrokerOrderBookTest, FillDuringReplace)
{
  auto cid = place(21, 10);
  book.on_accepted(cid);
  auto r = book.request_replace(21, 6, 15000);
  auto ncid = r.entry->pending_cid;
  book.on_fill(cid, 2);
  auto ok = book.on_replaced(ncid);
  EXPECT_EQ(ok.entry->leaves, 4);
}

TEST_F(BrokerOrderBookTest, ReplaceRejectedKeepsOriginal)
{
  auto cid = place(22, 10);
  book.on_accepted(cid);
  auto r = book.request_replace(22, 12, 15200);
  auto rr = book.on_replace_rejected(r.entry->pending_cid);
  EXPECT_EQ(rr.outcome, Outcome::Ok);
  EXPECT_EQ(rr.entry->state, State::Working);
  EXPECT_EQ(rr.entry->cid, cid);
  EXPECT_EQ(rr.entry->qty, 10);
  EXPECT_EQ(rr.entry->px, 15000);
}

TEST_F(BrokerOrderBookTest, ReplaceBelowFilledIsInvalid)
{
  auto cid = place(23, 10);
  book.on_accepted(cid);
  book.on_fill(cid, 4);
  EXPECT_EQ(book.request_replace(23, 4, 15000).outcome, Outcome::Invalid);
  EXPECT_EQ(book.request_replace(23, 5, 15000).outcome, Outcome::Ok);
}

// The venue may fill against the new, larger size before the replace is
// confirmed.
TEST_F(BrokerOrderBookTest, FillAboveOldSizeDuringReplaceUp)
{
  auto cid = place(24, 100);
  book.on_accepted(cid);
  book.on_fill(cid, 90);
  auto r = book.request_replace(24, 200, 15000);
  auto ncid = r.entry->pending_cid;
  auto f = book.on_fill(ncid, 50);
  EXPECT_EQ(f.outcome, Outcome::Ok);
  EXPECT_FALSE(f.done);
  EXPECT_EQ(f.entry->cum, 140);
  EXPECT_EQ(f.entry->leaves, 60);
  auto ok = book.on_replaced(ncid);
  EXPECT_EQ(ok.outcome, Outcome::Ok);
  EXPECT_EQ(ok.entry->leaves, 60);
  EXPECT_EQ(book.on_fill(ncid, 61).outcome, Outcome::Invalid);
}

// Filled up to the old size while a replace up is in flight, then the replace
// is rejected: the order is done at its old size.
TEST_F(BrokerOrderBookTest, ReplaceUpRejectedAfterOldSizeFilled)
{
  auto cid = place(25, 10);
  book.on_accepted(cid);
  auto r = book.request_replace(25, 20, 15000);
  EXPECT_FALSE(book.on_fill(cid, 10).done);
  auto rr = book.on_replace_rejected(r.entry->pending_cid);
  EXPECT_TRUE(rr.done);
  EXPECT_EQ(rr.entry->state, State::Filled);
}

// Filled beyond the new size while a replace down is in flight: the order is
// done, and the replace confirmation reports the overfill as Invalid.
TEST_F(BrokerOrderBookTest, ReplaceDownBelowFillsInFlightIsInvalid)
{
  auto cid = place(26, 10);
  book.on_accepted(cid);
  auto r = book.request_replace(26, 6, 15000);
  auto ncid = r.entry->pending_cid;
  book.on_fill(cid, 8);
  auto ok = book.on_replaced(ncid);
  EXPECT_EQ(ok.outcome, Outcome::Invalid);
  EXPECT_TRUE(ok.done);
  EXPECT_EQ(ok.entry->state, State::Filled);
  EXPECT_EQ(ok.entry->leaves, 0);
}

// Replace down, and the venue fills the old size before the replace: the order
// is done as Filled, leaves never goes negative, and a later confirmation of
// the smaller size is reported Invalid (once).
TEST_F(BrokerOrderBookTest, ReplaceDownFilledAtOldSizeFinishes)
{
  auto cid = place(60, 10);
  book.on_accepted(cid);
  auto ncid = book.request_replace(60, 6, 15000).entry->pending_cid;
  auto f = book.on_fill(cid, 10);
  EXPECT_EQ(f.outcome, Outcome::Ok);
  EXPECT_TRUE(f.done);
  EXPECT_EQ(f.fill_qty, 10);
  EXPECT_EQ(f.entry->state, State::Filled);
  EXPECT_EQ(f.entry->leaves, 0);
  EXPECT_EQ(book.live(), 0u);
  auto ok = book.on_replaced(ncid);
  EXPECT_EQ(ok.outcome, Outcome::Invalid);
  EXPECT_FALSE(ok.done);
  EXPECT_EQ(ok.entry->leaves, 0);
  EXPECT_EQ(book.on_replaced(ncid).outcome, Outcome::Late);
  EXPECT_EQ(book.on_replace_rejected(ncid).outcome, Outcome::Late);
}

// A replace to the same size (a price change) and the old size fills: done.
TEST_F(BrokerOrderBookTest, ReplaceSameSizeFilledFinishes)
{
  auto cid = place(61, 10);
  book.on_accepted(cid);
  auto ncid = book.request_replace(61, 10, 15100).entry->pending_cid;
  EXPECT_TRUE(book.on_fill(cid, 10).done);
  EXPECT_EQ(book.on_replaced(ncid).outcome, Outcome::Late);
}

// A duplicate 'replaced' on an earlier id does not confirm the replace now in
// flight.
TEST_F(BrokerOrderBookTest, StaleReplacedIsLate)
{
  auto c0 = place(62, 10);
  book.on_accepted(c0);
  auto r1 = book.request_replace(62, 12, 15100).entry->pending_cid;
  book.on_replaced(r1);
  auto r2 = book.request_replace(62, 14, 15200).entry->pending_cid;
  auto stale = book.on_replaced(c0);
  EXPECT_EQ(stale.outcome, Outcome::Late);
  EXPECT_EQ(stale.entry->state, State::PendingReplace);
  EXPECT_EQ(stale.entry->cid, r1);
  EXPECT_EQ(stale.entry->pending_cid, r2);
  EXPECT_EQ(stale.entry->qty, 12);
  EXPECT_EQ(book.on_replaced(r2).entry->qty, 14);
}

// A duplicate cancel or replace reject after the order is back to Working is
// Late, not Invalid.
TEST_F(BrokerOrderBookTest, DuplicateRejectAfterWorkingIsLate)
{
  auto cid = place(63);
  book.on_accepted(cid);
  auto c = book.request_cancel(63).entry->pending_cid;
  book.on_cancel_rejected(c);
  EXPECT_EQ(book.on_cancel_rejected(c).outcome, Outcome::Late);
  auto r = book.request_replace(63, 12, 15100).entry->pending_cid;
  book.on_replace_rejected(r);
  auto dup = book.on_replace_rejected(r);
  EXPECT_EQ(dup.outcome, Outcome::Late);
  EXPECT_EQ(dup.entry->state, State::Working);
}

// ---- requests sent before the ack -----------------------------------------

// A cancel sent before the ack, then the new order is rejected: the order is
// finished as Rejected, not left live.
TEST_F(BrokerOrderBookTest, RejectAfterCancelSentBeforeAck)
{
  auto cid = place(27);
  book.request_cancel(27);
  auto r = book.on_rejected(cid);
  EXPECT_EQ(r.outcome, Outcome::Ok);
  EXPECT_TRUE(r.done);
  EXPECT_EQ(r.entry->state, State::Rejected);
  EXPECT_EQ(r.entry->pending_cid, 0u);
  EXPECT_EQ(book.live(), 0u);
}

TEST_F(BrokerOrderBookTest, RejectAfterReplaceSentBeforeAck)
{
  auto cid = place(28);
  book.request_replace(28, 12, 15100);
  auto r = book.on_rejected(cid);
  EXPECT_TRUE(r.done);
  EXPECT_EQ(r.entry->state, State::Rejected);
}

// A cancel or replace rejected before the ack leaves the order unacknowledged:
// it can still be accepted or rejected.
TEST_F(BrokerOrderBookTest, CancelOrReplaceRejectedBeforeAckStaysPendingNew)
{
  auto cid = place(29);
  auto c = book.request_cancel(29);
  EXPECT_EQ(book.on_cancel_rejected(c.entry->pending_cid).entry->state, State::PendingNew);
  auto r = book.request_replace(29, 12, 15100);
  EXPECT_EQ(book.on_replace_rejected(r.entry->pending_cid).entry->state, State::PendingNew);
  EXPECT_EQ(book.on_rejected(cid).entry->state, State::Rejected);
}

// A reject on an order that was accepted is not a new-order reject.
TEST_F(BrokerOrderBookTest, RejectOnAcceptedOrderIsInvalid)
{
  auto cid = place(31);
  book.on_accepted(cid);
  book.request_cancel(31);
  auto r = book.on_rejected(cid);
  EXPECT_EQ(r.outcome, Outcome::Invalid);
  EXPECT_EQ(r.entry->state, State::PendingCancel);
}

// An earlier cancel's reject, resent or late, does not clear the cancel in
// flight.
TEST_F(BrokerOrderBookTest, StaleCancelRejectIsLate)
{
  auto cid = place(32);
  book.on_accepted(cid);
  auto c1 = book.request_cancel(32).entry->pending_cid;
  book.on_cancel_rejected(c1);
  auto c2 = book.request_cancel(32).entry->pending_cid;
  auto stale = book.on_cancel_rejected(c1);
  EXPECT_EQ(stale.outcome, Outcome::Late);
  EXPECT_EQ(stale.entry->state, State::PendingCancel);
  EXPECT_EQ(stale.entry->pending_cid, c2);
  EXPECT_EQ(book.request_cancel(32).outcome, Outcome::Busy);
}

// ---- unknown and invalid input never aborts -------------------------------

// Reports for ids we never issued: a restart with working orders, or an order
// worked from the broker's own screen. Each comes back Unknown.
TEST_F(BrokerOrderBookTest, UnknownIdsAreReportedNotFatal)
{
  EXPECT_EQ(book.on_accepted(42).outcome, Outcome::Unknown);
  auto f = book.on_fill(42, 3);
  EXPECT_EQ(f.outcome, Outcome::Unknown);
  EXPECT_EQ(f.fill_qty, 3); // the order manager raises this one
  EXPECT_EQ(book.on_canceled(42).outcome, Outcome::Unknown);
  EXPECT_EQ(book.on_rejected(42).outcome, Outcome::Unknown);
  EXPECT_EQ(book.on_replaced(42).outcome, Outcome::Unknown);
  EXPECT_EQ(book.on_cancel_rejected(42).outcome, Outcome::Unknown);
}

TEST_F(BrokerOrderBookTest, OverfillIsInvalid)
{
  auto cid = place(30, 5);
  book.on_accepted(cid);
  EXPECT_EQ(book.on_fill(cid, 6).outcome, Outcome::Invalid);
  EXPECT_EQ(book.on_fill(cid, 0).outcome, Outcome::Invalid);
  EXPECT_EQ(book.find_oid(30)->cum, 0);
}

// A fill so large that cum + qty would overflow int is Invalid, not wrapped.
TEST_F(BrokerOrderBookTest, HugeFillDoesNotOverflow)
{
  auto cid = place(33, 10);
  book.on_accepted(cid);
  book.on_fill(cid, 5);
  EXPECT_EQ(book.on_fill(cid, INT_MAX).outcome, Outcome::Invalid);
  EXPECT_EQ(book.find_oid(33)->cum, 5);
}

// add() creates no entry for an invalid order: entry is null.
TEST_F(BrokerOrderBookTest, InvalidAddHasNoEntry)
{
  EXPECT_EQ(book.add(order(34, 0), nullptr).entry, nullptr);
}

// ---- synchronous send failures ------------------------------------------------

TEST_F(BrokerOrderBookTest, SendFailedNewOrderIsRejected)
{
  auto cid = place(40);
  auto r = book.send_failed(cid);
  EXPECT_TRUE(r.done);
  EXPECT_EQ(r.entry->state, State::Rejected);
}

// Accepted without an exchange id (it is optional) and with no fills: a failed
// cancel send still restores Working.
TEST_F(BrokerOrderBookTest, SendFailedCancelWithoutExchangeIdRestoresWorking)
{
  auto cid = place(42);
  book.on_accepted(cid);
  auto c = book.request_cancel(42);
  auto r = book.send_failed(c.entry->pending_cid);
  EXPECT_EQ(r.entry->state, State::Working);
  EXPECT_EQ(book.on_rejected(cid).outcome, Outcome::Invalid);
}

TEST_F(BrokerOrderBookTest, SendFailedCancelRestoresOrder)
{
  auto cid = place(41);
  book.on_accepted(cid, 9);
  auto c = book.request_cancel(41);
  auto r = book.send_failed(c.entry->pending_cid);
  EXPECT_EQ(r.outcome, Outcome::Ok);
  EXPECT_EQ(r.entry->state, State::Working);
  EXPECT_EQ(r.entry->pending_cid, 0u);
}

// A replace up whose send fails after the old size has filled: the order is
// done, as on a replace reject.
TEST_F(BrokerOrderBookTest, SendFailedReplaceAfterOldSizeFilledFinishes)
{
  auto cid = place(43, 10);
  book.on_accepted(cid);
  auto r = book.request_replace(43, 20, 15000).entry->pending_cid;
  book.on_fill(cid, 10);
  auto f = book.send_failed(r);
  EXPECT_EQ(f.outcome, Outcome::Ok);
  EXPECT_TRUE(f.done);
  EXPECT_EQ(f.entry->state, State::Filled);
  EXPECT_EQ(f.entry->leaves, 0);
}

// ---- housekeeping -------------------------------------------------------------

TEST_F(BrokerOrderBookTest, PurgeDoneDropsFinishedOrdersAndTheirIds)
{
  auto a = place(50, 1);
  auto b = place(51, 1);
  book.on_accepted(a);
  book.on_fill(a, 1);
  book.on_accepted(b);
  EXPECT_EQ(book.size(), 2u);
  book.purge_done();
  EXPECT_EQ(book.size(), 1u);
  EXPECT_EQ(book.find_cid(a), nullptr);
  EXPECT_NE(book.find_cid(b), nullptr);
}
