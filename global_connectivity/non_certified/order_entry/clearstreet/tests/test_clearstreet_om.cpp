/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// ClearStreetOM against FakeClearStreet, on its own actor thread under a
// kaspar Manager as in production. "SOM" is a recorder: orders come from it,
// replies go back to it. The test thread plays QuickFIX's thread. The list
// follows order_entry.md, Tests, plus the Clear Street specifics in feed.md.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeClearStreet.hpp"
#include "TestUtil.hpp"
#include "clearstreet_om/act/ClearStreetOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using OT = Order::OrdType;
using TIF = Order::TimeInForce;
using namespace clearstreet::om;
using namespace clearstreet::om::test;
using frame::som::State;

namespace
{
  class ClearStreetOMTest : public ::testing::Test
  {
  protected:
    FakeClearStreet cs;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    ClearStreetOM *om = nullptr;
    const frame::ref::Asset *aapl = nullptr;

    void SetUp() override { aapl = equity("AAPL_CS"); }
    void TearDown() override { stop(); }

    ClearStreetOMConfig cfg()
    {
      ClearStreetOMConfig c;
      c.account = "SIM1";
      c.instruments[aapl->id] = CsInstrument{"AAPL", 0.01};
      return c;
    }
    void start(ClearStreetOMConfig c, frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000),
               bool wait_ready = true, std::string date = "20261118")
    {
      om = new ClearStreetOM(c, &somr, cs.factory(), en::x::CLEARSTREET, ids, date);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      if (wait_ready)
        ASSERT_TRUE(wait_until([&] { return om->ready(); }));
    }
    void start() { start(cfg()); }
    void stop()
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      om = nullptr;
    }

    Order order(int oid, int qty = 100, int px = 23025, en::bs side = en::bs::BUY)
    {
      Order o("CS", en::x::CLEARSTREET, aapl->id, qty, px, side, 4);
      o.oid = oid;
      return o;
    }
    // Send as the SOM does; wait until it reaches Clear Street; return its ClOrdID.
    std::string place(const Order &o)
    {
      auto before = cs.news().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return cs.news().size() > before; })) << "order never reached Clear Street";
      auto n = cs.news();
      return n.empty() ? std::string() : n.back().clordid;
    }
    NewOrderReq sent(const Order &o)
    {
      place(o);
      return cs.news().back();
    }
    const som::Reject *refused(const Order &o, size_t nth)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }
    void cancel(int oid) { om->send(new som::Cancel(oid), &somr); }
    CancelReq cancel_sent(int oid)
    {
      auto before = cs.cancels().size();
      cancel(oid);
      EXPECT_TRUE(wait_until([&] { return cs.cancels().size() > before; }));
      auto c = cs.cancels();
      return c.empty() ? CancelReq{} : c.back();
    }
    void settle() { std::this_thread::sleep_for(100ms); }
    std::vector<std::string> replies()
    {
      settle();
      return somr.replies();
    }
  };
}

// ---- logon, registration, configuration --------------------------------------------------

TEST_F(ClearStreetOMTest, LogsOnAndRegistersWithTheSom)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::CLEARSTREET);
  EXPECT_EQ(reg->manager, om);
  EXPECT_EQ(cs.sessions(), 1);
  EXPECT_EQ(om->counters().account_not_listed, 0u);
}

TEST_F(ClearStreetOMTest, AccountMissingFromTheLogonListIsReported)
{
  cs.logon_text = "ACC7,ACC8";
  start();
  settle();
  EXPECT_EQ(om->counters().account_not_listed, 1u);
}

TEST_F(ClearStreetOMTest, BadConfigGivesNullNotCrash)
{
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "cs_om_cfg_test";
  fs::create_directories(dir);
  std::ofstream(dir / "inst.csv") << "1001,AAPL,0.01\n";
  std::ofstream(dir / "key") << "secret\n";
  auto write = [&](const std::string &body) { std::ofstream(dir / "om.ini") << body; };
  const std::string inst = "instruments = " + (dir / "inst.csv").string() + "\n";

  EXPECT_EQ(create_ClearStreetOM((dir / "missing.ini").string(), &somr), nullptr);
  write("environment = sim\n" + inst); // no account
  EXPECT_EQ(create_ClearStreetOM((dir / "om.ini").string(), &somr), nullptr);
  write("environment = live\naccount = A\n" + inst);
  EXPECT_EQ(create_ClearStreetOM((dir / "om.ini").string(), &somr), nullptr);
  write("environment = production\naccount = A\n" + inst + "host = h\nport = 1\nsender_comp_id = S\n"
        "target_comp_id = T\nstore_path = /tmp/x\ndictionary = d\nkey_file = " + (dir / "key").string() + "\n");
  EXPECT_EQ(create_ClearStreetOM((dir / "om.ini").string(), &somr), nullptr) << "production without production_ok";
  write("environment = uat\naccount = A\n" + inst + "host = h\nport = 1\nsender_comp_id = S\n"
        "target_comp_id = T\nstore_path = /tmp/x\ndictionary = d\nkey_file = /nonexistent\n");
  EXPECT_EQ(create_ClearStreetOM((dir / "om.ini").string(), &somr), nullptr) << "no key file";
  write("environment = sim\naccount = A\ninstruments = /nonexistent\n");
  EXPECT_EQ(create_ClearStreetOM((dir / "om.ini").string(), &somr), nullptr);
  write("venue = NOPE\nenvironment = sim\naccount = A\n" + inst);
  EXPECT_EQ(create_ClearStreetOM((dir / "om.ini").string(), &somr), nullptr);

  write("environment = sim\naccount = A\n" + inst);
  auto a = create_ClearStreetOM((dir / "om.ini").string(), &somr);
  EXPECT_NE(a, nullptr);
  delete a;
  ClearStreetOMConfig c;
  en::x v;
  std::string err;
  write("environment = uat\naccount = A\n" + inst + "host = h\nport = 9\nsender_comp_id = S\n"
        "target_comp_id = T\nstore_path = /tmp/x\ndictionary = d\nqf.SSLProtocol = +TLSv1.2\nkey_file = " +
        (dir / "key").string() + "\n");
  ASSERT_TRUE(ClearStreetOMConfig::from_file((dir / "om.ini").string(), c, v, err)) << err;
  EXPECT_EQ(c.session.api_key, "secret");
  EXPECT_EQ(c.session.extra["SSLProtocol"], "+TLSv1.2");
  EXPECT_EQ(v, en::x::CLEARSTREET);
}

TEST_F(ClearStreetOMTest, OrdersBeforeLogonAndWhileDisconnectedAreRejectedLocally)
{
  cs.auto_logon = false;
  start(cfg(), frame::som::ClientIdGen(5000), false);
  auto r = refused(order(1), 0);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  cs.logon();
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  cs.logout("bye");
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  r = refused(order(2), 1);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  // logged on, but the session refuses synchronously
  cs.logon();
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  cs.connected = false;
  r = refused(order(3), 2);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
  EXPECT_TRUE(cs.news().empty());
}

// ---- fields as sent ------------------------------------------------------------------------

TEST_F(ClearStreetOMTest, LimitDayBuyFields)
{
  start();
  auto q = sent(order(1, 25, 23025));
  EXPECT_EQ(q.account, "SIM1");
  EXPECT_EQ(q.clordid, "20261118-" + std::to_string((5000ULL << 36) | 1));
  EXPECT_EQ(q.symbol, "AAPL");
  EXPECT_EQ(q.side, '1');
  EXPECT_EQ(q.open_close, 0);
  EXPECT_EQ(q.qty, "25");
  EXPECT_EQ(q.ord_type, '2');
  EXPECT_EQ(q.px, "230.25");
  EXPECT_EQ(q.stop_px, "");
  EXPECT_EQ(q.tif, '0');
  EXPECT_EQ(q.target_strategy, 0);
  EXPECT_FALSE(q.extended_hours);
  EXPECT_EQ(q.transact_time.size(), 21u);
}

TEST_F(ClearStreetOMTest, OrderTypesTimeInForceAndExtendedHours)
{
  start();
  auto o = order(1);
  o.ord_type = OT::MARKET;
  auto q = sent(o);
  EXPECT_EQ(q.ord_type, '1');
  EXPECT_EQ(q.px, "");

  o = order(2);
  o.ord_type = OT::STOP;
  o.stop_px = 21600;
  q = sent(o);
  EXPECT_EQ(q.ord_type, '3');
  EXPECT_EQ(q.px, "");
  EXPECT_EQ(q.stop_px, "216.00");

  o = order(3, 100, 21500, en::bs::SEL);
  o.ord_type = OT::STOP_LIMIT;
  o.stop_px = 21600;
  o.tif = TIF::GTC;
  q = sent(o);
  EXPECT_EQ(q.ord_type, '4');
  EXPECT_EQ(q.px, "215.00");
  EXPECT_EQ(q.stop_px, "216.00");
  EXPECT_EQ(q.tif, '1');
  EXPECT_EQ(q.side, '2');

  const std::pair<TIF, char> tifs[] = {{TIF::AT_OPEN, '2'}, {TIF::IOC, '3'}, {TIF::FOK, '4'}, {TIF::AT_CLOSE, '7'}};
  int oid = 10;
  for (auto [t, c] : tifs)
  {
    o = order(++oid);
    o.tif = t;
    EXPECT_EQ(sent(o).tif, c);
  }

  o = order(20);
  o.tif = TIF::EXTENDED_DAY;
  q = sent(o);
  EXPECT_EQ(q.tif, '0');
  EXPECT_TRUE(q.extended_hours);
}

TEST_F(ClearStreetOMTest, StrategiesAndRoutes)
{
  start();
  auto o = order(1, 1000);
  o.ord_type = OT::MARKET;
  o.strategy = "TWAP";
  auto q = sent(o);
  EXPECT_EQ(q.target_strategy, 1002);
  EXPECT_EQ(q.ex_destination, "");

  o = order(2);
  o.strategy = "vwap";
  EXPECT_EQ(sent(o).target_strategy, 1001);

  o = order(3);
  o.strategy = "DMA";
  o.route = "XNAS";
  q = sent(o);
  EXPECT_EQ(q.target_strategy, 1003);
  EXPECT_EQ(q.ex_destination, "XNAS");

  o = order(4); // a route alone is DMA to that exchange
  o.route = "IEXG";
  q = sent(o);
  EXPECT_EQ(q.target_strategy, 1003);
  EXPECT_EQ(q.ex_destination, "IEXG");
}

TEST_F(ClearStreetOMTest, ShortSales)
{
  start();
  auto o = order(1, 100, 23025, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  auto q = sent(o);
  EXPECT_EQ(q.side, '2'); // never 5: Clear Street rejects it
  EXPECT_EQ(q.open_close, 'O');

  o = order(2, 100, 23025, en::bs::SEL); // a plain sell: Clear Street decides
  q = sent(o);
  EXPECT_EQ(q.side, '2');
  EXPECT_EQ(q.open_close, 0);
}

TEST_F(ClearStreetOMTest, UnsupportedCombinationsAreRefusedLocally)
{
  start();
  std::vector<Order> bad;
  auto o = order(1, 100, 23025, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT_EXEMPT;
  bad.push_back(o);
  o = order(2, 100, 23025, en::bs::SEL);
  o.locate_id = "LOC1";
  bad.push_back(o);
  o = order(3);
  o.ord_type = OT::MARKET;
  o.tif = TIF::GTC;
  bad.push_back(o);
  o = order(4);
  o.ord_type = OT::STOP;
  o.stop_px = 100;
  o.tif = TIF::IOC;
  bad.push_back(o);
  o = order(5);
  o.ord_type = OT::MARKET;
  o.tif = TIF::EXTENDED_DAY;
  bad.push_back(o);
  o = order(6);
  o.strategy = "VWAP";
  o.tif = TIF::IOC;
  bad.push_back(o);
  o = order(7);
  o.strategy = "DMA"; // no route
  bad.push_back(o);
  o = order(8);
  o.route = "XLON";
  bad.push_back(o);
  o = order(9);
  o.strategy = "TWAP";
  o.route = "XNAS";
  bad.push_back(o);
  o = order(10);
  o.strategy = "ICEBERG";
  bad.push_back(o);
  o = order(11);
  o.ord_type = OT::STOP_LIMIT;
  o.stop_px = 100;
  o.strategy = "VWAP";
  bad.push_back(o);
  o = order(12);
  o.sz = 10.5;
  bad.push_back(o);
  o = order(13);
  o.sym = 424242; // not configured
  bad.push_back(o);
  o = order(14);
  o.px = 0;
  bad.push_back(o);
  for (size_t i = 0; i < bad.size(); ++i)
  {
    auto r = refused(bad[i], i);
    ASSERT_NE(r, nullptr) << "order " << bad[i].oid;
    EXPECT_EQ(int(r->id), bad[i].oid);
  }
  EXPECT_TRUE(cs.news().empty());
}

TEST_F(ClearStreetOMTest, ShortSalesRefusedWhenMarkingIsOff)
{
  auto c = cfg();
  c.mark_short_open = false;
  start(c);
  auto o = order(1, 100, 23025, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  ASSERT_NE(refused(o, 0), nullptr);
  EXPECT_TRUE(cs.news().empty());
}

TEST_F(ClearStreetOMTest, LimitsAndThrottle)
{
  auto c = cfg();
  c.max_order_qty = 500;
  c.max_notional = 50000;
  c.max_orders_per_sec = 2;
  start(c);
  auto r = refused(order(1, 600), 0);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  r = refused(order(2, 300, 23025), 1); // 69,075 dollars
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  place(order(3, 10));
  place(order(4, 10));
  r = refused(order(5, 10), 2);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  EXPECT_EQ(om->counters().throttled, 1u);
  // tokens refill: available again
  std::this_thread::sleep_for(600ms);
  place(order(6, 10));
  EXPECT_EQ(cs.news().size(), 3u);
}

// ---- acks, fills, rejects ------------------------------------------------------------------

TEST_F(ClearStreetOMTest, AckCarriesTheOrderIdThenPartialAndFullFill)
{
  start();
  auto cl = place(order(1));
  auto a = cs.accepted(cl);
  cs.fill(cl, 40, 230.25, 40);
  cs.fill(cl, 60, 230.24, 100);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 40", "Fill 1 60"}));
  auto ack = somr.wait_for<som::Ack>();
  EXPECT_EQ(ack->xordid, ClearStreetOM::xordid(a.order_id));
  EXPECT_NE(ack->xordid, 0u);
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->pxi, 23025);
  EXPECT_EQ(f[1]->pxi, 23024);
  EXPECT_EQ(int(f[0]->still_to_be_filled), 60);
  EXPECT_EQ(int(f[1]->still_to_be_filled), 0);
  EXPECT_EQ(f[1]->xordid, ack->xordid);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
}

TEST_F(ClearStreetOMTest, PendingNewIsTheAck)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl, 100, 'A'); // queued for deferred release
  cs.accepted(cl);           // then New
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1"}));
}

TEST_F(ClearStreetOMTest, Reject)
{
  start();
  auto cl = place(order(1));
  cs.rejected(cl, 3, "risk");
  settle();
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::EXCHLIMIT);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Reject 1"}));
}

TEST_F(ClearStreetOMTest, SessionRejectOfAnOrderRejectsIt)
{
  // guide: an unknown or unauthorized Account is rejected with Reject (35=3)
  start();
  auto cl = place(order(1));
  cs.session_reject(cl, "D");
  EXPECT_EQ(replies(), (std::vector<std::string>{"Reject 1"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Rejected);
  // a session Reject of something else is logged only
  cs.session_reject("", "0");
  settle();
  EXPECT_EQ(om->counters().session_rejects, 2u);
}

TEST_F(ClearStreetOMTest, RejectAfterAcceptanceEndsTheOrder)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto r = cs.base(cl, '8', '8');
  r.text = "refused by the market";
  cs.exec(r);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1"}));
}

// ---- cancels -------------------------------------------------------------------------------

TEST_F(ClearStreetOMTest, CancelRoundTripUsesANewClOrdID)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto c = cancel_sent(1);
  EXPECT_EQ(c.orig_clordid, cl);
  EXPECT_NE(c.clordid, cl);
  EXPECT_EQ(c.account, "SIM1");
  EXPECT_EQ(c.symbol, "AAPL");
  EXPECT_EQ(c.side, '1');
  EXPECT_EQ(c.qty, "100");
  cs.canceled(c.clordid, cl);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1"}));
}

TEST_F(ClearStreetOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto c = cancel_sent(1);
  cs.cancel_reject(c.clordid, cl, '1', '0', 3);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancReject 1"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
  cs.fill(cl, 100, 230.25, 100);
  EXPECT_EQ(replies().back(), "Fill 1 100");
}

TEST_F(ClearStreetOMTest, CancelOfUnknownOrFilledOrderIsRejectedLocally)
{
  start();
  cancel(99);
  auto r = somr.wait_for<som::CancReject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::NOTFOUND);
  auto cl = place(order(1));
  cs.accepted(cl);
  cs.fill(cl, 100, 230.25, 100);
  settle();
  cancel(1);
  r = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(cs.cancels().empty());
}

TEST_F(ClearStreetOMTest, UnsolicitedCancelAndDoneForDayReachTheSom)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  cs.canceled(cl); // no OrigClOrdID: unsolicited
  auto cl2 = place(order(2));
  cs.accepted(cl2);
  auto d = cs.base(cl2, '3', '3');
  d.leaves = 0;
  cs.exec(d);
  // a GTC order done for the day stays working
  auto g = order(3);
  g.tif = TIF::GTC;
  auto cl3 = place(g);
  cs.accepted(cl3);
  d = cs.base(cl3, '3', '3');
  cs.exec(d);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1", "Ack 2", "CancAck 2", "Ack 3"}));
  EXPECT_EQ(om->book().find_oid(3)->state, State::Working);
}

// ---- races ---------------------------------------------------------------------------------

TEST_F(ClearStreetOMTest, FillBeforeAck)
{
  start();
  auto cl = place(order(1));
  cs.fill(cl, 30, 230.25, 30);
  cs.accepted(cl);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 30"}));
}

TEST_F(ClearStreetOMTest, FillRacingACancel)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto c = cancel_sent(1);
  cs.fill(cl, 100, 230.25, 100); // fills before the cancel lands
  cs.cancel_reject(c.clordid, cl, '1', '2', 0);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 100"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
}

TEST_F(ClearStreetOMTest, FillAfterTheCancelAckStillCounts)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto c = cancel_sent(1);
  cs.canceled(c.clordid, cl);
  cs.fill(c.clordid, 30, 230.25, 30);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1", "Fill 1 30"}));
}

TEST_F(ClearStreetOMTest, CancelReportCarryingAnUnseenFill)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto c = cancel_sent(1);
  cs.canceled(c.clordid, cl, 20, 230.20); // CumQty 20 no fill report showed
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 20", "CancAck 1"}));
  EXPECT_EQ(somr.all<som::Fill>()[0]->pxi, 23020);
}

TEST_F(ClearStreetOMTest, TradeCancelsAndCorrectionsAreNotFills)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto r = cs.base(cl, '2', '2');
  r.exec_trans_type = '1'; // trade cancel (not sent in this version, guide)
  r.cum = 100;
  r.last_qty = 100;
  cs.exec(r);
  r = cs.base(cl, '2', '2');
  r.exec_trans_type = '2'; // trade correct
  r.cum = 100;
  cs.exec(r);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1"}));
  EXPECT_EQ(om->counters().busts, 2u);
}

// ---- replace -------------------------------------------------------------------------------

TEST_F(ClearStreetOMTest, ReplaceAcksTheNewOrderAndCancelsTheOld)
{
  start();
  auto o = order(1, 100, 23025);
  auto cl = place(o);
  cs.accepted(cl);
  cs.fill(cl, 10, 230.25, 10);
  auto n = order(2, 50, 22950);
  n.oid_to_cancel = 1;
  om->send(new Order(n), &somr);
  ASSERT_TRUE(wait_until([&] { return cs.replaces().size() == 1; }));
  auto q = cs.replaces()[0];
  EXPECT_EQ(q.orig_clordid, cl);
  EXPECT_EQ(q.qty, "50");
  EXPECT_EQ(q.px, "229.50");
  EXPECT_EQ(q.side, '1');
  EXPECT_EQ(q.ord_type, '2');
  EXPECT_EQ(q.tif, '0');
  EXPECT_EQ(q.account, "SIM1");
  cs.replaced(q.clordid, cl, 50, 10);
  cs.fill(q.clordid, 40, 229.50, 50, 50);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 10", "CancAck 1", "Ack 2", "Fill 2 40"}));
}

TEST_F(ClearStreetOMTest, ReplaceRejectKeepsTheOldOrder)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto n = order(2, 50, 22950);
  n.oid_to_cancel = 1;
  om->send(new Order(n), &somr);
  ASSERT_TRUE(wait_until([&] { return cs.replaces().size() == 1; }));
  cs.cancel_reject(cs.replaces()[0].clordid, cl, '2', '0', -1);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancReject 1", "Reject 2"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
  // a replace that changes the side is refused locally
  n = order(3, 50, 22950, en::bs::SEL);
  n.oid_to_cancel = 1;
  ASSERT_NE(refused(n, 1), nullptr);
}

// ---- reconnect, duplicates, unknown reports -----------------------------------------------

TEST_F(ClearStreetOMTest, ReconnectDeliversAMissedFillOnce)
{
  start();
  auto cl = place(order(1));
  cs.accepted(cl);
  auto f1 = cs.fill(cl, 40, 230.25, 40);
  cs.logout("connection lost");
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  cs.logon();
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  // Clear Street resends what it sent: the first fill again (same ExecID,
  // PossDup), then the fill missed while down.
  f1.poss_dup = true;
  cs.exec(f1);
  auto again = f1;
  again.exec_id = "other-id-same-cum"; // a replay with a new ExecID still adds nothing
  cs.exec(again);
  cs.fill(cl, 60, 230.25, 100);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 40", "Fill 1 60"}));
  EXPECT_EQ(om->counters().duplicates, 1u);
  EXPECT_EQ(om->counters().covered_fills, 1u);
  EXPECT_EQ(om->counters().logons, 2u);
}

TEST_F(ClearStreetOMTest, UnknownReportsAreLoggedAndIgnored)
{
  start();
  cs.fill("MANUAL-ORDER-1", 10, 230.25, 10);     // placed outside this OM
  cs.fill("20261117-123", 10, 230.25, 10);       // yesterday's id format
  cs.cancel_reject("X1", "X0", '1', '8', 1);
  cs.unparsed("garbage");
  settle();
  EXPECT_TRUE(somr.replies().empty());
  EXPECT_EQ(om->counters().unknown, 3u);
  EXPECT_EQ(om->counters().unparsed, 1u);
}

TEST_F(ClearStreetOMTest, ClientIdsAreUniqueAndARestartDoesNotReuseThem)
{
  start(cfg(), frame::som::ClientIdGen(5000));
  std::set<std::string> ids;
  for (int i = 1; i <= 5; ++i)
    ids.insert(place(order(i)));
  stop();
  start(cfg(), frame::som::ClientIdGen(5001)); // later millisecond, same day
  for (int i = 1; i <= 5; ++i)
    ids.insert(place(order(i)));
  stop();
  start(cfg(), frame::som::ClientIdGen(5000), true, "20261119"); // next day, same clock
  for (int i = 1; i <= 5; ++i)
    ids.insert(place(order(i)));
  EXPECT_EQ(ids.size(), 15u);
  auto n = cs.news();
  for (size_t i = 1; i < 5; ++i)
    EXPECT_LT(std::stoull(n[i - 1].clordid.substr(9)), std::stoull(n[i].clordid.substr(9)));
  for (auto &q : n)
    EXPECT_LE(q.clordid.size(), 64u);
}

TEST_F(ClearStreetOMTest, ClOrdIdPrefix)
{
  auto c = cfg();
  c.clordid_prefix = "K";
  start(c);
  auto cl = place(order(1));
  EXPECT_EQ(cl.rfind("K20261118-", 0), 0u);
  frame::som::ClientId cid;
  EXPECT_TRUE(om->parse_clordid(cl, cid));
  EXPECT_FALSE(om->parse_clordid("20261118-" + std::to_string(cid), cid));
}

TEST_F(ClearStreetOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto cl = place(order(1));
  for (int i = 0; i < 200; ++i) // queued in the OM's mailbox when shutdown starts
    cs.fill(cl, 0.0, 230.25, 0);
  cs.accepted(cl);
  stop();
  SUCCEED();
}
