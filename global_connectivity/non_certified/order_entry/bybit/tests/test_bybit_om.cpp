/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitOM against FakeBybit, on its own actor thread under a kaspar Manager.
// "SOM" is a recorder; the test thread plays Bybit. The cases are the list in
// .claude/skills/build-feed-handler/order_entry.md.

#include <gtest/gtest.h>

#include <memory>
#include <set>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeBybit.hpp"
#include "TestUtil.hpp"
#include "bybit_oe/act/BybitOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace bybit::oe;
using namespace bybit::oe::test;
using frame::som::State;

namespace
{
  class BybitOMTest : public ::testing::Test
  {
  protected:
    FakeBybit bybit;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    BybitOM *om = nullptr;
    const frame::ref::Asset *btc = nullptr;
    const frame::ref::Asset *spot = nullptr;

    void SetUp() override
    {
      btc = test_asset("BTCUSDT_L");
      spot = test_asset("BTCUSDT_S");
    }

    BybitOMConfig cfg()
    {
      BybitOMConfig c;
      c.session.credentials = {"key", "secret"};
      c.reconnect_ms = 30;
      c.instruments[btc->id] = {"linear", "BTCUSDT", 0.1, 0.001};
      c.instruments[spot->id] = {"spot", "BTCUSDT", 0.1, 0.000001};
      c.max_orders_per_sec = 0;
      return c;
    }

    void start(bool wait_ready = true, BybitOMConfig c = {}, uint64_t idbase = 5000)
    {
      if (c.instruments.empty())
        c = cfg();
      om = new BybitOM(c, &somr, bybit.factory(), en::x::BYBITF, frame::som::ClientIdGen(idbase), 20261118);
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

    Order order(int oid, int sz = 5, int px = 630000, en::bs side = en::bs::BUY, const frame::ref::Asset *a = nullptr)
    {
      Order o("CR", en::x::BYBITF, (a ? a : btc)->id, sz, px, side, 4);
      o.oid = oid;
      return o;
    }

    // Send as the SOM does; wait until it reaches Bybit; return its orderLinkId.
    std::string place(const Order &o)
    {
      auto before = bybit.calls(RequestKind::Place).size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return bybit.calls(RequestKind::Place).size() > before; }))
          << "order never reached Bybit";
      auto c = bybit.calls(RequestKind::Place);
      return c.empty() ? "" : c.back().link;
    }
    PlaceRequest sent() { return bybit.calls(RequestKind::Place).back().place; }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }
    void cancel(int oid) { om->send(new som::Cancel(oid), &somr); }
    void settle() { std::this_thread::sleep_for(100ms); }
    State state(int oid)
    {
      State s = State::PendingNew;
      wait_until([&] { return om->book().find_oid(oid) != nullptr; });
      settle();
      if (auto e = om->book().find_oid(oid))
        s = e->state;
      return s;
    }
  };
}

// ---- logon and registration -----------------------------------------------------------------

TEST_F(BybitOMTest, LogsInAndRegistersWithTheSom)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::BYBITF);
  EXPECT_EQ(reg->manager, om);
  EXPECT_EQ(bybit.sessions, 1);
}

TEST_F(BybitOMTest, BadConfigGivesNullNotCrash)
{
  auto c = cfg();
  c.session.rest_host = "api.bybit.com"; // production while testnet = true
  EXPECT_EQ(create_BybitOM(c, &somr, en::x::BYBITF, bybit.factory()), nullptr);
  c = cfg();
  c.session.ws_host = "stream.bybit.com";
  EXPECT_EQ(create_BybitOM(c, &somr, en::x::BYBITF, bybit.factory()), nullptr);
  c = cfg();
  c.instruments.clear();
  EXPECT_EQ(create_BybitOM(c, &somr, en::x::BYBITF, bybit.factory()), nullptr);
  c = cfg();
  c.session.credentials = {};
  EXPECT_EQ(create_BybitOM(c, &somr, en::x::BYBITF, bybit.factory()), nullptr);
  EXPECT_EQ(create_BybitOM("/nonexistent/bybit.ini", &somr, en::x::BYBITF), nullptr);
  c = cfg();
  auto a = create_BybitOM(c, &somr, en::x::BYBITF, bybit.factory());
  EXPECT_NE(a, nullptr);
  delete a;
}

TEST_F(BybitOMTest, OrdersBeforeLoginAndWhileDisconnectedAreRejectedLocally)
{
  bybit.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return bybit.sessions == 1; }));
  settle();
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  bybit.logged_in();
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  bybit.disconnect();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto r2 = refused(order(2), 1);
  ASSERT_NE(r2, nullptr);
  EXPECT_TRUE(r2->reason == som::Reject::DISCONNECTED || r2->reason == som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(bybit.calls(RequestKind::Place).empty());
}

// ---- what is sent ------------------------------------------------------------------------------

TEST_F(BybitOMTest, FieldsOfEachOrderTypeAsSent)
{
  start();
  auto link = place(order(1));
  auto p = sent();
  EXPECT_EQ(link, "k20261118-" + std::to_string(frame::som::ClientIdGen(5000).next()));
  EXPECT_EQ(p.category, "linear");
  EXPECT_EQ(p.symbol, "BTCUSDT");
  EXPECT_EQ(p.side, "Buy");
  EXPECT_EQ(p.order_type, "Limit");
  EXPECT_EQ(p.qty, "0.005");
  EXPECT_EQ(p.price, "63000.0");
  EXPECT_EQ(p.time_in_force, "GTC"); // DAY sent as GTC

  auto o = order(2, 1, 630001, en::bs::SEL);
  o.tif = Order::TimeInForce::IOC;
  place(o);
  EXPECT_EQ(sent().side, "Sell");
  EXPECT_EQ(sent().time_in_force, "IOC");
  EXPECT_EQ(sent().price, "63000.1");

  o = order(3);
  o.tif = Order::TimeInForce::FOK;
  place(o);
  EXPECT_EQ(sent().time_in_force, "FOK");

  o = order(4);
  o.tif = Order::TimeInForce::GTC;
  o.strategy = "POSTONLY";
  place(o);
  EXPECT_EQ(sent().time_in_force, "PostOnly");

  o = order(5, 3, 0, en::bs::SEL);
  o.ord_type = Order::OrdType::MARKET;
  place(o);
  EXPECT_EQ(sent().order_type, "Market");
  EXPECT_EQ(sent().price, "");
  EXPECT_EQ(sent().qty, "0.003");

  o = order(6, 1234, 631234, en::bs::BUY, spot);
  place(o);
  EXPECT_EQ(sent().category, "spot");
  EXPECT_EQ(sent().qty, "0.001234");
  EXPECT_EQ(sent().price, "63123.4");
}

TEST_F(BybitOMTest, UnsupportedCombinationsAreRefusedLocally)
{
  auto c = cfg();
  c.max_order_qty = 1000;
  c.max_order_notional = 100000;
  start(true, c);
  size_t n = 0;
  auto expect = [&](Order o, int reason) {
    auto r = refused(o, n++);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->reason, reason);
  };
  auto o = order(1);
  o.ord_type = Order::OrdType::STOP;
  expect(o, som::Reject::UNKNOWN);
  o = order(2, 1, 630000, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  expect(o, som::Reject::UNKNOWN);
  o = order(3);
  o.tif = Order::TimeInForce::AT_OPEN;
  expect(o, som::Reject::UNKNOWN);
  o = order(4);
  o.strategy = "VWAP";
  expect(o, som::Reject::UNKNOWN);
  o = order(5);
  o.sz = 1.5;
  expect(o, som::Reject::OVERLIMIT);
  o = order(6);
  o.sym = 999999; // not configured
  expect(o, som::Reject::UNKNOWN);
  expect(order(7, 2000), som::Reject::OVERLIMIT);        // over max_order_qty
  expect(order(8, 900, 1200000), som::Reject::OVERLIMIT); // 0.9 BTC at 120000 > 100000 notional
  o = order(9, 5, 630000, en::bs::BUY, spot);
  o.ord_type = Order::OrdType::MARKET;
  expect(o, som::Reject::OVERLIMIT); // market order with a notional limit set
  o = order(10);
  o.oid_to_cancel = 1;
  expect(o, som::Reject::UNKNOWN);
  o = order(11);
  o.tif = Order::TimeInForce::IOC;
  o.strategy = "POSTONLY";
  expect(o, som::Reject::UNKNOWN);
  EXPECT_TRUE(bybit.calls(RequestKind::Place).empty());
}

TEST_F(BybitOMTest, SpotMarketBuyIsRefused)
{
  start();
  auto o = order(1, 5, 0, en::bs::BUY, spot);
  o.ord_type = Order::OrdType::MARKET;
  auto r = refused(o);
  ASSERT_NE(r, nullptr);
  EXPECT_TRUE(bybit.calls(RequestKind::Place).empty());
}

TEST_F(BybitOMTest, DayOrdersRefusedWhenDayAsGtcIsOff)
{
  auto c = cfg();
  c.day_as_gtc = false;
  start(true, c);
  ASSERT_NE(refused(order(1)), nullptr);
}

// ---- reports -----------------------------------------------------------------------------------

TEST_F(BybitOMTest, AckCarriesTheBybitOrderId)
{
  start();
  auto link = place(order(1));
  bybit.response(RequestKind::Place, link, 0, "OK");
  bybit.order(order_update(link, "New"));
  auto a = somr.wait_for<som::Ack>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->id, 1u);
  EXPECT_EQ(a->xordid, 1321003749386327552ull);
  bybit.order(order_update(link, "New")); // a repeat: one Ack only
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
}

TEST_F(BybitOMTest, UuidOrderIdIsHashed)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New", "0", "c6f055d9-7f21-4079-913d-e6523a9cfffa"));
  auto a = somr.wait_for<som::Ack>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->xordid, BybitOM::xordid("c6f055d9-7f21-4079-913d-e6523a9cfffa"));
}

TEST_F(BybitOMTest, PartialThenFullFill)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  bybit.execution(exec(link, "e1", "0.002", "63000.0"));
  bybit.order(order_update(link, "PartiallyFilled", "0.002"));
  bybit.execution(exec(link, "e2", "0.003", "62999.9"));
  bybit.order(order_update(link, "Filled", "0.005"));
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  auto f1 = somr.all<som::Fill>()[0];
  EXPECT_EQ(f1->sz, 2);
  EXPECT_EQ(f1->still_to_be_filled, 3);
  EXPECT_EQ(f1->pxi, 630000);
  EXPECT_EQ(f1->tim, 1795027104327000000ull);
  EXPECT_EQ(f2->sz, 3);
  EXPECT_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 629999);
  EXPECT_EQ(f2->xordid, 1321003749386327552ull);
  EXPECT_EQ(state(1), State::Filled);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

TEST_F(BybitOMTest, RejectFromTheStreamAndFromRest)
{
  start();
  auto l1 = place(order(1));
  bybit.order(order_update(l1, "Rejected", "0", "x", "EC_InvalidSymbolStatus"));
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->id, 1u);
  auto l2 = place(order(2));
  bybit.response(RequestKind::Place, l2, 110007, "ab not enough for new order");
  auto r2 = somr.wait_for<som::Reject>(1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->id, 2u);
  EXPECT_EQ(state(2), State::Rejected);
}

TEST_F(BybitOMTest, PostOnlyThatWouldCrossIsAReject)
{
  start();
  auto o = order(1);
  o.tif = Order::TimeInForce::GTC;
  o.strategy = "POSTONLY";
  auto link = place(o);
  bybit.order(order_update(link, "Cancelled", "0", "x", "EC_PostOnlyWillTakeLiquidity"));
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  settle();
  EXPECT_TRUE(somr.all<som::CancAck>().empty());
}

TEST_F(BybitOMTest, RateLimitAnswerThrottlesUntilAvailable)
{
  start();
  auto link = place(order(1));
  bybit.response(RequestKind::Place, link, 10006, "Too many visits!");
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  ASSERT_TRUE(wait_until([&] { return om->busy(); }));
  auto r2 = refused(order(2), 1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::THROTTLE);
  bybit.available();
  ASSERT_TRUE(wait_until([&] { return !om->busy(); }));
  place(order(3));
}

TEST_F(BybitOMTest, BusyQueueRejectsWithThrottle)
{
  start();
  bybit.refuse_calls = true;
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  EXPECT_TRUE(om->busy());
}

TEST_F(BybitOMTest, NoAnswerToACreateQueriesAndNeverResends)
{
  start();
  auto link = place(order(1));
  bybit.response(RequestKind::Place, link, -1, "transport: timeout");
  ASSERT_TRUE(wait_until([&] { return bybit.calls(RequestKind::QueryOrder).size() == 1; }));
  settle();
  EXPECT_EQ(bybit.calls(RequestKind::Place).size(), 1u);
  EXPECT_TRUE(somr.all<som::Reject>().empty());
  bybit.order(order_update(link, "New"));
  EXPECT_NE(somr.wait_for<som::Ack>(), nullptr);
}

// ---- cancels -----------------------------------------------------------------------------------

TEST_F(BybitOMTest, CancelRoundTripByOrderLinkId)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cancel(1);
  ASSERT_TRUE(wait_until([&] { return bybit.calls(RequestKind::Cancel).size() == 1; }));
  EXPECT_EQ(bybit.calls(RequestKind::Cancel)[0].link, link);
  bybit.response(RequestKind::Cancel, link, 0, "OK");
  bybit.order(order_update(link, "Cancelled", "0", "1321003749386327552", "EC_PerCancelRequest", "CancelByUser"));
  auto c = somr.wait_for<som::CancAck>();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->id, 1u);
  EXPECT_EQ(state(1), State::Canceled);
}

TEST_F(BybitOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  cancel(1);
  ASSERT_TRUE(wait_until([&] { return bybit.calls(RequestKind::Cancel).size() == 1; }));
  bybit.response(RequestKind::Cancel, link, 110001, "order not exists or too late to cancel");
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_EQ(state(1), State::Working);
  bybit.execution(exec(link, "e1", "0.005"));
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(state(1), State::Filled);
}

TEST_F(BybitOMTest, CancelOfUnknownOrFilledOrder)
{
  start();
  cancel(77);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::NOTFOUND);
  auto link = place(order(1));
  bybit.execution(exec(link, "e1", "0.005"));
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  cancel(1);
  auto cr2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(cr2, nullptr);
  EXPECT_EQ(cr2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(bybit.calls(RequestKind::Cancel).empty());
}

TEST_F(BybitOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  bybit.order(order_update(link, "Cancelled", "0", "1321003749386327552", "EC_NoError", "CancelByAdmin"));
  auto c = somr.wait_for<som::CancAck>();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(state(1), State::Canceled);
}

// ---- races ---------------------------------------------------------------------------------------

TEST_F(BybitOMTest, FillBeforeAck)
{
  start();
  auto link = place(order(1));
  bybit.execution(exec(link, "e1", "0.005"));
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  bybit.order(order_update(link, "Filled", "0.005"));
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
}

// The cancel's update counts a fill whose execution has not arrived: the
// CancAck waits for it, and the executions are fetched over REST.
TEST_F(BybitOMTest, FillRacingACancel)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  cancel(1);
  ASSERT_TRUE(wait_until([&] { return bybit.calls(RequestKind::Cancel).size() == 1; }));
  bybit.order(order_update(link, "Cancelled", "0.002", "1321003749386327552", "EC_PerCancelRequest", "CancelByUser"));
  ASSERT_TRUE(wait_until([&] { return bybit.calls(RequestKind::QueryExecutions).size() == 1; }));
  settle();
  EXPECT_TRUE(somr.all<som::CancAck>().empty());
  bybit.execution(exec(link, "e1", "0.002"));
  auto c = somr.wait_for<som::CancAck>();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->fillsz, 2);
  bybit.execution(exec(link, "e1", "0.002")); // the REST copy of the same execution
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
}

TEST_F(BybitOMTest, FillAfterTheCancelAckStillCounts)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  bybit.order(order_update(link, "Cancelled", "0", "1321003749386327552", "EC_NoError", "CancelByUser"));
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  bybit.execution(exec(link, "e9", "0.001"));
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 1);
}

// Bybit has no trade bust: BustTrade is a takeover liquidation. These are
// position changes the SOM did not order, logged as errors, never fills.
TEST_F(BybitOMTest, LiquidationAdlAndCashExecutionTypesAreNotFills)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  for (auto t : {"BustTrade", "AdlTrade", "Funding", "Delivery", "Settle"})
    bybit.execution(exec(link, std::string("x-") + t, "0.001", "63000", t));
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
  EXPECT_EQ(state(1), State::Working);
}

TEST_F(BybitOMTest, ReconnectReconcilesAndDeliversAMissedFillOnce)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  bybit.disconnect();
  ASSERT_TRUE(wait_until([&] { return bybit.sessions == 2 && om->ready(); }));
  ASSERT_TRUE(wait_until([&] {
    return bybit.calls(RequestKind::QueryOrder).size() == 1 && bybit.calls(RequestKind::QueryExecutions).size() == 1;
  }));
  EXPECT_EQ(bybit.calls(RequestKind::QueryExecutions)[0].link, link);
  // The fill that happened while disconnected, from REST, then again on the stream.
  bybit.execution(exec(link, "missed-1", "0.005"));
  bybit.order(order_update(link, "Filled", "0.005"));
  bybit.execution(exec(link, "missed-1", "0.005"));
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(state(1), State::Filled);
  // Done orders are not reconciled on the next login.
  bybit.disconnect();
  ASSERT_TRUE(wait_until([&] { return bybit.sessions == 3 && om->ready(); }));
  settle();
  EXPECT_EQ(bybit.calls(RequestKind::QueryOrder).size(), 1u);
}

TEST_F(BybitOMTest, ReportsForOrdersPlacedOutsideTheOMAreIgnored)
{
  start();
  bybit.order(order_update("manual-from-the-web", "New"));
  bybit.execution(exec("manual-from-the-web", "m1", "0.001"));
  bybit.execution(exec("", "m2", "0.001"));
  settle();
  EXPECT_TRUE(somr.replies().empty());
}

TEST_F(BybitOMTest, ClientIdsAreUniqueAndIncreasingAndARestartDoesNotReuseThem)
{
  start(true, cfg(), 0);
  std::set<std::string> links;
  std::string prev;
  for (int i = 1; i <= 5; ++i)
  {
    auto l = place(order(i));
    EXPECT_TRUE(links.insert(l).second);
    frame::som::ClientId a = 0, b = 0;
    if (!prev.empty())
    {
      ASSERT_TRUE(BybitOM::parse_link(prev, a));
      ASSERT_TRUE(BybitOM::parse_link(l, b));
      EXPECT_LT(a, b);
    }
    prev = l;
  }
  stop();
  // A restarted process takes its ids from a later millisecond.
  auto g1 = frame::som::ClientIdGen::from_clock();
  auto g2 = frame::som::ClientIdGen::from_clock();
  EXPECT_NE(BybitOM::link_id(20261118, g1.next()), BybitOM::link_id(20261118, g2.next()));
}

TEST_F(BybitOMTest, ShutdownWithAReportInFlightCompletes)
{
  start();
  auto link = place(order(1));
  bybit.order(order_update(link, "New"));
  bybit.execution(exec(link, "e1", "0.001"));
  stop();
  SUCCEED();
}
