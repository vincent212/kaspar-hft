/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitFillsClient on its own actor thread against FakeFills and
// SimFillSource. The subscriber is a recorder.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"
#include "bybit_post_trade/SimFillSource.hpp"
#include "bybit_post_trade/if/BybitFillsClient.hpp"
#include "FakeFillSource.hpp"

using namespace bybit_pt;
using namespace bybit_pt::test;
using namespace std::chrono_literals;
using post_trade::msg::TradeCapture;

namespace
{
  template <class P>
  bool wait_until(P p, std::chrono::milliseconds limit = 5s)
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (p())
        return true;
      std::this_thread::sleep_for(2ms);
    }
    return p();
  }

  class Sub : public actors::Actor
  {
  public:
    const char *get_name() const override { return "sub"; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu);
      if (auto t = dynamic_cast<const TradeCapture *>(m))
        got.push_back(*t);
      delete m;
    }
    size_t n()
    {
      std::lock_guard<std::mutex> l(mu);
      return got.size();
    }
    std::vector<TradeCapture> all()
    {
      std::lock_guard<std::mutex> l(mu);
      return got;
    }
    std::mutex mu;
    std::vector<TradeCapture> got;
  };

  std::string exec(const std::string &id, const std::string &qty = "0.002", const std::string &type = "Trade",
                   uint64_t ms = 0, const std::string &cat = "linear")
  {
    if (!ms)
      ms = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
    nlohmann::json j = {{"category", cat}, {"symbol", "BTCUSDT"}, {"execId", id}, {"orderId", "o-" + id},
                        {"orderLinkId", "k20261118-1"}, {"side", "Buy"}, {"execPrice", "91000.5"},
                        {"execQty", qty}, {"execType", type}, {"execTime", std::to_string(ms)}};
    return j.dump();
  }

  class FillsClientTest : public ::testing::Test
  {
  protected:
    FakeFills bybit;
    Sub sub;
    std::unique_ptr<actors::Manager> mgr;
    BybitFillsClient *c = nullptr;

    ClientConfig cfg()
    {
      ClientConfig k;
      k.source.credentials = {"key", "secret"};
      k.instruments[{"linear", "BTCUSDT"}] = {"linear", "BTCUSDT", 7702, 0.001};
      k.instruments[{"spot", "BTCUSDT"}] = {"spot", "BTCUSDT", 7701, 0.000001};
      k.reconnect_ms = 20;
      k.reconcile_every_s = 0;
      return k;
    }
    void start(ClientConfig k = {}, SourceFactory f = nullptr)
    {
      if (k.instruments.empty())
        k = cfg();
      c = new BybitFillsClient(k, f ? f : bybit.factory(), &sub);
      mgr = std::make_unique<actors::Manager>("t");
      mgr->manage_and_start(c);
    }
    void settle() { std::this_thread::sleep_for(80ms); }
    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete c;
    }
  };
}

TEST_F(FillsClientTest, BadConfigGivesNull)
{
  auto k = cfg();
  k.source.rest_host = "api.bybit.com"; // production with testnet = true
  EXPECT_EQ(create_BybitFillsClient(k, &sub, bybit.factory()), nullptr);
  k = cfg();
  k.source.ws_host = "stream.bybit.com";
  EXPECT_EQ(create_BybitFillsClient(k, &sub, bybit.factory()), nullptr);
  k = cfg();
  k.instruments.clear();
  EXPECT_EQ(create_BybitFillsClient(k, &sub, bybit.factory()), nullptr);
  k = cfg();
  k.source.credentials = {};
  EXPECT_EQ(create_BybitFillsClient(k, &sub, bybit.factory()), nullptr);
  EXPECT_EQ(create_BybitFillsClient("/nonexistent.ini", &sub), nullptr);
  auto a = create_BybitFillsClient(cfg(), &sub, bybit.factory());
  EXPECT_NE(a, nullptr);
  delete a;
}

TEST_F(FillsClientTest, LoginReconcilesEachCategoryFromStartOfDay)
{
  start();
  ASSERT_TRUE(wait_until([&] { return bybit.calls().size() == 2; }));
  auto calls = bybit.calls();
  const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
  for (auto &k : calls)
  {
    EXPECT_EQ(k.from, now / 86400000 * 86400000);
    EXPECT_LE(k.to, now);
  }
  EXPECT_NE(calls[0].category, calls[1].category);
}

TEST_F(FillsClientTest, SinceSplitsIntoSevenDayWindows)
{
  auto k = cfg();
  k.instruments.erase({"spot", "BTCUSDT"});
  const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
  k.since_ms = now - 10ull * 86400000;
  start(k);
  ASSERT_TRUE(wait_until([&] { return bybit.calls().size() == 2; }));
  auto calls = bybit.calls();
  EXPECT_EQ(calls[0].from, k.since_ms);
  EXPECT_EQ(calls[0].to, k.since_ms + 7ull * 86400000 - 1);
  EXPECT_EQ(calls[1].from, k.since_ms + 7ull * 86400000);
}

TEST_F(FillsClientTest, StreamExecutionIsCaptured)
{
  start();
  ASSERT_TRUE(wait_until([&] { return c->ready(); }));
  bybit.exec(exec("e1"));
  ASSERT_TRUE(wait_until([&] { return sub.n() == 1; }));
  auto t = sub.all()[0];
  EXPECT_EQ(t.trade_id, "e1");
  EXPECT_EQ(t.qty, 2);
  EXPECT_EQ(t.venue, en::x::BYBITF);
  EXPECT_EQ(t.account, "k20261118-1");
  EXPECT_FALSE(t.poss_dup);
}

TEST_F(FillsClientTest, StreamAndRestCopiesReachTheSubscriberOnce)
{
  start();
  ASSERT_TRUE(wait_until([&] { return c->ready(); }));
  bybit.exec(exec("e1"));
  bybit.exec(exec("e1"), "linear", true);
  bybit.exec(exec("e2"), "linear", true); // missed on the stream
  bybit.exec(exec("e2"));
  bybit.reconciled("linear", 2);
  settle();
  EXPECT_EQ(sub.n(), 2u);
  EXPECT_EQ(c->stats().duplicates, 2u);
  EXPECT_EQ(c->stats().from_rest, 2u);
  EXPECT_TRUE(sub.all()[1].poss_dup);
}

TEST_F(FillsClientTest, CashAndUnknownTypesAreNotCaptured)
{
  start();
  ASSERT_TRUE(wait_until([&] { return c->ready(); }));
  bybit.exec(exec("f1", "0.002", "Funding"));
  bybit.exec(exec("s1", "0.002", "Settle"));
  bybit.exec(exec("b1", "0.002", "BustTrade")); // liquidation: a position change
  bybit.exec(exec("a1", "0.002", "AdlTrade"));
  ASSERT_TRUE(wait_until([&] { return sub.n() == 2; }));
  settle();
  EXPECT_EQ(sub.n(), 2u);
  EXPECT_EQ(c->stats().not_trades, 2u);
}

TEST_F(FillsClientTest, UnmappedAndBadExecutionsAreCountedNotSent)
{
  start();
  ASSERT_TRUE(wait_until([&] { return c->ready(); }));
  auto j = nlohmann::json::parse(exec("u1"));
  j["symbol"] = "ETHUSDT";
  bybit.exec(j.dump());
  bybit.exec(exec("q1", "0.0005"));
  bybit.exec("not json");
  settle();
  EXPECT_EQ(sub.n(), 0u);
  EXPECT_EQ(c->stats().unmapped, 1u);
  EXPECT_EQ(c->stats().bad, 2u);
}

TEST_F(FillsClientTest, ReconnectReconcilesFromTheLastWindowLessTheMargin)
{
  start();
  ASSERT_TRUE(wait_until([&] { return bybit.calls().size() == 2; }));
  bybit.reconciled("linear", 0);
  bybit.reconciled("spot", 0);
  settle();
  const uint64_t first_to = bybit.calls()[0].to;
  bybit.disconnect();
  ASSERT_TRUE(wait_until([&] { return bybit.sources == 2 && bybit.calls().size() == 4; }));
  auto calls = bybit.calls();
  EXPECT_EQ(calls[2].from, first_to - 60000);
  EXPECT_EQ(calls[3].from, first_to - 60000);
  // The fill missed while disconnected, from REST, then the stream repeats it.
  bybit.exec(exec("m1"), "linear", true);
  bybit.exec(exec("m1"));
  settle();
  EXPECT_EQ(sub.n(), 1u);
  EXPECT_EQ(c->stats().logins, 2u);
  EXPECT_EQ(c->stats().disconnects, 1u);
}

TEST_F(FillsClientTest, FailedReconciliationIsRetriedFromTheSameStart)
{
  start();
  ASSERT_TRUE(wait_until([&] { return bybit.calls().size() == 2; }));
  const uint64_t from = bybit.calls()[0].from;
  bybit.reconciled("linear", 0, false);
  bybit.reconciled("spot", 0);
  settle();
  bybit.disconnect();
  ASSERT_TRUE(wait_until([&] { return bybit.calls().size() == 4; }));
  EXPECT_EQ(bybit.calls()[2].from, from);
  EXPECT_EQ(c->stats().reconcile_failures, 1u);
}

TEST_F(FillsClientTest, DayRollKeepsYesterdaysIds)
{
  start();
  ASSERT_TRUE(wait_until([&] { return c->ready(); }));
  const uint64_t d1 = 1795000000000ull; // 2026-11-18
  const uint64_t d0 = d1 - 86400000;
  bybit.exec(exec("y1", "0.002", "Trade", d0));
  bybit.exec(exec("t1", "0.002", "Trade", d1));
  bybit.exec(exec("y1", "0.002", "Trade", d0), "linear", true); // late copy of yesterday's
  bybit.exec(exec("old", "0.002", "Trade", d0 - 2 * 86400000), "linear", true);
  settle();
  EXPECT_EQ(sub.n(), 2u);
  EXPECT_EQ(c->stats().duplicates, 1u);
  EXPECT_EQ(c->stats().older_than_ledger, 1u);
  EXPECT_EQ(c->ledger().live(), 1u);
}

TEST_F(FillsClientTest, SimulatorFillsAreCapturedOnceAcrossReconciliations)
{
  auto k = cfg();
  k.sim = true;
  k.sim_config.fill_every_ms = 5;
  start(k, SimFillSource::make_factory(k.sim_config, k.instruments));
  ASSERT_TRUE(wait_until([&] { return sub.n() >= 20; }));
  for (int i = 0; i < 3; ++i)
    c->send(new msg::FillEvent(msg::FillEvent::RECONCILE, 0), nullptr);
  settle();
  mgr->end();
  mgr.reset();
  const auto &s = c->stats();
  EXPECT_EQ(s.emitted, sub.n());
  EXPECT_EQ(s.emitted, s.from_stream);
  EXPECT_GT(s.duplicates, 0u);
  EXPECT_EQ(s.executions, s.emitted + s.duplicates);
}

TEST(FillsConfig, ExampleFileReadsInSimMode)
{
  // The example names a key file that may not exist; sim skips it.
  const char *k = std::getenv("KSPRPROJ");
  std::string src = std::string(k ? k : "") +
                    "/global_connectivity/non_certified/post_trade/bybit/config/bybit_fills_example.ini";
  std::ifstream in(src);
  ASSERT_TRUE(in) << src;
  std::stringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  text.replace(text.find("sim = false"), 11, "sim = true");
  const std::string tmp = ::testing::TempDir() + "bybit_fills.ini";
  std::ofstream(tmp) << text;
  ClientConfig c;
  std::string err;
  ASSERT_TRUE(ClientConfig::from_file(tmp, c, err)) << err;
  EXPECT_EQ(c.instruments.size(), 2u);
  EXPECT_EQ(c.instruments.at({"linear", "BTCUSDT"}).asset, 1);
  EXPECT_DOUBLE_EQ(c.instruments.at({"spot", "BTCUSDT"}).qty_unit, 0.000001);
  EXPECT_EQ(c.source.rest_host, "api-testnet.bybit.com");
  EXPECT_EQ(c.reconcile_every_s, 300);
}
