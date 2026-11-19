/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LmaxOM against FakeLmax, on its own actor thread under a kaspar Manager as
// in production. "SOM" is a recorder: orders come from it, replies go back
// to it. The test thread plays QuickFIX's thread. The list follows
// order_entry.md, Tests.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeLmax.hpp"
#include "TestUtil.hpp"
#include "lmax_om/act/LmaxOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace lmax::om;
using namespace lmax::om::test;
using frame::som::State;

namespace
{
  class LmaxOMTest : public ::testing::Test
  {
  protected:
    FakeLmax lmax;
    Recorder somr{"SOM"};
    Recorder sink{"PostTrade"};
    std::unique_ptr<actors::Manager> mgr;
    LmaxOM *om = nullptr;
    const frame::ref::Asset *eurusd = nullptr, *usdjpy = nullptr;

    void SetUp() override
    {
      eurusd = fx_asset("EURUSD_T", 0.00001);
      usdjpy = fx_asset("USDJPY_T", 0.001);
    }
    void TearDown() override { stop(); }

    LmaxOMConfig cfg()
    {
      LmaxOMConfig c;
      c.instruments[eurusd->id] = OmInstrument{"4001", "EUR/USD", 0.00001, 0.1};
      c.instruments[usdjpy->id] = OmInstrument{"4004", "USD/JPY", 0.001, 0.1};
      c.account = "ACC1";
      return c;
    }
    void start(LmaxOMConfig c, frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000), bool wait_ready = true)
    {
      om = new LmaxOM(c, &somr, lmax.factory(), en::x::UNI, ids, &sink);
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

    Order order(int oid, int qty = 10, int px = 115230, en::bs side = en::bs::BUY)
    {
      Order o("FX", en::x::UNI, eurusd->id, qty, px, side, 4);
      o.oid = oid;
      return o;
    }
    // Send as the SOM does; wait until it reaches LMAX; return its ClOrdID.
    std::string place(const Order &o)
    {
      auto before = lmax.news().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return lmax.news().size() > before; })) << "order never reached LMAX";
      auto n = lmax.news();
      return n.empty() ? std::string() : n.back().clordid;
    }
    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }
    void cancel(int oid) { om->send(new som::Cancel(oid), &somr); }
    CancelReq cancel_sent(int oid)
    {
      auto before = lmax.cancels().size();
      cancel(oid);
      EXPECT_TRUE(wait_until([&] { return lmax.cancels().size() > before; }));
      auto c = lmax.cancels();
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

// ---- logon, registration, configuration ---------------------------------------------------

TEST_F(LmaxOMTest, LogsOnAndRegistersWithTheSom)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::UNI);
  EXPECT_EQ(reg->manager, om);
  EXPECT_EQ(lmax.sessions(), 1);
}

TEST_F(LmaxOMTest, BadConfigGivesNullNotCrash)
{
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "lmax_om_cfg_test";
  fs::create_directories(dir);
  std::ofstream(dir / "inst.csv") << "4001,EUR/USD,7001,0.00001,0.1\n";
  std::ofstream(dir / "key.json") << R"({"environment":"demo","username":"u","password":"p","trade_target_comp_id":"T"})";
  auto write = [&](const std::string &env, const std::string &host, const std::string &extra = "") {
    std::ofstream(dir / "om.ini") << "venue = UNI\nenvironment = " << env << "\nhost = " << host
                                  << "\nkey_file = " << (dir / "key.json").string() << "\ninstruments = "
                                  << (dir / "inst.csv").string() << "\ndictionary = " << LMAX_TRADING_DICT
                                  << "\nstore_path = " << (dir / "store").string() << "\n"
                                  << extra;
    return (dir / "om.ini").string();
  };
  EXPECT_EQ(create_LmaxOM((dir / "missing.ini").string(), &somr), nullptr);
  EXPECT_EQ(create_LmaxOM(write("demo", "fix-order.london-perpetual.lmax.com"), &somr), nullptr);
  EXPECT_EQ(create_LmaxOM(write("production", "fix-order.london-demo.lmax.com"), &somr), nullptr);
  EXPECT_EQ(create_LmaxOM(write("demo", "fix.lmaxtrader.com"), &somr), nullptr);
  LmaxOMConfig c;
  en::x v;
  std::string err;
  EXPECT_TRUE(LmaxOMConfig::from_file(write("demo", "fix-order.london-demo.lmax.com"), c, v, err)) << err;
  EXPECT_EQ(c.instruments.at(7001).security_id, "4001");
  EXPECT_FALSE(c.session.params.reset_on_logon);
  EXPECT_EQ(c.session.params.start_day, "Sat");
  std::ofstream(dir / "om.ini", std::ios::app) << "";
  EXPECT_FALSE(LmaxOMConfig::from_file(write("demo", "fix-order.london-demo.lmax.com", "venue = NOPE\n"), c, v, err));
  fs::remove_all(dir);
}

TEST_F(LmaxOMTest, OrdersBeforeLogonAndWhileDisconnectedAreRejectedLocally)
{
  lmax.auto_logon = false;
  start(cfg(), frame::som::ClientIdGen(5000), false);
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(lmax.news().empty());
  lmax.logon();
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  lmax.connected = false; // QuickFIX refuses the send
  auto r2 = refused(order(2), 1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::DISCONNECTED);
  lmax.connected = true;
  lmax.logout("bye");
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto r3 = refused(order(3), 2);
  ASSERT_NE(r3, nullptr);
  EXPECT_EQ(r3->reason, som::Reject::NOTLOGGEDIN);
}

// ---- fields as sent --------------------------------------------------------------------------

TEST_F(LmaxOMTest, LimitOrderFields)
{
  start();
  place(order(1, 25, 115231, en::bs::SEL));
  auto n = lmax.news().at(0);
  EXPECT_EQ(n.security_id, "4001");
  EXPECT_EQ(n.side, '2');
  EXPECT_EQ(n.qty, "2.5"); // 25 units of 0.1 contract
  EXPECT_EQ(n.ord_type, '2');
  EXPECT_EQ(n.px, "1.15231");
  EXPECT_EQ(n.tif, '0');
  EXPECT_EQ(n.account, "ACC1");
  EXPECT_FALSE(n.reinstate);
  EXPECT_LE(n.clordid.size(), 20u);
}

TEST_F(LmaxOMTest, OrderTypesAndTimeInForce)
{
  auto c = cfg();
  c.reinstate_on_disconnect = true;
  start(c);
  auto o = order(1);
  o.ord_type = Order::OrdType::MARKET;
  o.tif = Order::TimeInForce::IOC;
  place(o);
  o = order(2);
  o.ord_type = Order::OrdType::STOP;
  o.stop_px = 115300;
  o.tif = Order::TimeInForce::GTC;
  place(o);
  o = order(3);
  o.ord_type = Order::OrdType::STOP_LIMIT;
  o.stop_px = 115300;
  o.tif = Order::TimeInForce::FOK;
  place(o);
  o = order(4);
  o.sym = usdjpy->id;
  o.px = 155123;
  place(o);
  auto n = lmax.news();
  ASSERT_EQ(n.size(), 4u);
  EXPECT_EQ(n[0].ord_type, '1');
  EXPECT_TRUE(n[0].px.empty());
  EXPECT_EQ(n[0].tif, '3');
  EXPECT_EQ(n[1].ord_type, '3');
  EXPECT_EQ(n[1].stop_px, "1.15300");
  EXPECT_TRUE(n[1].px.empty());
  EXPECT_EQ(n[1].tif, '1');
  EXPECT_EQ(n[2].ord_type, '4');
  EXPECT_EQ(n[2].px, "1.15230");
  EXPECT_EQ(n[2].tif, '4');
  EXPECT_EQ(n[3].security_id, "4004");
  EXPECT_EQ(n[3].px, "155.123");
  for (auto &x : n)
    EXPECT_TRUE(x.reinstate);
}

TEST_F(LmaxOMTest, UnsupportedCombinationsAreRefusedLocally)
{
  auto c = cfg();
  c.max_order_qty = 100;
  start(c);
  std::vector<std::function<void(Order &)>> bad = {
      [](Order &o) { o.tif = Order::TimeInForce::AT_OPEN; },
      [](Order &o) { o.tif = Order::TimeInForce::AT_CLOSE; },
      [](Order &o) { o.tif = Order::TimeInForce::EXTENDED_DAY; },
      [](Order &o) { o.strategy = "VWAP"; },
      [](Order &o) { o.route = "EBS"; },
      [](Order &o) { o.sz = 2.5; },
      [](Order &o) { o.sz = 101; },
      [](Order &o) { o.sym = 999999; },
      [](Order &o) { o.ord_type = Order::OrdType::STOP; o.stop_px = 0; },
  };
  for (size_t i = 0; i < bad.size(); ++i)
  {
    auto o = order(int(i) + 1);
    bad[i](o);
    ASSERT_NE(refused(o, i), nullptr) << i;
  }
  EXPECT_TRUE(lmax.news().empty());
}

TEST_F(LmaxOMTest, Throttle)
{
  auto c = cfg();
  c.max_orders_per_sec = 2;
  start(c);
  for (int i = 1; i <= 3; ++i)
    om->send(new Order(order(i)), &somr);
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  EXPECT_EQ(lmax.news().size(), 2u);
}

// ---- reports -----------------------------------------------------------------------------------

TEST_F(LmaxOMTest, AckPartialAndFullFill)
{
  start();
  auto cl = place(order(1, 10, 115230));
  lmax.accepted(cl);
  auto a = somr.wait_for<som::Ack>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->id, 1u);
  EXPECT_NE(a->xordid, 0u);
  lmax.trade(cl, 0.4, 1.15230, 0.4, 0.6);
  lmax.trade(cl, 0.6, 1.15229, 1.0, 0.0);
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->sz, 4);
  EXPECT_EQ(f[0]->pxi, 115230);
  EXPECT_EQ(f[0]->still_to_be_filled, 6);
  EXPECT_EQ(f[1]->sz, 6);
  EXPECT_EQ(f[1]->pxi, 115229);
  EXPECT_EQ(f[1]->still_to_be_filled, 0);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 4", "Fill 1 6"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
  ASSERT_TRUE(wait_until([&] { return sink.all<msg::ExecCopy>().size() == 2; }));
}

TEST_F(LmaxOMTest, Reject)
{
  start();
  auto cl = place(order(1));
  lmax.rejected(cl, 3, "exceeds limit");
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::EXCHLIMIT);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Rejected);
}

TEST_F(LmaxOMTest, CancelRoundTripUsesANewClOrdID)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto c = cancel_sent(1);
  EXPECT_EQ(c.orig_clordid, cl);
  EXPECT_NE(c.clordid, cl);
  EXPECT_EQ(c.security_id, "4001");
  EXPECT_EQ(c.side, '1');
  lmax.canceled(c.clordid, cl);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1"}));
}

TEST_F(LmaxOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto c = cancel_sent(1);
  lmax.cancel_reject(c.clordid, cl, '1', '0', 99);
  auto r = somr.wait_for<som::CancReject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::FROMEXCHANGE);
  settle();
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
  // A second cancel works.
  auto c2 = cancel_sent(1);
  lmax.canceled(c2.clordid, cl);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(LmaxOMTest, CancelOfUnknownOrFilledOrderIsRejectedLocally)
{
  start();
  cancel(77);
  auto r = somr.wait_for<som::CancReject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::NOTFOUND);
  auto cl = place(order(1));
  lmax.trade(cl, 1.0, 1.1523, 1.0, 0);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  cancel(1);
  auto r2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(lmax.cancels().empty());
}

TEST_F(LmaxOMTest, UnsolicitedCancelAndExpiryReachTheSom)
{
  start();
  auto c1 = place(order(1));
  lmax.accepted(c1);
  lmax.canceled(c1); // cancelled on disconnect or by LMAX
  auto o = order(2);
  o.tif = Order::TimeInForce::IOC;
  auto c2 = place(o);
  lmax.accepted(c2);
  lmax.canceled(c2, {}, 0, 'C'); // IOC expired
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1", "Ack 2", "CancAck 2"}));
}

TEST_F(LmaxOMTest, FillBeforeAck)
{
  start();
  auto cl = place(order(1));
  lmax.trade(cl, 1.0, 1.1523, 1.0, 0);
  lmax.accepted(cl);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 10"}));
}

TEST_F(LmaxOMTest, FillRacingACancel)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto c = cancel_sent(1);
  lmax.trade(cl, 1.0, 1.1523, 1.0, 0);          // filled while the cancel was in flight
  lmax.cancel_reject(c.clordid, cl, '1', '2', 0); // too late: filled
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 10"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
}

TEST_F(LmaxOMTest, FillAfterTheCancelAckStillCounts)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto c = cancel_sent(1);
  lmax.canceled(c.clordid, cl);
  lmax.trade(cl, 0.3, 1.1523, 0.3, 0);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1", "Fill 1 3"}));
}

TEST_F(LmaxOMTest, CancelReportCarryingAnUnseenFill)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto c = cancel_sent(1);
  lmax.canceled(c.clordid, cl, 0.2); // CumQty 0.2: a fill no trade report showed
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 2", "CancAck 1"}));
}

TEST_F(LmaxOMTest, TradeCorrectionsAndCancelsAreNotFills)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto r = lmax.base(cl, 'H', '0');
  lmax.exec(r);
  r = lmax.base(cl, 'G', '0');
  lmax.exec(r);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1"}));
  EXPECT_EQ(om->counters().busts, 2u);
  ASSERT_TRUE(wait_until([&] { return sink.all<msg::ExecCopy>().size() == 2; }));
}

TEST_F(LmaxOMTest, DuplicateReportsAreAppliedOnce)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  auto t = lmax.trade(cl, 0.5, 1.1523, 0.5, 0.5);
  t.poss_dup = true;
  lmax.exec(t); // the same ExecID again (a resend)
  auto t2 = t;
  t2.exec_id = "OTHER-ID";
  lmax.exec(t2); // a different ExecID with the same CumQty
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Fill 1 5"}));
  EXPECT_EQ(om->counters().duplicates, 1u);
  EXPECT_EQ(om->counters().covered_fills, 1u);
}

// ---- replace -------------------------------------------------------------------------------------

TEST_F(LmaxOMTest, ReplaceAcksTheNewOrderAndCancelsTheOld)
{
  start();
  auto cl = place(order(1, 10, 115200));
  lmax.accepted(cl);
  auto r = order(2, 20, 115210);
  r.oid_to_cancel = 1;
  om->send(new Order(r), &somr);
  ASSERT_TRUE(wait_until([&] { return lmax.replaces().size() == 1; }));
  auto g = lmax.replaces()[0];
  EXPECT_EQ(g.orig_clordid, cl);
  EXPECT_NE(g.clordid, cl);
  EXPECT_EQ(g.qty, "2.0");
  EXPECT_EQ(g.px, "1.15210");
  EXPECT_EQ(g.ord_type, '2');
  lmax.replaced(g.clordid, cl, 2.0);
  lmax.trade(g.clordid, 0.5, 1.1521, 0.5, 1.5);
  auto c = cancel_sent(2);
  EXPECT_EQ(c.orig_clordid, g.clordid);
  lmax.canceled(c.clordid, g.clordid, 0.5);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancAck 1", "Ack 2", "Fill 2 5", "CancAck 2"}));
  EXPECT_EQ(om->clordid(2), g.clordid);
}

TEST_F(LmaxOMTest, ReplaceRejectKeepsTheOldOrder)
{
  start();
  auto cl = place(order(1, 10, 115200));
  lmax.accepted(cl);
  auto r = order(2, 20, 115210);
  r.oid_to_cancel = 1;
  om->send(new Order(r), &somr);
  ASSERT_TRUE(wait_until([&] { return lmax.replaces().size() == 1; }));
  auto g = lmax.replaces()[0];
  lmax.cancel_reject(g.clordid, cl, '2', '0', 99);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "CancReject 1", "Reject 2"}));
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
  EXPECT_EQ(om->book().find_oid(1)->qty, 10);
  lmax.trade(cl, 1.0, 1.152, 1.0, 0); // the old order still fills under its own id
  EXPECT_EQ(replies().back(), "Fill 1 10");
}

TEST_F(LmaxOMTest, ReplaceWhileACancelIsInFlightIsRefused)
{
  start();
  auto cl = place(order(1));
  lmax.accepted(cl);
  cancel_sent(1);
  auto r = order(2, 20, 115210);
  r.oid_to_cancel = 1;
  ASSERT_NE(refused(r), nullptr);
  EXPECT_TRUE(lmax.replaces().empty());
}

// ---- reconnect ---------------------------------------------------------------------------------

TEST_F(LmaxOMTest, ReconnectAsksForStatusAndDeliversAMissedFillOnce)
{
  start();
  auto c1 = place(order(1, 10));
  auto c2 = place(order(2, 10));
  lmax.accepted(c1);
  lmax.accepted(c2);
  lmax.trade(c1, 0.2, 1.1523, 0.2, 0.8);
  lmax.logout();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  lmax.logon();
  ASSERT_TRUE(wait_until([&] { return lmax.statuses().size() == 2; }));
  auto s = lmax.statuses();
  EXPECT_EQ(s[0].security_id, "4001");
  // While disconnected: order 1 filled 0.3 more at 1.15240 (avg 1.15236), order 2 was cancelled.
  lmax.status(c1, '1', 0.5, (0.2 * 1.15230 + 0.3 * 1.15240) / 0.5);
  lmax.status(c2, '4', 0, 0);
  // Then the resent trade report arrives: already counted.
  lmax.trade(c1, 0.3, 1.15240, 0.5, 0.5);
  EXPECT_EQ(replies(), (std::vector<std::string>{"Ack 1", "Ack 2", "Fill 1 2", "Fill 1 3", "CancAck 2"}));
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[1]->pxi, 115240);
  EXPECT_EQ(om->counters().status_fills, 1u);
  EXPECT_EQ(om->counters().covered_fills, 1u);
}

TEST_F(LmaxOMTest, UnknownReportsAreLoggedAndIgnored)
{
  start();
  lmax.trade("MANUAL-ORDER-1", 1.0, 1.1, 1.0, 0); // placed outside this OM
  lmax.accepted("99999");
  lmax.cancel_reject("123", "122", '1', '8', 1);
  lmax.unparsed("ExecutionReport without required tag 37");
  settle();
  EXPECT_TRUE(replies().empty());
  EXPECT_EQ(om->counters().unknown, 3u);
  EXPECT_EQ(om->counters().unparsed, 1u);
}

TEST_F(LmaxOMTest, ClientIdsAreUniqueAndARestartDoesNotReuseThem)
{
  start(cfg(), frame::som::ClientIdGen(5000));
  auto a = place(order(1));
  auto b = place(order(2));
  EXPECT_LT(std::stoull(a), std::stoull(b));
  stop();
  start(cfg(), frame::som::ClientIdGen(5001)); // a later millisecond
  auto c = place(order(3));
  EXPECT_GT(std::stoull(c), std::stoull(b));
  EXPECT_LE(c.size(), 20u);
}

TEST_F(LmaxOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto cl = place(order(1));
  for (int i = 0; i < 200; ++i)
    lmax.accepted(cl);
  stop();
  SUCCEED();
}
