/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * CbxDropClient: the mapping of Coinbase drop-copy ExecutionReports
 * (exchange/fix-api/drop-copy, retrieved 2026-11-18; the page has no wire
 * examples, so the reports are built from its field table) to TradeCapture,
 * and the client as an actor against the fake session and SimDropCopy.
 */

#include <gtest/gtest.h>

#include <fstream>

#include "chutil/Assert.hpp"
#include "actors/act/Manager.hpp"

#include "FakeCbx.hpp"
#include "TestUtil.hpp"
#include "coinbase_exchange_pt/SimDropCopy.hpp"
#include "coinbase_exchange_pt/act/CbxDropClient.hpp"

using namespace coinbase_exchange;
using namespace coinbase_exchange::fix;
using namespace coinbase_exchange::pt;
using coinbase_exchange::oe::test::FakeCbxServer;
using coinbase_exchange::oe::test::Recorder;
using coinbase_exchange::oe::test::wait_until;
namespace ptm = post_trade::msg;

namespace
{
  DropConfig cfg()
  {
    DropConfig c;
    c.session.environment = Environment::Sandbox;
    c.products["BTC-USD"] = {101, 8};
    c.products["ETH-USD"] = {102, 8};
    c.reconnect_ms = 50;
    return c;
  }

  FixMsg trade(const std::string &exec_id, const std::string &trade_id = "1135493769", char side = '1',
               const std::string &qty = "0.00250000", const std::string &px = "64000.01")
  {
    FixMsg r("8");
    r.set(ClOrdID, "1985ca2d-61ef-49f1-bfce-6c39d8462914").set(OrderID, "7b52009b-64fd-4a2a-89e6-d8a939753077");
    r.set(ExecID, exec_id).set(OrdStatus, '1').set(ExecType, 'F').set(Symbol, "BTC-USD").set(Side, side);
    r.set(OrdType, '2').set(LastQty, qty).set(LastPx, px).set(CumQty, qty).set(LeavesQty, "0.0075");
    r.set(TransactTime, "20261118-17:25:47.637");
    if (!trade_id.empty())
      r.set(TradeID, trade_id);
    r.set(AggressorIndicator, 'N');
    r.add(NoMiscFees, "1").add(MiscFeeAmt, "0.0004").add(MiscFeeCurr, "USD").add(MiscFeeType, "4").add(MiscFeeBasis, "2");
    return r;
  }
}

// ---- mapping -------------------------------------------------------------------------------------

TEST(CbxDropMapping, TradeReport)
{
  std::string why;
  auto t = to_trade_capture(trade("e1"), cfg(), why);
  ASSERT_TRUE(t) << why;
  EXPECT_EQ(t->venue, en::x::COINBASEX);
  EXPECT_EQ(t->trade_id, "BTC-USD/1135493769/7b52009b-64fd-4a2a-89e6-d8a939753077");
  EXPECT_EQ(t->report_id, "e1");
  EXPECT_EQ(t->event, post_trade::Event::New);
  EXPECT_EQ(t->venue_order_id, "7b52009b-64fd-4a2a-89e6-d8a939753077");
  EXPECT_EQ(t->symbol, "BTC-USD");
  EXPECT_EQ(t->asset, 101);
  EXPECT_EQ(t->side, post_trade::Side::Buy);
  EXPECT_EQ(t->qty, 250000) << "base increments (1e-8 BTC)";
  EXPECT_EQ(t->price_mantissa, 6400001);
  EXPECT_EQ(t->price_decimals, 2);
  EXPECT_DOUBLE_EQ(t->price(), 64000.01);
  EXPECT_EQ(t->trade_date, 20261118u);
  EXPECT_EQ(t->trade_time_ns, int64_t((17 * 3600 + 25 * 60 + 47) * 1000000000ll + 637000000ll));
  EXPECT_FALSE(t->poss_dup);
  EXPECT_NE(t->raw.find("1003=1135493769"), std::string::npos);
}

TEST(CbxDropMapping, OrderEventsAreNotTrades)
{
  std::string why;
  for (char et : {'0', '4', '5', '8', 'C', 'D', 'I'})
  {
    auto r = trade("e1");
    r.set(ExecType, et);
    EXPECT_FALSE(to_trade_capture(r, cfg(), why)) << et;
    EXPECT_EQ(why.rfind("order event", 0), 0u);
  }
}

TEST(CbxDropMapping, WithoutTradeIdTheExecIdIsTheTrade)
{
  std::string why;
  auto t = to_trade_capture(trade("e9", ""), cfg(), why);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->trade_id, "e9");
}

TEST(CbxDropMapping, TradeCancelIsABust_TradeCorrectIsAnAmend)
{
  std::string why;
  auto b = trade("e2");
  b.set(ExecType, 'H');
  auto t = to_trade_capture(b, cfg(), why);
  ASSERT_TRUE(t) << why;
  EXPECT_EQ(t->event, post_trade::Event::Bust);
  EXPECT_EQ(t->ref_trade_id, t->trade_id);
  auto g = trade("e3", "1135493769", '1', "0.002", "64000.00");
  g.set(ExecType, 'G');
  t = to_trade_capture(g, cfg(), why);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->event, post_trade::Event::Amend);
  EXPECT_EQ(t->qty, 200000);
}

TEST(CbxDropMapping, MalformedIsRefused)
{
  std::string why;
  auto r = trade("e1");
  r.set(LastPx, "abc");
  EXPECT_FALSE(to_trade_capture(r, cfg(), why));
  auto u = trade("");
  EXPECT_FALSE(to_trade_capture(u, cfg(), why));
}

// ---- the actor ---------------------------------------------------------------------------------------

class CbxDropTest : public ::testing::Test
{
protected:
  FakeCbxServer cb;
  Recorder sub{"positions"};
  std::unique_ptr<actors::Manager> mgr;
  CbxDropClient *c = nullptr;

  void start(DropConfig d = cfg(), bool wait = true)
  {
    c = new CbxDropClient(d, cb.factory(), &sub);
    mgr = std::make_unique<actors::Manager>("test");
    mgr->manage_and_start(c);
    if (wait)
      ASSERT_TRUE(wait_until([&] { return c->logged_on(); }));
  }
  void TearDown() override
  {
    if (mgr)
      mgr->end();
    mgr.reset();
    delete c;
  }
};

TEST_F(CbxDropTest, LogsOnAsADropCopySession)
{
  start();
  auto s = cb.sessions();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_TRUE(s[0].cfg.drop_copy) << "DropCopyFlag 9406=Y";
  EXPECT_EQ(s[0].cfg.cancel_on_disconnect, 0) << "no 8013 on a drop-copy Logon";
}

TEST_F(CbxDropTest, TradesReachTheSubscriberOnce)
{
  start();
  cb.deliver(trade("e1", "100"));
  cb.deliver(trade("e2", "101", '2', "0.001"));
  cb.deliver(trade("e1", "100")); // the same report again
  auto resend = trade("e2", "101", '2', "0.001");
  resend.set(PossDupFlag, 'Y');
  cb.deliver(resend);
  auto n = trade("e0", "100");
  n.set(ExecType, '0');
  cb.deliver(n); // an order event
  ASSERT_NE(sub.wait_for<ptm::TradeCapture>(1), nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(sub.all<ptm::TradeCapture>().size(), 2u);
  ASSERT_TRUE(wait_until([&] { return c->stats().reports == 5; }));
  EXPECT_EQ(c->stats().duplicates, 2u);
  EXPECT_EQ(c->stats().order_events, 1u);
  EXPECT_EQ(c->ledger().net_qty("BTC-USD"), 250000 - 100000);
}

TEST_F(CbxDropTest, BustRemovesTheTrade)
{
  start();
  cb.deliver(trade("e1", "100"));
  auto b = trade("e5", "100");
  b.set(ExecType, 'H');
  cb.deliver(b);
  ASSERT_NE(sub.wait_for<ptm::TradeCapture>(1), nullptr);
  EXPECT_EQ(sub.all<ptm::TradeCapture>()[1]->event, post_trade::Event::Bust);
  EXPECT_EQ(c->ledger().net_qty("BTC-USD"), 0);
}

TEST_F(CbxDropTest, ReconnectDoesNotDuplicate)
{
  start();
  cb.deliver(trade("e1", "100"));
  ASSERT_NE(sub.wait_for<ptm::TradeCapture>(), nullptr);
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 2 && c->logged_on(); }));
  auto again = trade("e1", "100");
  again.set(PossDupFlag, 'Y');
  cb.deliver(again);
  cb.deliver(trade("e7", "102"));
  ASSERT_NE(sub.wait_for<ptm::TradeCapture>(1), nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(sub.all<ptm::TradeCapture>().size(), 2u);
}

TEST_F(CbxDropTest, LogonRejectedStops)
{
  cb.reject_logon = true;
  start(cfg(), false);
  ASSERT_TRUE(wait_until([&] { return c->stopped(); }));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(cb.sessions().size(), 1u);
}

TEST(CbxDropSim, SimulatorTradesWithResends)
{
  SimDropCopy::Config sc;
  sc.max_trades = 9;
  sc.resend_every = 3;
  sc.trade_ms = 5;
  SimDropCopy sim(sc);
  Recorder sub{"positions"};
  auto c = new CbxDropClient(cfg(), sim.factory(), &sub);
  auto mgr = std::make_unique<actors::Manager>("test");
  mgr->manage_and_start(c);
  ASSERT_TRUE(wait_until([&] { return c->stats().reports == 9 * 2 + 3; }));
  EXPECT_EQ(sub.all<ptm::TradeCapture>().size(), 9u);
  EXPECT_EQ(c->stats().duplicates, 3u);
  EXPECT_EQ(c->stats().order_events, 9u);
  mgr->end();
  delete c;
}

TEST(CbxDropConfig, FileIsRead)
{
  const char *k = std::getenv("KSPRPROJ");
  std::string path =
      std::string(k ? k : "") + "/global_connectivity/non_certified/post_trade/coinbase_exchange/config/cbx_drop_example.ini";
  DropConfig d;
  std::string err;
  ASSERT_TRUE(DropConfig::from_file(path, d, err)) << err;
  EXPECT_EQ(d.session.environment, Environment::Sandbox);
  EXPECT_TRUE(d.session.drop_copy);
  EXPECT_EQ(d.products.at("BTC-USD").size_decimals, 8);
  EXPECT_EQ(create_CbxDropClient(DropConfig{}, nullptr, FakeCbxServer().factory()), nullptr) << "no products";
}

// The only wire-format ExecutionReport in Coinbase's documentation (the
// changelog, 2025-SEP-3, an RFQ fill). Its BodyLength (440) and CheckSum (204)
// do not match its content (344 and 244), as placeholders replaced the real
// values; with them it is refused, without them it maps.
TEST(CbxDropMapping, TheChangelogsSampleExecutionReport)
{
  const std::string sample =
      "8=FIXT.1.1|9=440|35=8|49=Coinbase|56=TARGET_COMP_ID|34=15|50=TEST|52=20250408-03:34:32.789012|369=8|6=70000|"
      "11=CLIENT_ORDER_ID|14=0.01428|17=EXEC_ID|37=ORDER_ID|39=2|55=BTC-USD|54=2|40=2|32=0.01428|31=70000|44=70000|"
      "38=0.01428|60=20250408-03:34:32.784254|152=999.60015994|150=F|8152=0|59=4|126=20250408-03:34:39.467|136=1|"
      "137=0.249900039985|138=USD|139=4|891=0|10=204";
  EXPECT_FALSE(FixMsg::parse(sample, '|')) << "BodyLength and CheckSum are wrong in the sample";
  std::string body = sample.substr(sample.find("35=8"));
  body = body.substr(0, body.find("|10="));
  auto m = FixMsg::parse(body, '|');
  ASSERT_TRUE(m);
  std::string why;
  auto t = to_trade_capture(*m, cfg(), why);
  ASSERT_TRUE(t) << why;
  EXPECT_EQ(t->trade_id, "EXEC_ID") << "no TradeID in the sample";
  EXPECT_EQ(t->side, post_trade::Side::Sell);
  EXPECT_EQ(t->qty, 1428000);
  EXPECT_EQ(t->price_mantissa, 70000);
  EXPECT_EQ(t->price_decimals, 0);
  EXPECT_EQ(t->trade_date, 20250408u);
  EXPECT_EQ(t->trade_time_ns, int64_t((3 * 3600 + 34 * 60 + 32) * 1000000000ll + 784254000ll));
}
