/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * CbxOM against FakeCbx, run as an actor under a Manager, in the form of the
 * Nasdaq OUCH OM's tests (order_entry.md, "Tests"). The venue's reports are
 * built from the field tables of Coinbase's FIX 5.0 order-entry page
 * (exchange/fix-api/order-entry-messages/order-entry-messages5, retrieved
 * 2026-11-18); that page has no wire examples.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <set>

#include "chutil/Assert.hpp"
#include "actors/act/Manager.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeCbx.hpp"
#include "TestUtil.hpp"
#include "coinbase_exchange_oe/act/CbxOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace coinbase_exchange;
using namespace coinbase_exchange::oe;
using namespace coinbase_exchange::oe::test;
using namespace coinbase_exchange::fix;
using frame::som::State;

namespace
{
  const char *ORDER_ID = "7b52009b-64fd-4a2a-89e6-d8a939753077";

  class CbxOMTest : public ::testing::Test
  {
  protected:
    FakeCbxServer cb;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    CbxOM *om = nullptr;
    const frame::ref::Asset *btc = nullptr;

    void SetUp() override { btc = test_product("BTC-USD"); }

    static CbxOMConfig cfg()
    {
      CbxOMConfig c;
      c.session.environment = Environment::Sandbox;
      c.reconnect_ms = 50;
      OEProduct p;
      std::string err;
      EXPECT_TRUE(OEProduct::make("BTC-USD", "0.01", "0.00000001", "0.0001", p, err)) << err;
      c.products["BTC-USD"] = p;
      return c;
    }

    void start(bool wait_ready = true, CbxOMConfig c = cfg(),
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000), uint64_t seed = 42)
    {
      om = new CbxOM(c, &somr, cb.factory(), en::x::COINBASEX, ids, seed);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      if (wait_ready)
        ASSERT_TRUE(wait_until([&] { return om->ready(); })) << "never ready";
    }
    void stop()
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      om = nullptr;
    }
    void TearDown() override { stop(); }

    // 25 units of 0.0001 BTC at 64,000.00 (6,400,000 ticks of 0.01)
    Order order(int oid, int qty = 25, int px = 6400000, en::bs side = en::bs::BUY)
    {
      Order o("CB", en::x::COINBASEX, btc->id, qty, px, side, 4);
      o.oid = oid;
      return o;
    }
    // Send as the SOM does; wait until it reaches Coinbase; return the 35=D.
    FixMsg place(const Order &o)
    {
      auto before = cb.sent("D").size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return cb.sent("D").size() > before; })) << "order never reached Coinbase";
      auto d = cb.sent("D");
      return d.empty() ? FixMsg() : d.back();
    }
    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }
    void cancel(int oid) { om->send(new som::Cancel(oid), &somr); }
    void settle() { std::this_thread::sleep_for(150ms); }

    // ExecutionReport for an order sent as `d`
    FixMsg er(const FixMsg &d, char exec_type, char ord_status, const std::string &cum = "0",
              const std::string &leaves = "0.0025")
    {
      static int n = 0;
      FixMsg r("8");
      r.set(ClOrdID, d.get(ClOrdID)).set(OrderID, ORDER_ID);
      r.set(ExecID, "e" + std::to_string(++n) + "-00000000-0000-4000-8000-000000000000");
      r.set(OrdStatus, ord_status).set(ExecType, exec_type).set(Symbol, "BTC-USD").set(Side, d.get(Side));
      r.set(OrdType, d.get(OrdType)).set(Price, d.get(Price)).set(OrderQty, d.get(OrderQty));
      r.set(CumQty, cum).set(LeavesQty, leaves).set(TransactTime, "20261118-17:25:47.637");
      return r;
    }
    FixMsg trade(const FixMsg &d, const std::string &qty, const std::string &px, const std::string &cum,
                 const std::string &leaves, const std::string &exec_id = "")
    {
      auto r = er(d, 'F', leaves == "0" || leaves == "0.0000" ? '2' : '1', cum, leaves);
      r.set(LastQty, qty).set(LastPx, px).set(TradeID, "1135493769").set(AggressorIndicator, 'N');
      if (!exec_id.empty())
        r.set(ExecID, exec_id);
      r.add(NoMiscFees, "1").add(MiscFeeAmt, "0.0005").add(MiscFeeCurr, "USD").add(MiscFeeType, "4").add(MiscFeeBasis, "2");
      return r;
    }
    void ack(const FixMsg &d) { ASSERT_TRUE(cb.deliver(er(d, '0', '0'))); }
  };
} // namespace

// ---- logon, registration, configuration ---------------------------------------------------

TEST_F(CbxOMTest, LogsOnAndRegistersWithTheSom)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::COINBASEX);
  EXPECT_EQ(reg->manager, om);
  EXPECT_EQ(cb.sessions().size(), 1u);
}

TEST_F(CbxOMTest, SessionIsCreatedOnTheActorThread)
{
  start();
  EXPECT_NE(cb.sessions()[0].created_on, std::this_thread::get_id());
}

TEST_F(CbxOMTest, BadConfigGivesNullNotCrash)
{
  auto c = cfg();
  c.products.clear();
  EXPECT_EQ(create_CbxOM(c, &somr, cb.factory()), nullptr);
  c = cfg();
  c.session.heartbeat_s = 31;
  EXPECT_EQ(create_CbxOM(c, &somr, cb.factory()), nullptr) << "Coinbase: HeartBtInt <= 30";
  c = cfg();
  c.day_tif = 'X';
  EXPECT_EQ(create_CbxOM(c, &somr, cb.factory()), nullptr);
  EXPECT_EQ(create_CbxOM(std::string("/nonexistent/cbx.ini"), &somr), nullptr);
}

TEST_F(CbxOMTest, ConfigFileIsRead)
{
  const char *k = std::getenv("KSPRPROJ");
  std::string path = std::string(k ? k : "") +
                     "/global_connectivity/non_certified/order_entry/coinbase_exchange/config/cbx_om_example.ini";
  CbxOMConfig c;
  std::string err;
  ASSERT_TRUE(CbxOMConfig::from_file(path, c, err)) << err;
  EXPECT_EQ(c.session.environment, Environment::Sandbox);
  EXPECT_EQ(c.session.cancel_on_disconnect, 'Y');
  EXPECT_EQ(c.day_tif, '1');
  ASSERT_EQ(c.products.count("BTC-USD"), 1u);
  EXPECT_EQ(c.products["BTC-USD"].qty_unit, 1) << "one SOM unit = base_increment by default";
}

TEST_F(CbxOMTest, OrderBeforeLogonIsRejected)
{
  cb.auto_logon = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 1; }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(cb.sent("D").empty());
}

// The connection never comes up (TCP or TLS): QuickFIX tells nobody, so the
// OM gives up on the session after logon_timeout_ms and makes a new one.
TEST_F(CbxOMTest, NoLogonAnswerMakesANewSession)
{
  cb.auto_logon = false;
  auto c = cfg();
  c.logon_timeout_ms = 50;
  start(false, c);
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 1; }));
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 2; })) << "dropped after the logon timeout";
  EXPECT_FALSE(om->ready());
  cb.auto_logon = true;
  ASSERT_TRUE(wait_until([&] { return om->ready(); })) << "a later session logs on";
  EXPECT_GE(cb.sessions().size(), 3u);
  auto n = cb.sessions().size();
  settle();
  EXPECT_EQ(cb.sessions().size(), n) << "logged on: the timeout does not fire";
}

TEST_F(CbxOMTest, LogonRejectedStaysDown)
{
  cb.reject_logon = true;
  start(false);
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 1; }));
  settle();
  EXPECT_EQ(cb.sessions().size(), 1u) << "a bad signature or key does not get better by retrying";
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
}

// ---- what is sent ------------------------------------------------------------------------------

TEST_F(CbxOMTest, NewOrderSingleFields)
{
  start();
  auto d = place(order(1));
  EXPECT_TRUE(is_uuid_v4_lower(d.get(ClOrdID))) << d.get(ClOrdID);
  EXPECT_EQ(d.get(Symbol), "BTC-USD");
  EXPECT_EQ(d.get(Side), "1");
  EXPECT_EQ(d.get(OrderQty), "0.00250000") << "25 units of 0.0001 BTC";
  EXPECT_EQ(d.get(OrdType), "2");
  EXPECT_EQ(d.get(Price), "64000.00");
  EXPECT_EQ(d.get(TimeInForce), "1") << "SOM DAY goes out as GTC by default: Coinbase has no DAY";
  EXPECT_EQ(d.get(SelfTradeType), "D");
  EXPECT_FALSE(d.has(ExecInst));
  EXPECT_EQ(om->clordid(1), d.get(ClOrdID));
}

TEST_F(CbxOMTest, TimeInForceAndOrderTypes)
{
  auto c = cfg();
  c.post_only = true;
  start(true, c);
  auto o = order(1);
  o.tif = Order::TimeInForce::IOC;
  EXPECT_EQ(place(o).get(TimeInForce), "3");
  o = order(2);
  o.tif = Order::TimeInForce::FOK;
  EXPECT_EQ(place(o).get(TimeInForce), "4");
  o = order(3);
  o.tif = Order::TimeInForce::GTC;
  auto d = place(o);
  EXPECT_EQ(d.get(TimeInForce), "1");
  EXPECT_EQ(d.get(ExecInst), "A") << "post-only";
  o = order(4);
  o.ord_type = Order::OrdType::MARKET;
  d = place(o);
  EXPECT_EQ(d.get(OrdType), "1");
  EXPECT_FALSE(d.has(Price));
  EXPECT_EQ(d.get(TimeInForce), "3") << "a market order goes IOC";
  EXPECT_FALSE(d.has(ExecInst)) << "post-only applies to limit orders";
  o = order(5);
  o.ord_type = Order::OrdType::STOP_LIMIT;
  o.stop_px = 6390000;
  d = place(o);
  EXPECT_EQ(d.get(OrdType), "4");
  EXPECT_EQ(d.get(StopPx), "63900.00");
  EXPECT_EQ(d.get(Price), "64000.00");
}

TEST_F(CbxOMTest, DayAsGtdUntilUtcMidnight)
{
  auto c = cfg();
  c.day_tif = '6';
  start(true, c);
  auto d = place(order(1));
  EXPECT_EQ(d.get(TimeInForce), "6");
  ASSERT_TRUE(d.has(ExpireTime));
  EXPECT_EQ(d.get(ExpireTime).substr(8), "-00:00:00.000");
}

TEST_F(CbxOMTest, SidesAndShortSale)
{
  start();
  EXPECT_EQ(place(order(1, 25, 6400000, en::bs::SEL)).get(Side), "2");
  auto o = order(2, 25, 6400000, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  auto r = refused(o);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->id, 2u);
}

TEST_F(CbxOMTest, UnsupportedOrdersAreRefusedLocally)
{
  auto c = cfg();
  c.day_tif = 'R';
  start(true, c);
  size_t n = 0;
  auto refuse = [&](Order o, const char *what) {
    om->send(new Order(o), &somr);
    EXPECT_NE(somr.wait_for<som::Reject>(n++), nullptr) << what;
  };
  auto o = order(1);
  refuse(o, "DAY with day_tif = reject");
  o = order(2);
  o.tif = Order::TimeInForce::AT_OPEN;
  refuse(o, "at the open");
  o = order(3);
  o.tif = Order::TimeInForce::GTC;
  o.ord_type = Order::OrdType::STOP;
  refuse(o, "plain stop");
  o = order(4);
  o.tif = Order::TimeInForce::GTC;
  o.route = "SOR";
  refuse(o, "route");
  o = order(5);
  o.tif = Order::TimeInForce::GTC;
  o.strategy = "VWAP";
  refuse(o, "algorithm");
  o = order(6);
  o.tif = Order::TimeInForce::GTC;
  o.oid_to_cancel = 1;
  refuse(o, "cancel/replace");
  o = order(7);
  o.tif = Order::TimeInForce::GTC;
  o.sz = 2.5;
  refuse(o, "fractional units");
  auto eth = test_product("ETH-USD");
  o = Order("CB", en::x::COINBASEX, eth->id, 1, 100, en::bs::BUY, 4);
  o.oid = 8;
  o.tif = Order::TimeInForce::GTC;
  refuse(o, "not in the products table");
  settle();
  EXPECT_TRUE(cb.sent("D").empty());
}

TEST_F(CbxOMTest, LocalRiskLimits)
{
  auto c = cfg();
  c.max_order_qty = 100;
  c.max_order_notional = 1000; // USD; 25 units at 64,000 = 160 USD
  c.max_orders_per_sec = 2;
  start(true, c);
  auto r = refused(order(1, 101));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  place(order(2, 100)); // 0.01 BTC at 64,000 = 640 USD: under the limit
  place(order(3, 25));
  r = refused(order(4, 25), 1);
  ASSERT_NE(r, nullptr) << "third order in the second: over max_orders_per_sec";
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
}

// ---- reports -------------------------------------------------------------------------------------

TEST_F(CbxOMTest, AckCarriesTheExchangeOrderId)
{
  start();
  auto d = place(order(1));
  ack(d);
  auto a = somr.wait_for<som::Ack>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->id, 1u);
  EXPECT_EQ(a->xordid, order_id64(*parse_uuid(ORDER_ID))) << "the id the market-data handler gives it in OB";
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
}

TEST_F(CbxOMTest, PartialThenFullFill)
{
  start();
  auto d = place(order(1));
  ack(d);
  cb.deliver(trade(d, "0.001", "64000.00", "0.001", "0.0015"));
  cb.deliver(trade(d, "0.0015", "63999.99", "0.0025", "0"));
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->sz, 10);
  EXPECT_EQ(f[0]->still_to_be_filled, 15);
  EXPECT_EQ(f[0]->pxi, 6400000);
  EXPECT_EQ(f[1]->sz, 15);
  EXPECT_EQ(f[1]->pxi, 6399999);
  EXPECT_EQ(f[1]->still_to_be_filled, 0);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

TEST_F(CbxOMTest, RejectGivesReject)
{
  start();
  auto d = place(order(1));
  auto r = er(d, '8', '8');
  r.set(OrdRejReason, "1").set(Text, "Unknown Symbol");
  cb.deliver(r);
  auto j = somr.wait_for<som::Reject>();
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->id, 1u);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Rejected);
}

TEST_F(CbxOMTest, BusinessRejectOfANewOrderIsAReject)
{
  start();
  auto d = place(order(1));
  FixMsg j("j");
  j.set(RefSeqNum, "2").set(RefMsgType, "D").set(BusinessRejectRefID, d.get(ClOrdID)).set(BusinessRejectReason, "1");
  j.set(Text, "Invalid ClOrdID");
  cb.deliver(j);
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
}

TEST_F(CbxOMTest, CancelRoundTrip)
{
  start();
  auto d = place(order(1));
  ack(d);
  cancel(1);
  ASSERT_TRUE(wait_until([&] { return cb.sent("F").size() == 1; }));
  auto f = cb.sent("F")[0];
  EXPECT_TRUE(is_uuid_v4_lower(f.get(ClOrdID)));
  EXPECT_NE(f.get(ClOrdID), d.get(ClOrdID)) << "a cancel takes a new ClOrdID";
  EXPECT_EQ(f.get(OrigClOrdID), d.get(ClOrdID));
  EXPECT_EQ(f.get(OrderID), ORDER_ID);
  EXPECT_EQ(f.get(Symbol), "BTC-USD");
  auto c = er(d, '4', '4', "0", "0");
  c.set(ClOrdID, f.get(ClOrdID)).set(OrigClOrdID, d.get(ClOrdID));
  cb.deliver(c);
  auto a = somr.wait_for<som::CancAck>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->id, 1u);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Canceled);
}

TEST_F(CbxOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto d = place(order(1));
  ack(d);
  cancel(1);
  ASSERT_TRUE(wait_until([&] { return cb.sent("F").size() == 1; }));
  auto f = cb.sent("F")[0];
  FixMsg r("9");
  r.set(ClOrdID, f.get(ClOrdID)).set(OrderID, ORDER_ID).set(OrigClOrdID, d.get(ClOrdID)).set(OrdStatus, '8');
  r.set(CxlRejReason, "2").set(CxlRejResponseTo, '1').set(Text, "Too late");
  cb.deliver(r);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
  cb.deliver(trade(d, "0.0025", "64000.00", "0.0025", "0"));
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr) << "still working: it can fill";
}

TEST_F(CbxOMTest, CancelOfUnknownOrFilledOrder)
{
  start();
  cancel(99);
  auto r = somr.wait_for<som::CancReject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::NOTFOUND);
  auto d = place(order(1));
  ack(d);
  cb.deliver(trade(d, "0.0025", "64000.00", "0.0025", "0"));
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  cancel(1);
  r = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(cb.sent("F").empty());
}

TEST_F(CbxOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto d = place(order(1));
  ack(d);
  auto c = er(d, '4', '4', "0", "0");
  c.set(Text, "Self Trade Prevention");
  cb.deliver(c);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(CbxOMTest, IocPartialFillThenExpired)
{
  start();
  auto o = order(1);
  o.tif = Order::TimeInForce::IOC;
  auto d = place(o);
  ack(d);
  cb.deliver(trade(d, "0.001", "64000.00", "0.001", "0.0015"));
  cb.deliver(er(d, 'C', 'C', "0.001", "0"));
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  EXPECT_EQ(somr.all<som::CancAck>()[0]->fillsz, 10);
}

TEST_F(CbxOMTest, FillBeforeAck)
{
  start();
  auto d = place(order(1));
  cb.deliver(trade(d, "0.001", "64000.00", "0.001", "0.0015"));
  ack(d);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"})) << "one Ack, before the Fill";
  EXPECT_EQ(somr.all<som::Ack>()[0]->xordid, order_id64(*parse_uuid(ORDER_ID)));
}

TEST_F(CbxOMTest, FillRacingCancel)
{
  start();
  auto d = place(order(1));
  ack(d);
  cancel(1);
  ASSERT_TRUE(wait_until([&] { return cb.sent("F").size() == 1; }));
  cb.deliver(trade(d, "0.001", "64000.00", "0.001", "0.0015"));
  auto c = er(d, '4', '4', "0.001", "0");
  c.set(ClOrdID, cb.sent("F")[0].get(ClOrdID));
  cb.deliver(c);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
}

TEST_F(CbxOMTest, FillAfterTheCancelAckStillCounts)
{
  start();
  auto d = place(order(1));
  ack(d);
  cb.deliver(er(d, '4', '4', "0", "0"));
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  cb.deliver(trade(d, "0.001", "64000.00", "0.001", "0"));
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr) << "it happened: the position changed";
  EXPECT_EQ(f->sz, 10);
}

TEST_F(CbxOMTest, RestatementCutsTheQuantityAndIsNotAFill)
{
  start();
  auto d = place(order(1));
  ack(d);
  auto r = er(d, 'D', '0', "0", "0.0010");
  r.set(ExecRestatementReason, "5");
  cb.deliver(r);
  ASSERT_TRUE(wait_until([&] { return om->book().find_oid(1)->qty == 10; })) << "25 units cut to 10";
  EXPECT_EQ(om->book().find_oid(1)->leaves, 10);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
  EXPECT_TRUE(somr.all<som::Fill>().empty());
  EXPECT_TRUE(somr.all<som::CancAck>().empty()) << "0.0010 is still working";
  // a fill of the restated quantity finishes the order
  cb.deliver(trade(d, "0.0010", "64000.00", "0.0010", "0"));
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 10);
  EXPECT_EQ(f->still_to_be_filled, 0);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
  // restated to nothing: the order is gone
  auto d2 = place(order(2));
  ack(d2);
  auto r2 = er(d2, 'D', '0', "0", "0");
  r2.set(ExecRestatementReason, "5");
  cb.deliver(r2);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.all<som::CancAck>()[0]->id, 2u);
}

// The venue says the order is done (OrdStatus 2) on a fill short of the OM's
// quantity (a restatement the OM never saw): the fill finishes the order.
TEST_F(CbxOMTest, TradeWithOrdStatusFilledFinishesTheOrder)
{
  start();
  auto d = place(order(1));
  ack(d);
  cb.deliver(trade(d, "0.0010", "64000.00", "0.0010", "0")); // OrdStatus 2 at 10 of 25 units
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 10);
  EXPECT_EQ(f->still_to_be_filled, 0);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
  EXPECT_EQ(om->book().find_oid(1)->qty, 10);
  cancel(1);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FILLEDALREADY);
}

// Fills come in base increments; the SOM gets whole units. What is below a
// unit is carried to the next fill of the same order, never rounded.
TEST_F(CbxOMTest, FillsBelowAUnitAreCarriedToTheNextFill)
{
  start();
  auto d = place(order(1));
  ack(d);
  cb.deliver(trade(d, "0.00005", "64000.00", "0.00005", "0.00245")); // half a unit
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty()) << "below one unit of 0.0001: carried";
  cb.deliver(trade(d, "0.00005", "64000.00", "0.0001", "0.0024")); // the other half
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 1);
  EXPECT_EQ(f->still_to_be_filled, 24);
  cb.deliver(trade(d, "0.00015", "64000.00", "0.00025", "0.00225")); // 1.5 units: 1 now, 0.5 carried
  f = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 1);
  cb.deliver(trade(d, "0.00005", "64000.00", "0.0003", "0.0022"));
  f = somr.wait_for<som::Fill>(2);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 1);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 3u) << "0.0003 filled: 3 units, as the venue has it";
  EXPECT_EQ(om->book().find_oid(1)->cum, 3);
  EXPECT_EQ(om->duplicates_skipped(), 0u) << "CumQty in increments: none of these was a replay";
}

// ---- busy, reconnect, replay ---------------------------------------------------------------

TEST_F(CbxOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  cb.busy();
  ASSERT_TRUE(wait_until([&] { return om->busy(); }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  cb.available();
  ASSERT_TRUE(wait_until([&] { return !om->busy(); }));
  place(order(2));
}

TEST_F(CbxOMTest, ReconnectResumesAndDeliversAMissedFillOnce)
{
  start();
  auto d = place(order(1));
  ack(d);
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 2 && om->ready(); })) << "reconnected";
  ASSERT_TRUE(wait_until([&] { return cb.sent("H").size() == 1; })) << "status asked for the live order";
  auto h = cb.sent("H")[0];
  EXPECT_EQ(h.get(ClOrdID), d.get(ClOrdID));
  EXPECT_EQ(h.get(OrderID), ORDER_ID);
  EXPECT_EQ(h.get(Symbol), "BTC-USD");
  // filled 0.001 while we were away
  auto st = er(d, 'I', '1', "0.001", "0.0015");
  st.set(AvgPx, "63999.50");
  cb.deliver(st);
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 10);
  EXPECT_EQ(f->pxi, 6399950);
  // the venue then resends the trade report (PossDupFlag): not applied again
  auto t = trade(d, "0.001", "63999.50", "0.001", "0.0015");
  t.set(PossDupFlag, 'Y');
  cb.deliver(t);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(1)->cum, 10);
}

TEST_F(CbxOMTest, ReplayedReportsAreNotAppliedTwice)
{
  start();
  auto d = place(order(1));
  ack(d);
  auto t = trade(d, "0.001", "64000.00", "0.001", "0.0015", "11111111-1111-4111-8111-111111111111");
  cb.deliver(t);
  cb.deliver(t); // same ExecID
  auto t2 = trade(d, "0.001", "64000.00", "0.001", "0.0015", "22222222-2222-4222-8222-222222222222");
  cb.deliver(t2); // a new ExecID, but CumQty says it is the same fill
  cb.deliver(er(d, '0', '0')); // the New again
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->duplicates_skipped(), 2u);
}

TEST_F(CbxOMTest, CanceledWhileDisconnectedIsSettledByStatus)
{
  start();
  auto d = place(order(1));
  ack(d);
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return cb.sent("H").size() == 1; }));
  cb.deliver(er(d, 'I', '4', "0", "0")); // cancel-on-disconnect took it
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(CbxOMTest, OrderUnknownToTheVenueAfterReconnectIsRejected)
{
  start();
  auto d = place(order(1)); // never acknowledged
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return cb.sent("H").size() == 1; }));
  FixMsg r("8");
  r.set(ClOrdID, d.get(ClOrdID)).set(OrderID, "00000000-0000-0000-0000-000000000000").set(ExecID, "x");
  r.set(OrdStatus, '8').set(ExecType, 'I').set(Symbol, "BTC-USD").set(OrdRejReason, "5");
  cb.deliver(r);
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
}

TEST_F(CbxOMTest, OrderRejectedInTheStatusAnswerIsAReject)
{
  start();
  auto d = place(order(1)); // never acknowledged
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return cb.sent("H").size() == 1; }));
  FixMsg r("8");
  r.set(ClOrdID, d.get(ClOrdID)).set(OrderID, ORDER_ID).set(ExecID, "x");
  r.set(OrdStatus, '8').set(ExecType, 'I').set(Symbol, "BTC-USD").set(OrdRejReason, "1").set(Text, "Unknown Symbol");
  cb.deliver(r);
  auto j = somr.wait_for<som::Reject>();
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->id, 1u);
  EXPECT_EQ(j->reason, som::Reject::EXCHLIMIT);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Rejected);
}

// Finished orders are forgotten at the next logon, with their ids and
// ExecIDs; live ones are kept and asked about.
TEST_F(CbxOMTest, LogonForgetsFinishedOrders)
{
  start();
  auto d1 = place(order(1));
  ack(d1);
  cb.deliver(trade(d1, "0.0025", "64000.00", "0.0025", "0"));
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  auto d2 = place(order(2));
  ack(d2);
  auto d3 = place(order(3));
  auto j = er(d3, '8', '8');
  j.set(OrdRejReason, "1");
  cb.deliver(j);
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
  EXPECT_EQ(om->book().size(), 3u);
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return cb.sessions().size() == 2 && om->ready(); }));
  ASSERT_TRUE(wait_until([&] { return cb.sent("H").size() == 1; }));
  EXPECT_EQ(cb.sent("H")[0].get(ClOrdID), d2.get(ClOrdID)) << "only the live order is asked about";
  EXPECT_EQ(om->book().size(), 1u);
  EXPECT_EQ(om->book().find_oid(1), nullptr);
  EXPECT_EQ(om->book().find_oid(3), nullptr);
  EXPECT_NE(om->book().find_oid(2), nullptr);
  EXPECT_TRUE(om->clordid(1).empty());
  EXPECT_EQ(om->clordid(2), d2.get(ClOrdID));
  EXPECT_GE(om->purges(), 2u) << "at each logon";
  // a late report for the forgotten order is unknown: nothing reaches the SOM
  cb.deliver(trade(d1, "0.0001", "64000.00", "0.0026", "0"));
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
}

TEST_F(CbxOMTest, OrdersAndCancelsWhileDisconnectedAreRefused)
{
  auto c = cfg();
  c.reconnect_ms = 5000;
  start(true, c);
  auto d = place(order(1));
  ack(d);
  cb.disconnect();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto r = refused(order(2));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
  cancel(1);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Working);
}

TEST_F(CbxOMTest, UnknownReportsAreLoggedAndIgnored)
{
  start();
  FixMsg r("8");
  r.set(ClOrdID, "00000000-0000-4000-8000-00000000abcd").set(OrderID, ORDER_ID).set(ExecID, "z");
  r.set(ExecType, 'F').set(OrdStatus, '2').set(LastQty, "1").set(LastPx, "1.00").set(Symbol, "BTC-USD");
  cb.deliver(r); // an order placed outside this OM
  FixMsg q("X");
  cb.deliver(q);
  settle();
  EXPECT_EQ(somr.replies().size(), 0u);
}

TEST_F(CbxOMTest, ClientIdsAreUniqueAndARestartDoesNotReuseThem)
{
  start(true, cfg(), frame::som::ClientIdGen(5000), 0); // seeded as in production
  std::set<std::string> ids;
  for (int i = 1; i <= 20; ++i)
    ids.insert(place(order(i)).get(ClOrdID));
  EXPECT_EQ(ids.size(), 20u);
  auto first = om->book().find_oid(1)->cid, last = om->book().find_oid(20)->cid;
  EXPECT_LT(first, last);
  stop();
  start(true, cfg(), frame::som::ClientIdGen(6000), 0); // a restart, a millisecond later
  auto again = place(order(1));
  EXPECT_FALSE(ids.count(again.get(ClOrdID)));
  EXPECT_GT(om->book().find_oid(1)->cid, last);
}

TEST_F(CbxOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto d = place(order(1));
  std::thread t([&] {
    for (int i = 0; i < 200; ++i)
      cb.deliver(er(d, '0', '0'));
  });
  stop();
  t.join();
  SUCCEED();
}

// ---- Logon signature ---------------------------------------------------------------------------

// Vectors from Python's hmac/hashlib/base64 with the prehash of the
// documentation's QuickFIX example (order-entry-messages5, Logon).
TEST(CbxLogon, SignatureAndFields)
{
  FixMsg logon("A");
  ASSERT_TRUE(sign_logon(logon, "20260101-00:00:00.000", "1", "testkey", "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                         "testpass", 'Y', false));
  EXPECT_EQ(logon.get(RawData), "/Nvt08TkezsTKgS6G9J5XmTfKPJ6S6dyxNI/L53vTVU=");
  EXPECT_EQ(logon.get(RawDataLength), "44");
  EXPECT_EQ(logon.get(Username), "testkey");
  EXPECT_EQ(logon.get(Password), "testpass");
  EXPECT_EQ(logon.get(DefaultApplVerID), "9");
  EXPECT_EQ(logon.get(CancelOrdersOnDisconnect), "Y");
  EXPECT_FALSE(logon.has(DropCopyFlag));
  FixMsg dc("A");
  ASSERT_TRUE(sign_logon(dc, "20260101-00:00:00.000", "1", "testkey", "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                         "testpass", 'Y', true));
  EXPECT_EQ(dc.get(DropCopyFlag), "Y");
  EXPECT_FALSE(dc.has(CancelOrdersOnDisconnect));
  FixMsg bad("A");
  EXPECT_FALSE(sign_logon(bad, "x", "1", "k", "not base64!", "p", 'Y', false));
}

TEST(CbxLogon, RestAndWebSocketSignatures)
{
  EXPECT_EQ(*sign("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                  rest_prehash("1700000000.123", "GET", "/users/self/verify", "")),
            "CczvlgpGs/55OGojpPfB2R2JKWS2U8yUxUQ0vLEneVk=");
  EXPECT_EQ(*sign("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                  rest_prehash("1700000000", "POST", "/orders",
                               R"({"price":"1.0","size":"1.0","side":"buy","product_id":"BTC-USD"})")),
            "Wf4NVY76bNJwCb/mF2Mk0qoCqhVR7XdaeKMVr4B5NF4=");
}

// ---- the sandbox guard -------------------------------------------------------------------------

TEST(CbxGuard, SandboxNeverReachesProduction)
{
  std::string err;
  EXPECT_TRUE(host_allowed(Environment::Sandbox, "fix-ord.sandbox.exchange.coinbase.com", nullptr, err));
  EXPECT_FALSE(host_allowed(Environment::Sandbox, "fix-ord.exchange.coinbase.com", nullptr, err));
  EXPECT_NE(err.find("refusing"), std::string::npos);
  EXPECT_FALSE(host_allowed(Environment::Sandbox, "evil.sandbox.exchange.coinbase.com.example.org", nullptr, err));
  Credentials prod;
  prod.environment = Environment::Production;
  EXPECT_FALSE(host_allowed(Environment::Sandbox, "fix-ord.sandbox.exchange.coinbase.com", &prod, err))
      << "production keys in a sandbox configuration";
  EXPECT_FALSE(host_allowed(Environment::Production, "fix-ord.sandbox.exchange.coinbase.com", nullptr, err));
  EXPECT_EQ(endpoints(Environment::Sandbox).fix_ord_host, "fix-ord.sandbox.exchange.coinbase.com");
  EXPECT_EQ(endpoints(Environment::Sandbox).fix_dc_port, 6122);
}

TEST(CbxGuard, ExpandHome)
{
  const char *h = std::getenv("HOME");
  ASSERT_NE(h, nullptr);
  EXPECT_EQ(expand_home("~"), std::string(h));
  EXPECT_EQ(expand_home("~/.kaspar/keys/x.json"), std::string(h) + "/.kaspar/keys/x.json");
  EXPECT_EQ(expand_home("/var/tmp/x"), "/var/tmp/x");
  EXPECT_EQ(expand_home("~other/x"), "~other/x") << "another user's home is not guessed";
  EXPECT_EQ(expand_home(""), "");
}

TEST(CbxGuard, CredentialsFile)
{
  const char *dir = std::getenv("TMPDIR");
  std::string p = std::string(dir ? dir : "/tmp") + "/cbx_test_keys.json";
  {
    std::ofstream f(p);
    f << R"({"environment":"sandbox","key":"k","secret":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","passphrase":"p"})";
  }
  Credentials c;
  std::string err;
  chmod(p.c_str(), 0644);
  EXPECT_FALSE(load_credentials(p, c, err)) << "readable by others";
  chmod(p.c_str(), 0600);
  EXPECT_TRUE(load_credentials(p, c, err)) << err;
  EXPECT_EQ(c.environment, Environment::Sandbox);
  EXPECT_FALSE(load_credentials(p + ".missing", c, err));
  std::remove(p.c_str());
}

// ---- FixMsg ----------------------------------------------------------------------------------------

TEST(CbxFixMsg, WireRoundTripWithLengthAndChecksum)
{
  FixMsg m("D");
  m.set(ClOrdID, "1985ca2d-61ef-49f1-bfce-6c39d8462914").set(Symbol, "BTC-USD").set(Side, '1');
  auto w = m.to_wire();
  auto p = FixMsg::parse(w);
  ASSERT_TRUE(p);
  EXPECT_EQ(p->get(BeginString), "FIXT.1.1");
  EXPECT_EQ(p->get(ClOrdID), "1985ca2d-61ef-49f1-bfce-6c39d8462914");
  std::string bad = w;
  bad[bad.size() - 3] = bad[bad.size() - 3] == '0' ? '1' : '0';
  EXPECT_FALSE(FixMsg::parse(bad)) << "checksum";
  EXPECT_FALSE(FixMsg::parse("35=D\x01" "abc\x01"));
  auto piped = FixMsg::parse("35=8|150=F|32=0.001|", '|');
  ASSERT_TRUE(piped);
  EXPECT_EQ(piped->get_char(ExecType), 'F');
}
