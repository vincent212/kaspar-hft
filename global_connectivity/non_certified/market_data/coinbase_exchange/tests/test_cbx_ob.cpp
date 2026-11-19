/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * CoinbaseExchangeSync into a real OB, with no Mbp2Mbo: the L3 records go in
 * directly. The asset is registered as bnsim registers its crypto assets:
 * units 1 (prices arrive in ticks), maxpx far above any price, and a ring
 * ladder.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "coinbase_exchange/CoinbaseExchangeSync.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Fill.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;
using namespace coinbase_exchange;
using nlohmann::json;

namespace
{

struct TestableOB : public ob::act::OB
{
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;
};

constexpr uint64_t kDelayNs = 1000ULL * 1000; // OB's default order latency

const char *U[] = {"00000000-0000-4000-8000-000000000001", "00000000-0000-4000-8000-000000000002",
                   "00000000-0000-4000-8000-000000000003", "00000000-0000-4000-8000-000000000004",
                   "00000000-0000-4000-8000-000000000005", "00000000-0000-4000-8000-000000000006"};

class CbxOBTest : public ::testing::Test
{
protected:
  MockActor probe{"probe"};
  std::unique_ptr<TestableOB> ob;
  std::unique_ptr<CoinbaseExchangeSync> s;
  int sym = 0;
  int64_t seq = 1000;
  uint64_t now = 1795022845000000000ull;

  static void SetUpTestSuite()
  {
    const char *root = std::getenv("KSPRPROJ");
    std::string universe = root ? std::string(root) + "/unit_test/config/universe.csv" : "../config/universe.csv";
    ref::RefData::set_universe(universe, "");
  }

  void SetUp() override
  {
    ref::RefData::inst();
    auto a = ref::RefData::add_future_asset("BTC-USD_CBXTEST", en::x::COINBASEX, 4242, "", "BTC-USD", 1.0);
    ASSERT_NE(a, nullptr);
    a->maxpx = 1 << 30;
    sym = a->id;
    boost::property_tree::ptree pt;
    pt.put("ladder_ticks", 1 << 16);
    ob = std::make_unique<TestableOB>(nullptr, true, nullptr, sym, pt);
    std::string err;
    auto spec = ProductSpec::make("BTC-USD", 4242, char(en::x::COINBASEX), "0.01", "0.00000001", "0.000001", err);
    ASSERT_TRUE(spec) << err;
    s = std::make_unique<CoinbaseExchangeSync>(CoinbaseExchangeSync::Config{*spec}, [this](const bfile::l3_t &r) {
      mda::msg::Data d(true);
      d.l3 = r;
      dispatch(&d);
    });
    json snap{{"sequence", seq}, {"time", "2026-11-18T17:00:00.000000Z"}};
    snap["bids"] = json::array({json::array({"64000.00", "0.010000", U[0]}), json::array({"64000.00", "0.020000", U[1]}),
                                json::array({"63999.99", "0.5", U[2]})});
    snap["asks"] = json::array({json::array({"64000.01", "0.03", U[3]})});
    ASSERT_EQ(s->on_snapshot(snap, now), CoinbaseExchangeSync::SnapResult::Applied);
    probe.clear();
  }

  void dispatch(const actors::Message *msg)
  {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), msg, &probe);
    ob->reply_to = nullptr;
  }

  std::string ts()
  {
    now += 1000000; // 1 ms per message
    time_t t = time_t(now / 1000000000ull);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[64];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02d.%06uZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, unsigned((now / 1000) % 1000000));
    return b;
  }
  void open(const char *id, const char *side, const char *px, const char *sz)
  {
    s->on_message({{"type", "open"}, {"time", ts()}, {"product_id", "BTC-USD"}, {"sequence", ++seq},
                   {"order_id", id}, {"price", px}, {"remaining_size", sz}, {"side", side}},
                  now);
  }
  void match(const char *maker, const char *side, const char *px, const char *sz)
  {
    s->on_message({{"type", "match"}, {"trade_id", seq}, {"sequence", ++seq}, {"maker_order_id", maker},
                   {"taker_order_id", "99999999-0000-4000-8000-000000000009"}, {"time", ts()},
                   {"product_id", "BTC-USD"}, {"size", sz}, {"price", px}, {"side", side}},
                  now);
  }
  void done(const char *id, const char *side, const char *px)
  {
    s->on_message({{"type", "done"}, {"time", ts()}, {"product_id", "BTC-USD"}, {"sequence", ++seq},
                   {"price", px}, {"order_id", id}, {"reason", "canceled"}, {"side", side}, {"remaining_size", "0"}},
                  now);
  }
  void change(const char *id, const char *side, const char *px, const char *sz)
  {
    s->on_message({{"type", "change"}, {"reason", "modify_order"}, {"time", ts()}, {"sequence", ++seq},
                   {"order_id", id}, {"side", side}, {"product_id", "BTC-USD"}, {"new_size", sz}, {"new_price", px}},
                  now);
  }

  void ours_buy(int px_ticks, uint oid, int sz)
  {
    auto d = std::make_unique<mda::msg::Data>();
    d->israw = false;
    d->l3 = bfile::l3_sim_t();
    auto p = boost::intrusive_ptr<mda::msg::data_pay_load>(new mda::msg::data_pay_load());
    p->mkt = en::x::SIM;
    p->sym = sym;
    p->px = ref::Price(px_ticks, sym);
    p->sz = sz;
    p->disp_sz = sz;
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

  struct Level
  {
    long sz = -1, cnt = -1, sim = -1;
  };
  Level qat(bool bid, int64_t t)
  {
    probe.clear();
    cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    Level l;
    if (page)
      std::sscanf(page->val.c_str(), "QAT: %ld %ld %ld", &l.sz, &l.cnt, &l.sim);
    return l;
  }
  std::pair<long, long> bbo()
  {
    probe.clear();
    cons::msg::Get g("bbbo");
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    long b = -1, a = -1;
    if (page)
      std::sscanf(page->val.c_str(), "BBBO: %ld %ld", &b, &a);
    return {b, a};
  }
  void expect_ob_equals_sync()
  {
    for (int side = 0; side < 2; ++side)
      for (const auto &[px, l] : s->side(side == 0))
      {
        auto q = qat(side == 0, px);
        EXPECT_EQ(q.sz, l.disp) << (side == 0 ? "bid " : "ask ") << px;
        EXPECT_EQ(q.cnt, long(l.count)) << (side == 0 ? "bid " : "ask ") << px;
      }
    EXPECT_EQ(bbo(), std::make_pair(long(s->best(true)), long(s->best(false))));
  }
};

TEST_F(CbxOBTest, OBHoldsWhatTheSyncHolds)
{
  expect_ob_equals_sync();
  EXPECT_EQ(qat(true, 6400000).sz, 30000) << "0.01 + 0.02 BTC in 1e-6 units";
  EXPECT_EQ(qat(true, 6400000).cnt, 2);
  open(U[4], "sell", "64000.02", "0.25");
  match(U[3], "sell", "64000.01", "0.01");
  change(U[2], "buy", "63999.99", "0.4");
  done(U[1], "buy", "64000.00");
  expect_ob_equals_sync();
  match(U[3], "sell", "64000.01", "0.02"); // fills it
  expect_ob_equals_sync();
  EXPECT_EQ(bbo(), std::make_pair(6400000L, 6400002L));
  change(U[4], "sell", "64000.03", "0.25"); // price change
  expect_ob_equals_sync();
}

// Our bid of 1 unit joins the back of 0.03 BTC (30000 units) at 64000.00.
// Executions of the two orders ahead do not fill us; the next execution at
// the level, against an order that joined behind us, does.
TEST_F(CbxOBTest, OurOrderFillsOnlyAfterTheOrdersAheadOfItTrade)
{
  open(U[4], "sell", "64000.05", "1"); // live data first: OB's clock starts with it, not with recovery
  ours_buy(6400000, 1, 1);
  open(U[5], "buy", "64000.00", "0.001"); // behind us
  ASSERT_EQ(qat(true, 6400000).sim, 1);
  probe.clear();
  match(U[0], "buy", "64000.00", "0.01");
  match(U[1], "buy", "64000.00", "0.015");
  EXPECT_FALSE(probe.has_message_of_type<som::msg::Fill>()) << "0.005 BTC still ahead of us";
  match(U[1], "buy", "64000.00", "0.005");
  EXPECT_FALSE(probe.has_message_of_type<som::msg::Fill>()) << "the orders ahead are gone, nothing behind traded yet";
  match(U[5], "buy", "64000.00", "0.0005");
  EXPECT_TRUE(probe.has_message_of_type<som::msg::Fill>());
}

// A resync: the recovery records replace OB's book.
TEST_F(CbxOBTest, ResyncReplacesTheBook)
{
  s->on_disconnect(now);
  json snap{{"sequence", seq + 50}, {"time", ts()}};
  snap["bids"] = json::array({json::array({"63990.00", "1", U[5]})});
  snap["asks"] = json::array({json::array({"63990.05", "2", U[4]})});
  ASSERT_EQ(s->on_snapshot(snap, now), CoinbaseExchangeSync::SnapResult::Applied);
  EXPECT_LE(qat(true, 6400000).sz, 0) << "the old bids are gone";
  expect_ob_equals_sync();
  EXPECT_EQ(bbo(), std::make_pair(6399000L, 6399005L));
}

} // namespace
