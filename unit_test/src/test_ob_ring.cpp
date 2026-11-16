/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * OB with a ring price ladder (`ladder_ticks` in the ctor ptree).
 *
 * The ring holds N slots per side. Each side's window hangs from its top -- the
 * best price holding any order, real or ours:
 *
 *     bid: [bid_top - N + 1, bid_top]      ask: [ask_bot, ask_bot + N - 1]
 *
 * These tests run N = 16 and pin each way the window moves: a new top flushes
 * the far end, a side that backs off frees its slots for deeper prices, an ADD
 * below the window is dropped, a jump of N or more flushes everything. They
 * observe the book through the console query the other OB tests use, and our
 * own orders through the CancAck OB sends back.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/Fill.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

struct TestableOB : public ob::act::OB {
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;
};

constexpr uint64_t kT0 = 1736960000000000000ULL;   // epoch ns
constexpr uint64_t kDelayNs = 1000ULL * 1000;     // OB's default order latency
constexpr int kN = 16;

class OBRingTest : public ::testing::Test {
protected:
  MockActor probe{"probe"};
  boost::property_tree::ptree pt;
  std::unique_ptr<TestableOB> ob;
  int sym = 0;
  uint64_t next_oid = 1000;
  uint64_t now = kT0;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  void SetUp() override {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr);
    sym = asset->id;
    pt.put("ladder_ticks", kN);
    ob = std::make_unique<TestableOB>(nullptr, true /*do_cross_check*/,
                                      nullptr /*manager*/, sym, pt);
    probe.clear();
  }

  void dispatch(const actors::Message* m) {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), m, &probe);
    ob->reply_to = nullptr;
  }

  // One exchange MBO record, each in its own transaction. 0 ADD, 2 DELETE.
  uint64_t mbo(int action, en::bs side, int px, uint32_t qty, uint64_t oid = 0) {
    if (oid == 0) oid = next_oid++;
    now += 1000;
    bfile::l3_mbo_v2_t r{};
    r.typ               = en::l3::MBO_V2;
    r.venue             = char(en::x::CMEMD);
    r.transactTime      = now;
    r.sendingTime       = now;
    r.orderUpdateAction = uint8_t(action);
    r.orderID           = oid;
    r.priority          = oid;
    r.pxd               = double(px);
    r.displayQty        = qty;
    r.side              = (side == en::bs::BUY) ? '0' : '1';
    r.endOfEvent        = true;

    auto d = std::make_unique<mda::msg::Data>();
    d->israw   = true;
    d->l3      = r;
    d->payload = mda::msg::data_pay_load::make_payload(
        now, side, oid, mda::OrderID::longid(en::x::CMEMD, oid), sym,
        action == 0 ? en::md::ADD : en::md::DEL, en::mt::NONE, qty, qty,
        double(px), now, now, en::x::CMEMD, true, false);
    dispatch(d.get());
    return oid;
  }
  uint64_t add(en::bs side, int px, uint32_t qty = 5) { return mbo(0, side, px, qty); }
  void del(en::bs side, int px, uint64_t oid, uint32_t qty = 5) { mbo(2, side, px, qty, oid); }

  // One of our orders, shaped as SOM sends it, then a market record far
  // enough in market time to release it from OB's delay queue. The release
  // record is a 1-lot ask at `release_px`, which must already be inside the
  // ask window.
  void ours(en::bs side, int px, uint oid, int release_px) {
    auto d = std::make_unique<mda::msg::Data>();
    d->israw = false;
    d->l3    = bfile::l3_sim_t();
    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(
        new mda::msg::data_pay_load());
    payload->mkt       = en::x::SIM;
    payload->sym       = sym;
    payload->px        = ref::Price(px, sym);
    payload->sz        = 1;
    payload->disp_sz   = 1;
    payload->side      = side;
    payload->order_ref = mda::OrderID::longid(en::x::SIM, oid);
    payload->action    = en::mt::NONE;
    payload->ot        = en::ot::LIMIT;
    payload->mev       = en::md::ADD;
    payload->owner     = en::trader::SIMULATOR;
    payload->ts0       = now;
    d->payload = payload;
    dispatch(d.get());
    now += kDelayNs;
    add(en::bs::SEL, release_px, 1);
  }

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

  struct Level { int sz = -1, cnt = -1, sim = -1; };
  Level level(en::bs side, int px) {
    probe.clear();
    cons::msg::Get g("qat", {{"side", side == en::bs::BUY ? "B" : "S"},
                             {"px", std::to_string(px)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    Level l;
    if (page) std::sscanf(page->val.c_str(), "QAT: %d %d %d", &l.sz, &l.cnt, &l.sim);
    return l;
  }
};

// ---------------------------------------------------------------------------
// The window follows a new top
// ---------------------------------------------------------------------------

TEST_F(OBRingTest, ANewBestBidFlushesTheDeepestBids) {
  add(en::bs::SEL, 120);
  add(en::bs::BUY, 100);
  const auto deep = add(en::bs::BUY, 86);   // window [85, 100]
  add(en::bs::BUY, 90);
  ASSERT_EQ(level(en::bs::BUY, 86).sz, 5);

  add(en::bs::BUY, 103);                     // window [88, 103]: 85..87 leave

  EXPECT_EQ(level(en::bs::BUY, 86).sz, 0) << "86 fell off the bottom of the window";
  EXPECT_EQ(level(en::bs::BUY, 90).sz, 5) << "90 is still inside it";
  EXPECT_EQ(level(en::bs::BUY, 100).sz, 5);
  EXPECT_EQ(bbo(), std::make_pair(103, 120));

  // The exchange still thinks the flushed order rests; its delete is a no-op.
  del(en::bs::BUY, 86, deep);
  EXPECT_EQ(bbo(), std::make_pair(103, 120));
  EXPECT_EQ(level(en::bs::BUY, 90).sz, 5);
}

TEST_F(OBRingTest, ANewBestAskFlushesTheHighestAsks) {
  add(en::bs::BUY, 80);
  add(en::bs::SEL, 100);
  add(en::bs::SEL, 114);                     // window [100, 115]
  add(en::bs::SEL, 110);

  add(en::bs::SEL, 97);                      // window [97, 112]: 113..115 leave

  EXPECT_EQ(level(en::bs::SEL, 114).sz, 0);
  EXPECT_EQ(level(en::bs::SEL, 110).sz, 5);
  EXPECT_EQ(bbo(), std::make_pair(80, 97));
}

TEST_F(OBRingTest, AnAddBelowTheWindowIsDropped) {
  add(en::bs::SEL, 120);
  add(en::bs::BUY, 100);                     // window [85, 100]

  const auto oid = add(en::bs::BUY, 84);
  EXPECT_EQ(level(en::bs::BUY, 84).sz, 0);
  EXPECT_EQ(level(en::bs::BUY, 100).sz, 5) << "84 shares 100's slot and must not land in it";

  add(en::bs::BUY, 85);
  EXPECT_EQ(level(en::bs::BUY, 85).sz, 5) << "the bottom of the window is accepted";

  del(en::bs::BUY, 84, oid);                 // never placed: a no-op
  EXPECT_EQ(bbo(), std::make_pair(100, 120));
}

TEST_F(OBRingTest, AJumpOfNTicksOrMoreFlushesTheWholeSide) {
  add(en::bs::SEL, 200);
  add(en::bs::BUY, 100);
  add(en::bs::BUY, 95);

  add(en::bs::BUY, 100 + kN);

  EXPECT_EQ(level(en::bs::BUY, 100).sz, 0);
  EXPECT_EQ(level(en::bs::BUY, 95).sz, 0);
  EXPECT_EQ(bbo(), std::make_pair(100 + kN, 200));
}

// ---------------------------------------------------------------------------
// A side that backs off frees its slots
// ---------------------------------------------------------------------------

TEST_F(OBRingTest, WhenTheBidBacksOffDeeperPricesTakeTheFreedSlots) {
  add(en::bs::SEL, 120);
  const auto top = add(en::bs::BUY, 100);    // window [85, 100]
  add(en::bs::BUY, 95);

  del(en::bs::BUY, 100, top);                // the bid backs off to 95
  ASSERT_EQ(bbo().first, 95);

  add(en::bs::BUY, 84);                      // window settles to [80, 95]

  EXPECT_EQ(level(en::bs::BUY, 84).sz, 5);
  EXPECT_EQ(level(en::bs::BUY, 95).sz, 5);
  EXPECT_EQ(bbo(), std::make_pair(95, 120));
}

TEST_F(OBRingTest, WhenTheAskBacksOffHigherPricesTakeTheFreedSlots) {
  add(en::bs::BUY, 80);
  const auto bot = add(en::bs::SEL, 100);    // window [100, 115]
  add(en::bs::SEL, 105);

  del(en::bs::SEL, 100, bot);
  ASSERT_EQ(bbo().second, 105);

  add(en::bs::SEL, 118);                     // window settles to [105, 120]

  EXPECT_EQ(level(en::bs::SEL, 118).sz, 5);
  EXPECT_EQ(level(en::bs::SEL, 105).sz, 5);
  EXPECT_EQ(bbo(), std::make_pair(80, 105));
}

TEST_F(OBRingTest, AnEmptiedSideIsReseededAnywhere) {
  add(en::bs::SEL, 600);
  const auto a = add(en::bs::BUY, 100);
  const auto b = add(en::bs::BUY, 99);
  del(en::bs::BUY, 100, a);
  del(en::bs::BUY, 99, b);

  add(en::bs::BUY, 500);

  EXPECT_EQ(level(en::bs::BUY, 500).sz, 5);
  EXPECT_EQ(bbo(), std::make_pair(500, 600));
}

// ---------------------------------------------------------------------------
// Our own orders
// ---------------------------------------------------------------------------

// A bid of ours inside the spread is not part of the BBO, but it holds an
// order, so it is the top of the bid window.
TEST_F(OBRingTest, OurBidInsideTheSpreadBecomesTheTopOfTheWindow) {
  add(en::bs::SEL, 120);
  add(en::bs::BUY, 100);
  add(en::bs::BUY, 89);                      // window [85, 100]

  ours(en::bs::BUY, 105, 1, 121);            // window [90, 105]: 85..89 leave

  EXPECT_EQ(level(en::bs::BUY, 105).sim, 1) << "our bid rests at 105";
  EXPECT_EQ(level(en::bs::BUY, 89).sz, 0) << "89 fell off the bottom";
  EXPECT_EQ(bbo(), std::make_pair(100, 120)) << "the BBO is real orders only";
}

// A bid of ours at the ask fills on arrival and never rests, so it does not
// move the window.
TEST_F(OBRingTest, OurBidThatCrossesFillsAndMovesNothing) {
  add(en::bs::SEL, 110);
  add(en::bs::BUY, 100);
  add(en::bs::BUY, 86);                      // window [85, 100]

  ours(en::bs::BUY, 110, 1, 111);

  EXPECT_TRUE(probe.has_message_of_type<som::msg::Fill>()) << "filled on arrival";
  EXPECT_EQ(level(en::bs::BUY, 86).sz, 5) << "nothing was flushed";
}

// The real bid backs off below our bid: the window stays hung from ours, so a
// price more than N below it is still too deep.
TEST_F(OBRingTest, TheTopStaysAtOurBidWhenTheRealBidBacksOffBelowIt) {
  add(en::bs::SEL, 120);
  const auto top = add(en::bs::BUY, 100);
  add(en::bs::BUY, 95);
  ours(en::bs::BUY, 102, 1, 121);            // window [87, 102]

  del(en::bs::BUY, 100, top);
  ASSERT_EQ(bbo().first, 95);

  add(en::bs::BUY, 87);
  add(en::bs::BUY, 86);

  EXPECT_EQ(level(en::bs::BUY, 87).sz, 5);
  EXPECT_EQ(level(en::bs::BUY, 86).sz, 0) << "below the window under our bid";
  EXPECT_EQ(level(en::bs::BUY, 102).sim, 1) << "our bid is untouched";
}

// Ours at a price that leaves the window is cancelled back, so SOM and the
// light never hold an order the book no longer has.
TEST_F(OBRingTest, OurOrderFlushedOffTheWindowIsCancelledBack) {
  add(en::bs::SEL, 120);
  add(en::bs::BUY, 100);                     // window [85, 100]
  ours(en::bs::BUY, 90, 7, 121);
  ASSERT_EQ(level(en::bs::BUY, 90).sim, 1);

  probe.clear();
  add(en::bs::BUY, 106);                     // window [91, 106]: 90 leaves

  auto ack = probe.get_message<som::msg::CancAck>(0);
  ASSERT_NE(ack, nullptr) << "our order must be cancelled back, not dropped";
  EXPECT_EQ(ack->id, 7u);
  EXPECT_EQ(level(en::bs::BUY, 90).sim, 0);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

TEST_F(OBRingTest, LadderTicksMustBeAPowerOfTwo) {
  boost::property_tree::ptree bad;
  bad.put("ladder_ticks", 12);
  EXPECT_DEATH(TestableOB(nullptr, true, nullptr, sym, bad), "power of two");
}

}  // namespace
