/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BrokerOrderBook: order bookkeeping shared by the broker order managers.

#include <gtest/gtest.h>

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
    BrokerOrderBook book{ClientIdGen(34'200'000)}; // 09:30:00.000
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
  ClientIdGen g(ClientIdGen::MS_PER_DAY - 1);
  EXPECT_LT(g.next(), uint64_t(INT64_MAX));
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
