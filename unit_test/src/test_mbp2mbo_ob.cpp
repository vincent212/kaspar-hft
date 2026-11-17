/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Mbp2Mbo into a real OB: what bnsim does, without the network.
 *
 * The asset is registered as bnsim registers it: units 1 (prices arrive in
 * ticks), maxpx far above any price, and a ring ladder.
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
#include "frame/som/msg/Fill.hpp"
#include "sim/Mbp2Mbo.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

struct TestableOB : public ob::act::OB {
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;
};

constexpr uint64_t kT0 = 1791811800000000000ULL;
constexpr uint64_t kDelayNs = 1000ULL * 1000;   // OB's default order latency

class Mbp2MboOBTest : public ::testing::Test {
protected:
  MockActor probe{"probe"};
  std::unique_ptr<TestableOB> ob;
  std::unique_ptr<sim::Mbp2Mbo> m;
  int sym = 0;
  uint64_t now = kT0;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  // BTC spot as bnsim sets it up: tick 0.01, so 64,000 USD is tick 6,400,000.
  void SetUp() override {
    ref::RefData::inst();   // add_future_asset does not construct RefData itself
    auto a = ref::RefData::add_future_asset("BTCUSDT_TEST", en::x::BINANCE, 991, "",
                                            "BTCUSDT", 1.0);
    ASSERT_NE(a, nullptr);
    a->maxpx = 1 << 30;
    sym = a->id;
    boost::property_tree::ptree pt;
    pt.put("ladder_ticks", 4096);
    ob = std::make_unique<TestableOB>(nullptr, true, nullptr, sym, pt);
    m = std::make_unique<sim::Mbp2Mbo>(
        sim::Mbp2Mbo::Config{991, char(en::x::BINANCE), 0.01, 0.01},
        [this](const bfile::l3_t &r) {
          mda::msg::Data d(true);
          d.l3 = r;
          dispatch(&d);
        });
    m->start(now);
    probe.clear();
  }

  void dispatch(const actors::Message* msg) {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), msg, &probe);
    ob->reply_to = nullptr;
  }

  void level(bool bid, double px, double qty) {
    now += 1000000;
    m->level(bid, px, qty, now);
    m->end_event();
  }
  void trade(double px, double qty, bool buyer_is_maker) {
    now += 1000000;
    m->trade(px, qty, buyer_is_maker, now);
    m->end_event();
  }

  // One of our 1-chunk orders, then time enough for OB's delay queue to let it in.
  void ours_buy(int px_ticks, uint oid) {
    auto d = std::make_unique<mda::msg::Data>();
    d->israw = false;
    d->l3 = bfile::l3_sim_t();
    auto p = boost::intrusive_ptr<mda::msg::data_pay_load>(new mda::msg::data_pay_load());
    p->mkt = en::x::SIM;
    p->sym = sym;
    p->px = ref::Price(px_ticks, sym);
    p->sz = 1;
    p->disp_sz = 1;
    p->side = en::bs::BUY;
    p->order_ref = mda::OrderID::longid(en::x::SIM, oid);
    p->action = en::mt::NONE;
    p->ot = en::ot::LIMIT;
    p->mev = en::md::ADD;
    p->owner = en::trader::SIMULATOR;
    p->ts0 = now;
    d->payload = p;
    dispatch(d.get());
    now += kDelayNs;
  }

  struct Level { int sz = -1, cnt = -1, sim = -1; };
  Level qat(bool bid, int64_t t) {
    probe.clear();
    cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    Level l;
    if (page) std::sscanf(page->val.c_str(), "QAT: %d %d %d", &l.sz, &l.cnt, &l.sim);
    return l;
  }
  std::pair<int, int> bbo() {
    probe.clear();
    cons::msg::Get g("bbbo");
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    int b = -1, a = -1;
    if (page) std::sscanf(page->val.c_str(), "BBBO: %d %d", &b, &a);
    return {b, a};
  }
};

TEST_F(Mbp2MboOBTest, OBHoldsWhatMbp2MboHolds) {
  level(true, 64000.00, 0.03);
  level(true, 63999.99, 0.015);
  level(false, 64000.01, 0.05);
  trade(64000.01, 0.02, false);
  level(false, 64000.01, 0.04);

  for (auto [bid, t] : {std::pair{true, int64_t(6400000)}, {true, 6399999}, {false, 6400001}})
    EXPECT_EQ(qat(bid, t).sz, int(m->chunks(bid, t))) << (bid ? "bid " : "ask ") << t;
  EXPECT_EQ(qat(false, 6400001).sz, 4);
  EXPECT_EQ(bbo(), std::make_pair(6400000, 6400001));
}

// Our bid joins the back of 3 chunks. The 3 ahead trade without filling us;
// the next chunk to trade at the level -- one that joined behind us -- does,
// because OB fills our order when an execution reaches the real order behind it.
TEST_F(Mbp2MboOBTest, OurOrderFillsOnlyAfterTheChunksAheadOfItTrade) {
  level(false, 64000.05, 0.10);
  level(true, 64000.00, 0.03);
  ours_buy(6400000, 1);
  level(true, 64000.00, 0.04);   // one more chunk, behind us
  ASSERT_EQ(qat(true, 6400000).sim, 1);

  probe.clear();
  for (int i = 0; i < 3; ++i) trade(64000.00, 0.01, true);
  EXPECT_FALSE(probe.has_message_of_type<som::msg::Fill>()) << "3 chunks were ahead of us";

  trade(64000.00, 0.01, true);
  EXPECT_TRUE(probe.has_message_of_type<som::msg::Fill>());
}

// BTC at 0.01 is ten million ticks; the ring holds 4096 of them and follows the
// market as it moves several windows' worth.
TEST_F(Mbp2MboOBTest, TheRingFollowsAMarketThatMovesFartherThanItsSize) {
  double bid = 64000.00;
  for (int step = 0; step < 50; ++step) {
    if (step) {                            // the old levels go first, so nothing crosses
      level(false, bid - 10.0 + 0.05, 0);
      level(true, bid - 10.0, 0);
    }
    level(true, bid, 0.02);
    level(false, bid + 0.05, 0.02);
    EXPECT_EQ(bbo(), std::make_pair(int(m->ticks(bid)), int(m->ticks(bid + 0.05))));
    bid += 10.0;                           // 1000 ticks a step, 50,000 in all
  }
}

}  // namespace
