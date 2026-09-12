/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * OB's delay queue — the simulator's entire latency model.
 *
 * OB does not apply our own orders to the book when it receives them. It parks
 * them on del_q (OB.cpp:1507, keyed off `israw == false`) and releases each one
 * only once
 *
 *     payload->ts0 + max(40, delay) * 1000 ns
 *
 * has passed in MARKET time — the transactTime of the next real record. That
 * is what makes the fill model honest: everything the market does in that
 * window gets in front of us.
 *
 * Nothing tested it. `set_delay()` existed with no caller, so every run used
 * the hardcoded 1000 us, and the cancel path fed it a timestamp that had
 * already expired (see test_som_cancel_latency.cpp), which no test noticed
 * either.
 *
 * These tests drive a real OB with real MBO records and observe the book
 * through the one public channel it has — cons::msg::Get("bbbo") — so they
 * assert on what a consumer of the book actually sees, not on internals.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <map>
#include <string>
#include <utility>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

// OB::get_handler replies, and Actor::reply() asserts on a null return
// address. The run loop sets reply_to before dispatch; TestHelper does not.
// reply_to is protected in actors::Actor, so a using-declaration reaches it.
struct TestableOB : public ob::act::OB {
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;
};

constexpr uint64_t kT0       = 1736960000000000000ULL;  // epoch ns
constexpr int      kDelayUs  = 1000;                    // OB's own default
constexpr uint64_t kDelayNs  = uint64_t(kDelayUs) * 1000;

constexpr int kBidPx = 23990;
constexpr int kAskPx = 24010;
constexpr int kOurPx = 24000;   // inside the spread, so it moves the BBO

class OBDelayQueueTest : public ::testing::Test {
protected:
  MockActor probe{"probe"};
  boost::property_tree::ptree pt;
  std::unique_ptr<TestableOB> ob;
  int sym = 0;
  uint64_t next_ex_order_id = 1000;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  void SetUp() override {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr) << "fixture universe must contain ESH6";
    sym = asset->id;

    ob = std::make_unique<TestableOB>(nullptr, true /*do_cross_check*/,
                                      nullptr /*manager*/, sym, pt);
    probe.clear();
  }

  void TearDown() override { probe.clear(); }

  void dispatch(const actors::Message* m) {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), m, &probe);
    ob->reply_to = nullptr;
  }

  // A real exchange MBO record. `israw` is what tells OB this is market data
  // and not one of our own orders, so it bypasses del_q and is applied at once
  // -- and, on the way in, drains whatever on del_q has now come due.
  void market_add(en::bs side, int px, uint32_t qty, uint64_t txtim,
                  uint64_t* out_ex_order_id = nullptr) {
    const uint64_t exid = next_ex_order_id++;
    if (out_ex_order_id) *out_ex_order_id = exid;

    bfile::l3_mbo_v2_t mbo{};
    mbo.typ               = en::l3::MBO_V2;
    mbo.venue             = char(en::x::CMEMD);   // handler_if stores the en::x value
    mbo.transactTime      = txtim;
    mbo.sendingTime       = txtim;
    mbo.orderUpdateAction = 0;   // ADD
    mbo.orderID           = exid;
    mbo.priority          = exid;
    mbo.pxd               = double(px);
    mbo.displayQty        = qty;
    mbo.side              = (side == en::bs::BUY) ? '0' : '1';  // MDP3 coding
    mbo.endOfEvent        = true;

    auto d = std::make_unique<mda::msg::Data>();
    d->israw  = true;
    d->l3     = mbo;
    d->payload = mda::msg::data_pay_load::make_payload(
        txtim, side, exid, mda::OrderID::longid(en::x::CMEMD, exid), sym,
        en::md::ADD, en::mt::NONE, qty, qty, double(px), txtim, txtim,
        en::x::CMEMD, true /*eoe*/, false /*recovery*/);
    dispatch(d.get());
  }

  // One of OUR orders, shaped the way SOM shapes it: israw false, l3_sim_t,
  // venue SIM, and ts0 = the market time we decided to act.
  void our_order(en::bs side, int px, uint32_t qty, uint64_t ts0, uint oid,
                 en::md mev, en::mt action) {
    auto d = std::make_unique<mda::msg::Data>();
    d->israw = false;
    d->l3    = bfile::l3_sim_t();   // exactly as SOM builds it

    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(
        new mda::msg::data_pay_load());
    payload->mkt        = en::x::SIM;
    payload->sym        = sym;
    payload->px         = ref::Price(px, sym);
    payload->sz         = qty;
    payload->disp_sz    = (action == en::mt::CANCD) ? 0 : qty;
    payload->side       = side;
    payload->order_ref  = mda::OrderID::longid(en::x::SIM, oid);
    payload->action     = action;
    payload->ot         = en::ot::LIMIT;
    payload->mev        = mev;
    payload->owner      = en::trader::SIMULATOR;
    payload->ts0        = ts0;
    d->payload = payload;
    dispatch(d.get());
  }

  void place_ours(int px, uint64_t ts0, uint oid = 1, uint32_t qty = 1) {
    our_order(en::bs::BUY, px, qty, ts0, oid, en::md::ADD, en::mt::NONE);
  }

  void cancel_ours(int px, uint64_t ts0, uint oid = 1, uint32_t qty = 1) {
    our_order(en::bs::BUY, px, qty, ts0, oid, en::md::MOD, en::mt::CANCD);
  }

  // The market BBO, which deliberately EXCLUDES our own orders.
  std::pair<int, int> bbo() {
    probe.clear();
    cons::msg::Get g("bbbo");
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    if (!page) return {-1, -1};
    int bid = -1, ask = -1;
    std::sscanf(page->val.c_str(), "BBBO: %d %d", &bid, &ask);
    return {bid, ask};
  }

  // Our own resting size at a price. This is the observable that matters:
  // "bbbo" cannot answer it, because sim orders are kept out of the BBO so a
  // strategy cannot react to its own quote.
  int our_size_at(int px, en::bs side = en::bs::BUY) {
    probe.clear();
    cons::msg::Get g("qat", {{"side", side == en::bs::BUY ? "B" : "S"},
                             {"px", std::to_string(px)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    if (!page) return -1;
    int sz = -1, cnt = -1, simsz = -1;
    std::sscanf(page->val.c_str(), "QAT: %d %d %d", &sz, &cnt, &simsz);
    return simsz;
  }

  // Build a two-sided book with a 20-tick spread, wide enough that our order
  // can sit inside it without locking or crossing.
  void seed_book(uint64_t txtim) {
    market_add(en::bs::BUY, kBidPx, 10, txtim);
    market_add(en::bs::SEL, kAskPx, 10, txtim + 1);
  }
};

TEST_F(OBDelayQueueTest, SeededBookHasTheExpectedBbo) {
  seed_book(kT0);
  EXPECT_EQ(bbo(), std::make_pair(kBidPx, kAskPx));
  EXPECT_EQ(our_size_at(kOurPx), 0) << "we have not placed anything yet";
}

// Guards the observation channel itself: if our order ever started showing up
// in the BBO, every timing test below would be measuring the wrong thing.
TEST_F(OBDelayQueueTest, OurOwnOrderIsNotVisibleInTheMarketBbo) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);

  ASSERT_EQ(our_size_at(kOurPx), 1) << "precondition: our order is resting";
  EXPECT_EQ(bbo().first, kBidPx)
      << "a strategy must not see its own quote as the market bid";
}

// The core of the model: our order is NOT in the book the instant OB gets it.
TEST_F(OBDelayQueueTest, OurOrderIsWithheldUntilTheDelayHasElapsed) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "our order must not be in the book before any time has passed";

  // A record arriving mid-flight: still not ours.
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs / 2);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "released after only half the wire latency";

  // A record past the deadline drains it.
  market_add(en::bs::SEL, kAskPx + 6, 1, sent + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "our order should be in the book once the latency has elapsed";
}

// The regression test_som_cancel_latency.cpp pins at the SOM end, observed
// here at the book end: a cancel stamped with the ORIGINAL order's ts0 has a
// deadline that is already past, so it takes effect on the very next record.
TEST_F(OBDelayQueueTest, OurCancelIsWithheldForTheSameDelay) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1) << "precondition: our order is resting";

  const uint64_t cancelled_at = sent + kDelayNs + 100;
  cancel_ours(kOurPx, cancelled_at);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "cancel must not take effect on the message that carried it";

  market_add(en::bs::SEL, kAskPx + 6, 1, cancelled_at + kDelayNs / 2);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "cancel took effect after only half the wire latency -- this is what "
         "stamping it with the original order's ts0 used to do";

  market_add(en::bs::SEL, kAskPx + 7, 1, cancelled_at + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "cancel should take effect once its own latency has elapsed";
}

// The knob has to move the threshold, or it is decorative.
TEST_F(OBDelayQueueTest, SetDelayChangesHowLongTheOrderIsHeld) {
  constexpr int kSlowUs = 50000;              // 50 ms
  constexpr uint64_t kSlowNs = uint64_t(kSlowUs) * 1000;

  ob->set_delay(kSlowUs);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);

  // Past the DEFAULT deadline, nowhere near the configured one.
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "released at the default 1000us, so set_delay() did nothing";

  market_add(en::bs::SEL, kAskPx + 6, 1, sent + kSlowNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1);
}

// 40 us is a hard floor: max(40, delay). A zero or silly-small delay must not
// turn into "arrives instantly", which would quietly restore the old bug.
TEST_F(OBDelayQueueTest, DelayHasAFortyMicrosecondFloor) {
  ob->set_delay(0);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + 20000);   // +20 us
  EXPECT_EQ(our_size_at(kOurPx), 0) << "20us is inside the 40us floor";

  market_add(en::bs::SEL, kAskPx + 6, 1, sent + 41000);   // +41 us
  EXPECT_EQ(our_size_at(kOurPx), 1);
}

// An order and its cancel share a wire, so cancel_delay defaults to -1 (=
// delay). Set it and only cancels should move. It may only be made LONGER --
// del_q is FIFO, so a shorter cancel cannot overtake an order still in flight
// and set_cancel_delay() rejects it.
TEST_F(OBDelayQueueTest, CancelDelayCanBeMadeAsymmetric) {
  constexpr int kSlowCancelUs = 50000;
  constexpr uint64_t kSlowCancelNs = uint64_t(kSlowCancelUs) * 1000;

  ob->set_cancel_delay(kSlowCancelUs);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  // Placement still uses the ordinary delay.
  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1) << "cancel_delay must not slow placement down";

  const uint64_t cancelled_at = sent + kDelayNs + 100;
  cancel_ours(kOurPx, cancelled_at);

  market_add(en::bs::SEL, kAskPx + 6, 1, cancelled_at + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "cancel used the order delay, not the configured cancel delay";

  market_add(en::bs::SEL, kAskPx + 7, 1, cancelled_at + kSlowCancelNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0);
}

}  // namespace
