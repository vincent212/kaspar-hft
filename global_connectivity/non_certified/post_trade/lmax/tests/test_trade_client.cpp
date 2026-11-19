/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LmaxTradeClient against FakeLmax (the order manager's test double) on its
// own actor thread, and end to end over QuickFIX against SimLmaxGateway with
// LmaxOM placing the orders.

#include <gtest/gtest.h>

#include <filesystem>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Order.hpp"

#include "FakeLmax.hpp"
#include "TestUtil.hpp"
#include "lmax_fix/LmaxFix.hpp"
#include "lmax_fix/SimLmaxGateway.hpp"
#include "lmax_om/act/LmaxOM.hpp"
#include "lmax_post_trade/act/LmaxTradeClient.hpp"

using namespace lmax;
using namespace lmax::pt;
using lmax::om::test::FakeLmax;
using lmax::om::test::Recorder;
using lmax::om::test::wait_until;
using post_trade::msg::TradeCapture;
using namespace std::chrono_literals;

namespace
{
  uint64_t fixed_now() { return fix::utc_timestamp_ns("20261118-15:00:00.000"); }
  uint64_t next_day() { return fix::utc_timestamp_ns("20261118-23:00:00.000"); }

  TradeClientConfig cfg()
  {
    TradeClientConfig c;
    c.map.venue = en::x::UNI;
    c.map.instruments["4001"] = {7001, 0.1};
    c.account = "ACC";
    return c;
  }

  om::TradeCaptureReport report(const std::string &order_id, const std::string &sec, const std::string &qty,
                                bool poss_dup = false)
  {
    om::TradeCaptureReport r;
    r.req_id = "T1";
    r.exec_id = "E" + sec;
    r.secondary_exec_id = sec;
    r.security_id = "4001";
    r.last_qty_text = qty;
    r.last_qty = std::stod(qty);
    r.last_px_text = "1.15229";
    r.last_px = 1.15229;
    r.trade_date = "20261118";
    r.transact_time = "20261118-10:00:00.000";
    r.sides.push_back({'1', order_id, "1", ""});
    r.poss_dup = poss_dup;
    return r;
  }

  class TradeClientTest : public ::testing::Test
  {
  protected:
    FakeLmax lmax;
    Recorder sub{"Positions"};
    std::unique_ptr<actors::Manager> mgr;
    LmaxTradeClient *client = nullptr;

    void start(om::SessionFactory f, uint64_t (*clock)() = fixed_now)
    {
      client = new LmaxTradeClient(cfg(), std::move(f), &sub);
      client->set_clock(clock);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(client);
    }
    void TearDown() override
    {
      if (mgr)
        mgr->end();
      delete client;
    }
    void settle() { std::this_thread::sleep_for(100ms); }
  };
}

TEST_F(TradeClientTest, RequestsTheDaysTradesOnLogon)
{
  start(lmax.factory());
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 1; }));
  auto q = lmax.tcr_requests()[0];
  EXPECT_EQ(q.from, "20261117-22:00:00.000"); // 17:00 New York on the 17th
  EXPECT_TRUE(q.to.empty());
  EXPECT_TRUE(q.subscribe);
  EXPECT_EQ(q.account, "ACC");
  EXPECT_LE(q.req_id.size(), 16u);
}

TEST_F(TradeClientTest, ReportsReachTheSubscriberOnce)
{
  start(lmax.factory());
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 1; }));
  lmax.tcr_ack({"T1", "", 0, 0, -1, '1'});
  lmax.tcr(report("O1", "M1", "0.4"));
  lmax.tcr(report("O1", "M2", "0.6"));
  lmax.tcr(report("O1", "M1", "0.4", true)); // resent
  // The order session's report of the same fill.
  om::ExecReport e;
  e.exec_type = 'F';
  e.exec_id = "EX-OTHER";
  e.secondary_exec_id = "M2";
  e.order_id = "O1";
  e.side = '1';
  e.security_id = "4001";
  e.has_last = true;
  e.last_qty = 0.6;
  e.last_px = 1.15229;
  e.transact_time = "20261118-10:00:00.000";
  client->send(new om::msg::ExecCopy(e), nullptr);
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 2; }));
  settle();
  auto t = sub.all<TradeCapture>();
  ASSERT_EQ(t.size(), 2u);
  EXPECT_EQ(t[0]->qty, 4);
  EXPECT_EQ(t[1]->qty, 6);
  EXPECT_EQ(client->stats().duplicates, 2u);
  EXPECT_EQ(client->ledger().net_qty("4001"), 10);
}

TEST_F(TradeClientTest, ReconnectReplayAddsNothing)
{
  start(lmax.factory());
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 1; }));
  lmax.tcr(report("O1", "M1", "0.4"));
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 1; }));
  lmax.logout();
  lmax.logon();
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 2; }));
  EXPECT_NE(lmax.tcr_requests()[1].req_id, lmax.tcr_requests()[0].req_id);
  lmax.tcr(report("O1", "M1", "0.4")); // the day's replay
  lmax.tcr(report("O2", "M3", "0.1")); // missed while disconnected
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 2; }));
  settle();
  EXPECT_EQ(sub.all<TradeCapture>().size(), 2u);
}

TEST_F(TradeClientTest, CancelOfATradeIsPassedOnAsARemoval)
{
  start(lmax.factory());
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 1; }));
  lmax.tcr(report("O1", "M1", "0.4"));
  om::ExecReport h;
  h.exec_type = 'H';
  h.exec_id = "EH1";
  h.secondary_exec_id = "M1";
  h.order_id = "O1";
  h.side = '1';
  h.security_id = "4001";
  h.transact_time = "20261118-11:00:00.000";
  client->send(new om::msg::ExecCopy(h), nullptr);
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 2; }));
  auto t = sub.all<TradeCapture>();
  EXPECT_EQ(t[1]->event, post_trade::Event::Cancel);
  EXPECT_EQ(t[1]->qty, 4);
  EXPECT_EQ(client->ledger().net_qty("4001"), 0);
}

TEST_F(TradeClientTest, RejectedRequestAndUnmappableReportsAreCounted)
{
  start(lmax.factory());
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 1; }));
  lmax.tcr_ack({"T1", "not permitted", 99, 2, -1, '1'});
  auto r = report("O1", "M1", "0.4");
  r.security_id = "9999";
  lmax.tcr(r);
  lmax.unparsed("TradeCaptureReport without required tag 32");
  settle();
  EXPECT_EQ(client->stats().request_rejected, 1u);
  EXPECT_EQ(client->stats().unmapped, 2u);
  EXPECT_TRUE(sub.all<TradeCapture>().empty());
}

TEST_F(TradeClientTest, NewTradingDayClearsTheLedger)
{
  start(lmax.factory());
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 1; }));
  lmax.tcr(report("O1", "M1", "0.4"));
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 1; }));
  client->set_clock(next_day); // a logon after 17:00 New York
  lmax.logout();
  lmax.logon();
  ASSERT_TRUE(wait_until([&] { return lmax.tcr_requests().size() == 2; }));
  EXPECT_EQ(lmax.tcr_requests()[1].from, "20261118-22:00:00.000");
  settle();
  EXPECT_EQ(client->ledger().live(), 0u);
}

TEST_F(TradeClientTest, ExecCopyOnlyWithoutASession)
{
  start(nullptr);
  om::ExecReport e;
  e.exec_type = 'F';
  e.exec_id = "E1";
  e.secondary_exec_id = "M9";
  e.order_id = "O9";
  e.side = '2';
  e.security_id = "4001";
  e.has_last = true;
  e.last_qty = 1.0;
  e.last_px = 1.1;
  e.transact_time = "20261118-10:00:00.000";
  client->send(new om::msg::ExecCopy(e), nullptr);
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 1; }));
  EXPECT_EQ(sub.all<TradeCapture>()[0]->qty, 10);
  EXPECT_EQ(lmax.sessions(), 0);
}

// ---- end to end over QuickFIX ---------------------------------------------------------------

TEST(TradeClientLoopback, OrderManagerFillReachesTheLedgerOnceFromBothSources)
{
  namespace fs = std::filesystem;
  fix::SimLmaxGateway::Config g;
  g.md_dictionary = LMAX_MD_DICT;
  g.trading_dictionary = LMAX_TRADING_DICT;
  g.instruments = {{"4001", 1.15230, 0.00001, 0.1}};
  fix::SimLmaxGateway gw(g);
  gw.start();
  gw.set_market("4001", 1.15220, 1.15230);
  auto eurusd = lmax::om::test::fx_asset("EURUSD_P", 0.00001);

  auto params = [&] {
    fix::SessionParams p;
    p.env = fix::Env::Loopback;
    p.host = "127.0.0.1";
    p.port = gw.port();
    p.tls = false;
    p.sender = "CLIENTTR";
    p.target = "LMXBD";
    p.dictionary = LMAX_TRADING_DICT;
    p.reset_on_logon = true;
    p.reconnect_s = 1;
    p.start_time = p.end_time = "00:00:00";
    return p;
  };
  // The post-trade client's own session (the gateway serves one trading
  // session, so here it is also the order session's: the OM sends copies).
  Recorder sub{"Positions"}, somr{"SOM"};
  om::QfSessionConfig qs{params(), "user", "pass"};
  TradeClientConfig tc;
  tc.map.venue = en::x::UNI;
  tc.map.instruments["4001"] = {int(eurusd->id), 0.1};
  auto *client = new LmaxTradeClient(tc, {}, &sub);
  om::LmaxOMConfig oc;
  oc.session = qs;
  oc.instruments[eurusd->id] = om::OmInstrument{"4001", "EUR/USD", 0.00001, 0.1};
  auto *om_ = new om::LmaxOM(oc, &somr, om::qf_session_factory(qs), en::x::UNI,
                             frame::som::ClientIdGen::from_clock(), client);
  actors::Manager mgr("test");
  mgr.manage_and_start(client);
  mgr.manage_and_start(om_);
  ASSERT_TRUE(wait_until([&] { return om_->ready(); }, 10s));
  frame::som::msg::Order o("FX", en::x::UNI, eurusd->id, 3, 115230, en::bs::BUY, 1);
  o.oid = 1;
  om_->send(new frame::som::msg::Order(o), &somr);
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 1; }, 10s));
  auto t = sub.all<TradeCapture>()[0];
  EXPECT_EQ(t->qty, 3);
  EXPECT_EQ(t->side, post_trade::Side::Buy);
  EXPECT_DOUBLE_EQ(t->price(), 1.1523); // the simulator writes 31=1.1523
  EXPECT_EQ(t->asset, int(eurusd->id));
  mgr.end();
  delete om_;
  delete client;
  gw.stop();
}

TEST(TradeClientLoopback, OwnSessionGetsTheDaysTrades)
{
  fix::SimLmaxGateway::Config g;
  g.md_dictionary = LMAX_MD_DICT;
  g.trading_dictionary = LMAX_TRADING_DICT;
  g.instruments = {{"4001", 1.15230, 0.00001, 0.1}};
  fix::SimLmaxGateway gw(g);
  gw.start();
  gw.set_market("4001", 1.15220, 1.15230);
  auto eurusd = lmax::om::test::fx_asset("EURUSD_P2", 0.00001);
  fix::SessionParams p;
  p.env = fix::Env::Loopback;
  p.host = "127.0.0.1";
  p.port = gw.port();
  p.tls = false;
  p.sender = "CLIENTTR";
  p.target = "LMXBD";
  p.dictionary = LMAX_TRADING_DICT;
  p.reset_on_logon = true;
  p.reconnect_s = 1;
  p.start_time = p.end_time = "00:00:00";
  om::QfSessionConfig qs{p, "user", "pass"};

  // First the OM trades (session 1), then the post-trade client logs on
  // (session 2) and asks for the day: the trade arrives as an AE.
  Recorder sub{"Positions"}, somr{"SOM"};
  om::LmaxOMConfig oc;
  oc.session = qs;
  oc.instruments[eurusd->id] = om::OmInstrument{"4001", "EUR/USD", 0.00001, 0.1};
  {
    auto *om_ = new om::LmaxOM(oc, &somr, om::qf_session_factory(qs), en::x::UNI);
    actors::Manager m1("om");
    m1.manage_and_start(om_);
    ASSERT_TRUE(wait_until([&] { return om_->ready(); }, 10s));
    frame::som::msg::Order o("FX", en::x::UNI, eurusd->id, 2, 115240, en::bs::BUY, 1);
    o.oid = 1;
    om_->send(new frame::som::msg::Order(o), &somr);
    ASSERT_TRUE(wait_until([&] { return somr.all<frame::som::msg::Fill>().size() == 1; }, 10s));
    m1.end();
    delete om_;
  }
  TradeClientConfig tc;
  tc.map.venue = en::x::UNI;
  tc.map.instruments["4001"] = {int(eurusd->id), 0.1};
  auto *client = new LmaxTradeClient(tc, om::qf_session_factory(qs), &sub);
  actors::Manager m2("pt");
  m2.manage_and_start(client);
  ASSERT_TRUE(wait_until([&] { return sub.all<TradeCapture>().size() == 1; }, 10s));
  EXPECT_EQ(sub.all<TradeCapture>()[0]->qty, 2);
  EXPECT_EQ(gw.counts().tcr_requests.load(), 1);
  auto ad = gw.received("AD");
  ASSERT_EQ(ad.size(), 1u);
  EXPECT_NE(ad[0].find("|263=1|"), std::string::npos) << ad[0];
  EXPECT_NE(ad[0].find("|569=1|"), std::string::npos) << ad[0];
  m2.end();
  delete client;
  gw.stop();
}
