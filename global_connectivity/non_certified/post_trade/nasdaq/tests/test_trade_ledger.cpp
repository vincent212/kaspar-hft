/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// post_trade::TradeLedger on its own: new, amend, cancel, bust, duplicates,
// replay of a day, and reports out of order. No actor, no network.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "post_trade/TradeLedger.hpp"

using post_trade::Applied;
using post_trade::Event;
using post_trade::Side;
using post_trade::TradeLedger;
using post_trade::msg::TradeCapture;

namespace
{
  TradeCapture report(const std::string &trade, uint64_t seq, Event ev = Event::New,
                      int64_t qty = 100, int64_t px = 1000000, Side side = Side::Buy,
                      const std::string &sym = "INTC")
  {
    TradeCapture t;
    t.venue = en::x::NASDAQ;
    t.trade_id = trade;
    if (ev != Event::New)
      t.ref_trade_id = trade;
    t.report_id = "S1:" + std::to_string(seq);
    t.report_seq = seq;
    t.event = ev;
    t.symbol = sym;
    t.side = side;
    t.qty = qty;
    t.price_mantissa = px;
    t.price_decimals = 4;
    return t;
  }
}

TEST(TradeLedger, NewTradeIsAdded)
{
  TradeLedger l;
  auto r = l.apply(report("T1", 1));
  EXPECT_EQ(r.what, Applied::Added);
  EXPECT_TRUE(r.emit());
  EXPECT_EQ(r.out.trade_id, "T1");
  ASSERT_NE(l.find("T1"), nullptr);
  EXPECT_EQ(l.live(), 1u);
  EXPECT_EQ(l.net_qty("INTC"), 100);
}

TEST(TradeLedger, AmendReplacesEconomics)
{
  TradeLedger l;
  l.apply(report("T1", 1, Event::New, 100, 1000000));
  auto r = l.apply(report("T1", 2, Event::Amend, 80, 1000100));
  EXPECT_EQ(r.what, Applied::Amended);
  EXPECT_EQ(r.out.ref_trade_id, "T1");
  const TradeCapture *t = l.find("T1");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->qty, 80);
  EXPECT_EQ(t->price_mantissa, 1000100);
  EXPECT_EQ(l.live(), 1u);
  EXPECT_EQ(l.net_qty("INTC"), 80);
}

TEST(TradeLedger, CancelRemoves)
{
  TradeLedger l;
  l.apply(report("T1", 1));
  auto r = l.apply(report("T1", 2, Event::Cancel));
  EXPECT_EQ(r.what, Applied::Removed);
  EXPECT_EQ(r.out.event, Event::Cancel);
  EXPECT_EQ(l.find("T1"), nullptr);
  EXPECT_TRUE(l.removed("T1"));
  EXPECT_EQ(l.net_qty("INTC"), 0);
}

TEST(TradeLedger, BustRemovesAndCompletesTheRecord)
{
  TradeLedger l;
  l.apply(report("T1", 1, Event::New, 300, 1234500, Side::Sell));
  // A bust that leaves out the economics: they come from the trade it removes.
  TradeCapture b;
  b.trade_id = "T1";
  b.ref_trade_id = "T1";
  b.report_id = "S1:2";
  b.report_seq = 2;
  b.event = Event::Bust;
  auto r = l.apply(b);
  EXPECT_EQ(r.what, Applied::Removed);
  EXPECT_EQ(r.out.event, Event::Bust);
  EXPECT_EQ(r.out.qty, 300);
  EXPECT_EQ(r.out.side, Side::Sell);
  EXPECT_EQ(r.out.symbol, "INTC");
  EXPECT_EQ(r.out.price_mantissa, 1234500);
  EXPECT_EQ(l.live(), 0u);
}

TEST(TradeLedger, DuplicateReportIsDropped)
{
  TradeLedger l;
  EXPECT_EQ(l.apply(report("T1", 1)).what, Applied::Added);
  auto again = report("T1", 1);
  again.poss_dup = true;
  auto r = l.apply(again);
  EXPECT_EQ(r.what, Applied::Duplicate);
  EXPECT_FALSE(r.emit());
  EXPECT_EQ(l.live(), 1u);
  EXPECT_EQ(l.net_qty("INTC"), 100) << "a duplicate does not add to the position";

  // Duplicates of amendments and removals change nothing either.
  l.apply(report("T1", 2, Event::Amend, 50));
  EXPECT_EQ(l.apply(report("T1", 2, Event::Amend, 50)).what, Applied::Duplicate);
  l.apply(report("T1", 3, Event::Bust));
  EXPECT_EQ(l.apply(report("T1", 3, Event::Bust)).what, Applied::Duplicate);
  EXPECT_EQ(l.live(), 0u);
}

TEST(TradeLedger, ReplayFromStartOfDay)
{
  const std::vector<TradeCapture> day = {
      report("T1", 1, Event::New, 100),
      report("T2", 2, Event::New, 200, 1000000, Side::Sell),
      report("T3", 3, Event::New, 300),
      report("T1", 4, Event::Amend, 150),
      report("T3", 5, Event::Bust),
      report("T4", 6, Event::New, 50),
  };

  TradeLedger l;
  size_t emitted = 0;
  for (const auto &r : day)
    emitted += l.apply(r).emit();
  EXPECT_EQ(emitted, day.size());
  const int64_t net = l.net_qty("INTC");
  EXPECT_EQ(net, 150 - 200 + 50);
  EXPECT_EQ(l.live(), 3u);

  // The venue replays the whole day at the next logon: nothing changes.
  for (const auto &r : day)
    EXPECT_EQ(l.apply(r).what, Applied::Duplicate);
  EXPECT_EQ(l.net_qty("INTC"), net);
  EXPECT_EQ(l.live(), 3u);

  // After a restart (an empty ledger) the replay rebuilds the same state.
  TradeLedger fresh;
  for (const auto &r : day)
    fresh.apply(r);
  EXPECT_EQ(fresh.net_qty("INTC"), net);
  EXPECT_EQ(fresh.live(), 3u);
  ASSERT_NE(fresh.find("T1"), nullptr);
  EXPECT_EQ(fresh.find("T1")->qty, 150);
  EXPECT_TRUE(fresh.removed("T3"));
}

TEST(TradeLedger, AmendBeforeItsTrade)
{
  TradeLedger l;
  // The amendment (seq 5) arrives before the trade (seq 2).
  auto a = l.apply(report("T1", 5, Event::Amend, 80, 1000100));
  EXPECT_EQ(a.what, Applied::Added) << "the amendment stands for the trade";
  auto n = l.apply(report("T1", 2, Event::New, 100, 1000000));
  EXPECT_EQ(n.what, Applied::Stale);
  EXPECT_FALSE(n.emit());
  ASSERT_NE(l.find("T1"), nullptr);
  EXPECT_EQ(l.find("T1")->qty, 80);
  EXPECT_EQ(l.find("T1")->price_mantissa, 1000100);
}

TEST(TradeLedger, OlderAmendAfterNewerIsStale)
{
  TradeLedger l;
  l.apply(report("T1", 1, Event::New, 100));
  EXPECT_EQ(l.apply(report("T1", 7, Event::Amend, 70)).what, Applied::Amended);
  EXPECT_EQ(l.apply(report("T1", 4, Event::Amend, 40)).what, Applied::Stale);
  EXPECT_EQ(l.find("T1")->qty, 70);
}

TEST(TradeLedger, BustBeforeItsTradeLeavesATombstone)
{
  TradeLedger l;
  auto b = l.apply(report("T1", 9, Event::Bust));
  EXPECT_EQ(b.what, Applied::Orphan);
  EXPECT_FALSE(b.emit());
  // The trade arrives late: it was busted, so it is not resurrected.
  EXPECT_EQ(l.apply(report("T1", 3)).what, Applied::Stale);
  EXPECT_EQ(l.apply(report("T1", 4, Event::Amend, 10)).what, Applied::Stale);
  EXPECT_EQ(l.live(), 0u);
  EXPECT_EQ(l.net_qty("INTC"), 0);
}

TEST(TradeLedger, SecondRemovalIsStale)
{
  TradeLedger l;
  l.apply(report("T1", 1));
  l.apply(report("T1", 2, Event::Cancel));
  EXPECT_EQ(l.apply(report("T1", 3, Event::Bust)).what, Applied::Stale);
}

TEST(TradeLedger, SecondNewForALiveTradeIsInvalid)
{
  TradeLedger l;
  l.apply(report("T1", 1, Event::New, 100));
  EXPECT_EQ(l.apply(report("T1", 2, Event::New, 999)).what, Applied::Invalid);
  EXPECT_EQ(l.find("T1")->qty, 100);
}

TEST(TradeLedger, MissingIdsAreInvalid)
{
  TradeLedger l;
  auto r = report("", 1);
  EXPECT_EQ(l.apply(r).what, Applied::Invalid);
  auto s = report("T1", 1);
  s.report_id.clear();
  EXPECT_EQ(l.apply(s).what, Applied::Invalid);
  EXPECT_EQ(l.reports(), 0u);
}

TEST(TradeLedger, WithoutVenueSequenceArrivalOrderWins)
{
  TradeLedger l;
  auto n = report("T1", 0, Event::New, 100);
  n.report_id = "a";
  auto a1 = report("T1", 0, Event::Amend, 90);
  a1.report_id = "b";
  auto a2 = report("T1", 0, Event::Amend, 80);
  a2.report_id = "c";
  l.apply(n);
  EXPECT_EQ(l.apply(a1).what, Applied::Amended);
  EXPECT_EQ(l.apply(a2).what, Applied::Amended);
  EXPECT_EQ(l.find("T1")->qty, 80);
}

// With no venue order, the New of a trade that an amendment created is its
// late New: Stale, and its report_id is recorded, so a resend is a Duplicate.
TEST(TradeLedger, WithoutVenueSequenceNewAfterItsAmendIsStale)
{
  TradeLedger l;
  auto a = report("T1", 0, Event::Amend, 80, 1000100);
  a.report_id = "a";
  auto n = report("T1", 0, Event::New, 100, 1000000);
  n.report_id = "b";
  EXPECT_EQ(l.apply(a).what, Applied::Added);
  auto r = l.apply(n);
  EXPECT_EQ(r.what, Applied::Stale);
  EXPECT_FALSE(r.emit());
  ASSERT_NE(l.find("T1"), nullptr);
  EXPECT_EQ(l.find("T1")->qty, 80);
  EXPECT_EQ(l.reports(), 2u);
  EXPECT_EQ(l.apply(n).what, Applied::Duplicate);
  // A second New for a trade whose last report was a New is still Invalid.
  auto n2 = report("T1", 0, Event::New, 999);
  n2.report_id = "c";
  TradeLedger m;
  auto n1 = report("T1", 0, Event::New, 100);
  n1.report_id = "d";
  m.apply(n1);
  EXPECT_EQ(m.apply(n2).what, Applied::Invalid);
}

TEST(TradeLedger, ClearStartsANewDay)
{
  TradeLedger l;
  l.apply(report("T1", 1));
  l.clear();
  EXPECT_EQ(l.live(), 0u);
  EXPECT_EQ(l.reports(), 0u);
  EXPECT_EQ(l.apply(report("T1", 1)).what, Applied::Added) << "same ids on a new day";
}
