/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * TachBook and OB -- trade prints of CME MBO fills (frame/ob/FillSlate.hpp).
 *
 * An MBO trade record has an orderID and a quantity, nothing else; price and
 * side come from the order in the book. CME lists every filled order, the
 * aggressor included, and an existing order modified to cross is still in
 * the book at its OLD price and side when the trade arrives -- its Change
 * follows the trade. Published from the book at the trade record, that fill
 * printed at the old limit, outside the bid/offer. And a snapshot re-sending
 * an order the book already held was ignored, so a moved order's later fills
 * printed at its stale price.
 *
 * These drive the real books through their Data handler, record by record, in
 * the order CME sends them (trade records before the book records of the same
 * transaction), and check what goes out as TradeNotify.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <memory>
#include <string>
#include <vector>

#include "actors/msg/Shutdown.hpp"
#include "bfile/r_l3.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ob/act/TachBook.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

constexpr double TICK = 0.015625;   // ZN
constexpr int NEW = 0, CHANGE = 1, DELETE = 2;
constexpr char BID = '0', OFFER = '1';

struct Print { double px; bool resting_buy; int qty; };

// Collects what a book publishes. The name admits it to TachBook's HI-prio
// subscriber list (subscribe_handler's ACL).
class Sink : public MockActor {
public:
  Sink() : MockActor("aggr_fill_test") {}
  std::vector<Print> prints() const {
    std::vector<Print> v;
    for (size_t i = 0; i < message_count(); ++i)
      if (auto t = get_message<ob::msg::TradeNotify>(i))
        v.push_back({double(t->payload->px.to_double()), t->payload->side == en::bs::BUY,
                     int(t->payload->sz)});
    return v;
  }
};

ref::Asset *make_asset(const std::string &name, int32_t secid) {
  auto a = ref::RefData::add_future_asset(name, en::x::CMEMD, secid, "FFDXSX", "ZN", TICK);
  a->maxpx = 11520;   // 180 points of ZN ticks
  return a;
}

// Feeds records to one book through its Data handler.
class Feeder {
public:
  explicit Feeder(actors::Actor *book, int32_t secid) : book_(book), secid_(secid) {}
  void mbo(uint64_t tx, int act, uint64_t oid, char side, double px, uint32_t qty,
           bool recovery = false) {
    bfile::l3_mbo_v2_t m{};
    m.typ = en::l3::MBO_V2;
    m.venue = char(en::x::CMEMD);
    m.transactTime = m.sendingTime = m.handlerendtim = tx;
    m.orderUpdateAction = uint8_t(act);
    m.securityID = secid_;
    m.orderID = oid;
    m.priority = oid;
    m.pxd = px;
    m.displayQty = qty;
    m.side = side;
    m.endOfEvent = true;
    m.recovery = recovery;
    feed(m);
  }
  void trade(uint64_t tx, uint64_t oid, int qty) {
    bfile::l3_mbo_trd_v2_t t{};
    t.venue = char(en::x::CMEMD);
    t.transactTime = t.sendingTime = t.handlerendtim = tx;
    t.orderID = oid;
    t.lastQty = qty;
    feed(t);
  }

private:
  void feed(const bfile::l3_t &rec) {
    mda::msg::Data d;
    d.l3 = rec;
    d.israw = true;
    TestHelper::invoke_handler(book_, &d, nullptr);
  }
  actors::Actor *book_;
  int32_t secid_;
};

class TachBookFillTest : public ::testing::Test {
protected:
  static int32_t next_secid;
  int32_t secid = 0;
  std::unique_ptr<ob::act::TachBook> tb;
  std::unique_ptr<Feeder> f;
  Sink sink;

  void SetUp() override {
    secid = next_secid++;   // a fresh instrument per test
    auto a = make_asset("ZNTB" + std::to_string(secid), secid);
    tb = std::make_unique<ob::act::TachBook>(a->id);
    auto sub = std::make_unique<mda::msg::Subscribe>();
    sub->prio = mda::msg::Subscribe::HI;
    TestHelper::invoke_handler(tb.get(), sub.get(), &sink);
    f = std::make_unique<Feeder>(tb.get(), secid);
    // a book with a 104.703125 bid and a 104.734375 offer behind the
    // orders a test adds
    f->mbo(1, NEW, 900, BID, 104.703125, 100);
    f->mbo(2, NEW, 901, OFFER, 104.734375, 100);
    sink.clear();
  }
};
int32_t TachBookFillTest::next_secid = 990001;

}  // namespace

// ZN 2026-10-08 order 8419050632586: a bid at 104.6875 raised to cross the
// offers at 104.71875, bought 17. CME lists it among the fills, before its
// own Change. Before the fix TachBook printed its fill as a resting bid
// filled at 104.6875 -- outside the bid/offer, on the wrong side.
TEST_F(TachBookFillTest, ModifiedAggressorIsNotPrinted) {
  f->mbo(10, NEW, 1, BID, 104.6875, 127);
  f->mbo(11, NEW, 2, OFFER, 104.71875, 14);
  f->mbo(12, NEW, 3, OFFER, 104.71875, 1);
  f->mbo(13, NEW, 4, OFFER, 104.71875, 1);
  f->mbo(14, NEW, 5, OFFER, 104.71875, 1);
  sink.clear();
  f->trade(100, 1, 17);
  f->trade(100, 2, 14);
  f->trade(100, 3, 1);
  f->trade(100, 4, 1);
  f->trade(100, 5, 1);
  f->mbo(100, DELETE, 2, OFFER, 104.71875, 14);
  f->mbo(100, DELETE, 3, OFFER, 104.71875, 1);
  f->mbo(100, DELETE, 4, OFFER, 104.71875, 1);
  f->mbo(100, DELETE, 5, OFFER, 104.71875, 1);
  f->mbo(100, CHANGE, 1, BID, 104.71875, 110);
  auto p = sink.prints();
  ASSERT_EQ(p.size(), 4u);
  int qty = 0;
  for (auto &x : p) {
    EXPECT_DOUBLE_EQ(x.px, 104.71875);
    EXPECT_FALSE(x.resting_buy) << "the resting offers were filled, not a bid";
    qty += x.qty;
  }
  EXPECT_EQ(qty, 17);
}

// A snapshot re-sends order 1 at a new price (the capture missed its live
// Change). Before the fix, orders.emplace kept the old record, and the
// order's later fill printed at the stale 104.6875.
TEST_F(TachBookFillTest, SnapshotReAddMovesTheOrder) {
  f->mbo(10, NEW, 1, BID, 104.6875, 10);
  f->mbo(20, NEW, 1, BID, 104.71875, 10, /*recovery=*/true);
  sink.clear();
  f->trade(30, 1, 4);   // a new seller hits it
  f->mbo(30, CHANGE, 1, BID, 104.71875, 6);
  auto p = sink.prints();
  ASSERT_EQ(p.size(), 1u);
  EXPECT_DOUBLE_EQ(p[0].px, 104.71875);
  EXPECT_TRUE(p[0].resting_buy);
  EXPECT_EQ(p[0].qty, 4);
}

// 1 vs 1, both orders simply Deleted. The bid sits at the best bid; the
// offer's stored price is off the touch -- it was modified down to cross,
// so it is the aggressor and only the bid's fill is printed.
TEST_F(TachBookFillTest, OneVsOneDecidedByTouch) {
  f->mbo(10, NEW, 1, BID, 104.71875, 5);    // the new best bid
  f->mbo(11, NEW, 2, OFFER, 104.75, 5);     // behind the 104.734375 offer
  sink.clear();
  f->trade(50, 1, 5);
  f->trade(50, 2, 5);
  f->mbo(50, DELETE, 1, BID, 104.71875, 5);
  f->mbo(50, DELETE, 2, OFFER, 104.75, 5);
  auto p = sink.prints();
  ASSERT_EQ(p.size(), 1u);
  EXPECT_DOUBLE_EQ(p[0].px, 104.71875);
  EXPECT_TRUE(p[0].resting_buy);
}

// A new aggressor (not in the book) sweeping two levels: unchanged, every
// resting fill printed where it rested.
TEST_F(TachBookFillTest, NewAggressorSweepUnchanged) {
  f->mbo(10, NEW, 2, OFFER, 104.71875, 3);
  sink.clear();
  f->trade(60, 2, 3);
  f->trade(60, 901, 2);
  f->trade(60, 777, 5);   // the aggressor: not in the book
  f->mbo(60, DELETE, 2, OFFER, 104.71875, 3);
  f->mbo(60, CHANGE, 901, OFFER, 104.734375, 98);
  auto p = sink.prints();
  ASSERT_EQ(p.size(), 2u);
  EXPECT_DOUBLE_EQ(p[0].px, 104.71875);
  EXPECT_DOUBLE_EQ(p[1].px, 104.734375);
  EXPECT_FALSE(p[0].resting_buy);
  EXPECT_FALSE(p[1].resting_buy);
}

// An undecided fill (1 vs 1, both at the touch, both Deleted) is held. A
// trade record of a later transaction -- for an order this book does not
// have -- releases it, not this instrument's next book record.
TEST_F(TachBookFillTest, HeldFillReleasedByLaterTradeRecord) {
  f->mbo(10, NEW, 1, BID, 104.71875, 2);
  f->mbo(13, NEW, 2, OFFER, 104.734375, 2);   // at the best offer
  sink.clear();
  f->trade(70, 1, 2);
  f->trade(70, 2, 2);
  f->mbo(70, DELETE, 1, BID, 104.71875, 2);
  f->mbo(70, DELETE, 2, OFFER, 104.734375, 2);
  EXPECT_TRUE(sink.prints().empty()) << "undecided: held";
  f->trade(80, 99999, 1);
  EXPECT_EQ(sink.prints().size(), 2u);
}

// Fills still held at shutdown go out instead of being lost.
TEST_F(TachBookFillTest, ShutdownFlushesHeldFills) {
  f->mbo(10, NEW, 1, BID, 104.71875, 2);
  f->mbo(13, NEW, 2, OFFER, 104.734375, 2);
  sink.clear();
  f->trade(70, 1, 2);
  f->trade(70, 2, 2);
  f->mbo(70, DELETE, 1, BID, 104.71875, 2);
  f->mbo(70, DELETE, 2, OFFER, 104.734375, 2);
  EXPECT_TRUE(sink.prints().empty());
  actors::msg::Shutdown sd;
  TestHelper::invoke_handler(tb.get(), &sd, nullptr);
  EXPECT_EQ(sink.prints().size(), 2u);
}

// ---------------------------------------------------------------------------
// OB
// ---------------------------------------------------------------------------
namespace {

class OBFillTest : public ::testing::Test {
protected:
  static int32_t next_secid;
  int32_t secid = 0;
  boost::property_tree::ptree pt;
  std::unique_ptr<ob::act::OB> ob;
  std::unique_ptr<Feeder> f;
  Sink sink;

  void SetUp() override {
    secid = next_secid++;
    auto a = make_asset("ZNOB" + std::to_string(secid), secid);
    ob = std::make_unique<ob::act::OB>(nullptr, false, nullptr, a->id, pt);
    auto sub = new mda::msg::Subscribe();
    sub->prio = mda::msg::Subscribe::HI;
    ob->fast_send(sub, &sink);   // OB takes Subscribe in process_message
    f = std::make_unique<Feeder>(ob.get(), secid);
  }
};
int32_t OBFillTest::next_secid = 995001;

}  // namespace

// The modified aggressor of ModifiedAggressorIsNotPrinted, through OB: its
// own EXEC is dropped; the four resting offers are processed and printed.
TEST_F(OBFillTest, ModifiedAggressorIsNotPrinted) {
  f->mbo(1, NEW, 901, OFFER, 104.734375, 100);
  f->mbo(10, NEW, 1, BID, 104.6875, 127);
  f->mbo(11, NEW, 2, OFFER, 104.71875, 14);
  f->mbo(12, NEW, 3, OFFER, 104.71875, 1);
  f->mbo(13, NEW, 4, OFFER, 104.71875, 1);
  f->mbo(14, NEW, 5, OFFER, 104.71875, 1);
  sink.clear();
  f->trade(100, 1, 17);
  f->trade(100, 2, 14);
  f->trade(100, 3, 1);
  f->trade(100, 4, 1);
  f->trade(100, 5, 1);
  f->mbo(100, DELETE, 2, OFFER, 104.71875, 14);
  f->mbo(100, DELETE, 3, OFFER, 104.71875, 1);
  f->mbo(100, DELETE, 4, OFFER, 104.71875, 1);
  f->mbo(100, DELETE, 5, OFFER, 104.71875, 1);
  f->mbo(100, CHANGE, 1, BID, 104.71875, 110);
  auto p = sink.prints();
  ASSERT_EQ(p.size(), 4u);
  for (auto &x : p) {
    EXPECT_DOUBLE_EQ(x.px, 104.71875);
    EXPECT_FALSE(x.resting_buy);
  }
}

// Order 1 rests at 104.6875; a snapshot rebuilds the book with it there,
// then re-sends it at 104.703125 (it moved; the capture missed the live
// Change). Before the fix OB kept the OLD stored price and printed the later
// fill at 104.6875.
TEST_F(OBFillTest, SnapshotReAddUpdatesStoredPrice) {
  f->mbo(1000, NEW, 900, OFFER, 104.734375, 100);
  f->mbo(1001, NEW, 1, BID, 104.6875, 10);
  f->mbo(2000, NEW, 900, OFFER, 104.734375, 100, true);   // OB clears and rebuilds
  f->mbo(2001, NEW, 1, BID, 104.6875, 10, true);
  f->mbo(2002, NEW, 1, BID, 104.703125, 10, true);        // re-sent, moved
  sink.clear();
  f->trade(3000, 1, 4);
  f->mbo(3000, CHANGE, 1, BID, 104.703125, 6);
  auto p = sink.prints();
  ASSERT_EQ(p.size(), 1u);
  EXPECT_DOUBLE_EQ(p[0].px, 104.703125);
  EXPECT_TRUE(p[0].resting_buy);
  EXPECT_EQ(p[0].qty, 4);
}
