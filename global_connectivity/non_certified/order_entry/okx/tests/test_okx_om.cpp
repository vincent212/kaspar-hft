/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxOM against FakeOkx, with OkxOM on its own actor thread under a kaspar
// Manager. "SOM" is a recorder. The test thread plays the session's thread.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeOkx.hpp"
#include "TestUtil.hpp"
#include "okx_oe/if/OkxOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace okx::oe;
using namespace okx::oe::test;
using frame::som::State;

namespace
{
  class OkxOMTest : public ::testing::Test
  {
  protected:
    FakeOkxServer okx;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    OkxOM *om = nullptr;
    Assets a;

    void SetUp() override { a = assets(); }

    void start(bool wait_ready = true, OkxOMConfig c = demo_config(),
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000))
    {
      om = new OkxOM(c, &somr, okx.factory(), ids);
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

    // 10 units of 0.0001 BTC at 63123.4 (ticks of 0.1)
    Order order(int oid, int units = 10, int px = 631234, en::bs side = en::bs::BUY, bool swap = false)
    {
      Order o("CR", swap ? en::x::OKXF : en::x::OKX, uint(swap ? a.swap : a.spot), units, px, side, 4);
      o.oid = oid;
      return o;
    }

    FakeOkxServer::Req place(const Order &o)
    {
      auto before = okx.placed().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return okx.placed().size() > before; })) << "order never reached OKX";
      auto p = okx.placed();
      return p.empty() ? FakeOkxServer::Req{} : p.back();
    }

    std::string cl(const FakeOkxServer::Req &r) { return r.arg.value("clOrdId", ""); }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }

    FakeOkxServer::Req cancel(int oid)
    {
      auto before = okx.cancels().size();
      om->send(new som::Cancel(oid), &somr);
      EXPECT_TRUE(wait_until([&] { return okx.cancels().size() > before; }));
      auto c = okx.cancels();
      return c.empty() ? FakeOkxServer::Req{} : c.back();
    }

    void settle() { std::this_thread::sleep_for(150ms); }
  };
}

// ---- logon and registration ------------------------------------------------------------------

TEST_F(OkxOMTest, LogsInAndRegistersForSpotAndSwapVenues)
{
  start();
  ASSERT_NE(somr.wait_for<som::SetExchManager>(1), nullptr);
  auto regs = somr.all<som::SetExchManager>();
  ASSERT_EQ(regs.size(), 2u);
  EXPECT_EQ(regs[0]->venue, en::x::OKX);
  EXPECT_EQ(regs[1]->venue, en::x::OKXF);
  EXPECT_EQ(okx.sessions(), 1);
}

TEST_F(OkxOMTest, BadConfigOrRefusedEnvironmentGivesNullNotCrash)
{
  assets();
  auto c = demo_config();
  c.instruments[0].asset = "NO_SUCH_ASSET";
  EXPECT_EQ(create_OkxOM(c, &somr, okx.factory()), nullptr);
  c = demo_config();
  c.ep.ws_host = "ws.okx.com"; // production host while demo
  EXPECT_EQ(create_OkxOM(c, &somr, okx.factory()), nullptr);
  c = demo_config();
  c.ep.demo = false;
  c.ep.ws_host = "ws.okx.com"; // production without allow_production
  EXPECT_EQ(create_OkxOM(c, &somr, okx.factory()), nullptr);
  EXPECT_EQ(create_OkxOM("/nonexistent.ini", &somr), nullptr);
  c = demo_config();
  auto *ok = create_OkxOM(c, &somr, okx.factory());
  EXPECT_NE(ok, nullptr);
  delete ok;
}

TEST_F(OkxOMTest, OrderBeforeLoginIsRejectedLocally)
{
  okx.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return okx.sessions() == 1; }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(okx.placed().empty());
}

TEST_F(OkxOMTest, LoginRejectedStaysDown)
{
  okx.reject_login = true;
  start(false);
  ASSERT_TRUE(wait_until([&] { return okx.sessions() == 1; }));
  settle();
  EXPECT_EQ(okx.sessions(), 1);
  auto r = refused(order(2));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
}

// ---- new orders ---------------------------------------------------------------------------------

TEST_F(OkxOMTest, OrderFieldsForSpotAndSwap)
{
  start();
  auto r = place(order(10, 25, 631234, en::bs::BUY));
  EXPECT_EQ(r.id, "n" + cl(r));
  EXPECT_EQ(r.arg["instIdCode"], 3);
  EXPECT_EQ(r.arg["tdMode"], "cash");
  EXPECT_EQ(r.arg["side"], "buy");
  EXPECT_EQ(r.arg["ordType"], "limit");
  EXPECT_EQ(r.arg["sz"], "0.0025");
  EXPECT_EQ(r.arg["px"], "63123.4");
  EXPECT_EQ(cl(r), std::to_string(om->book().find_oid(10)->cid));
  EXPECT_LE(cl(r).size(), 32u);

  auto w = place(order(11, 150, 630001, en::bs::SEL, true));
  EXPECT_EQ(w.arg["instIdCode"], 10459);
  EXPECT_EQ(w.arg["tdMode"], "cross");
  EXPECT_EQ(w.arg["side"], "sell");
  EXPECT_EQ(w.arg["sz"], "1.50");
  EXPECT_EQ(w.arg["px"], "63000.1");
}

TEST_F(OkxOMTest, TimeInForceMapsToOrdType)
{
  auto c = demo_config();
  start();
  auto o = order(20);
  o.tif = Order::TimeInForce::IOC;
  EXPECT_EQ(place(o).arg["ordType"], "ioc");
  o = order(21);
  o.tif = Order::TimeInForce::FOK;
  EXPECT_EQ(place(o).arg["ordType"], "fok");
  o = order(22);
  o.tif = Order::TimeInForce::GTC;
  EXPECT_EQ(place(o).arg["ordType"], "limit");
  stop();
  c.post_only = true;
  start(true, c, frame::som::ClientIdGen(6000));
  EXPECT_EQ(place(order(23)).arg["ordType"], "post_only");
}

TEST_F(OkxOMTest, UnsupportedOrdersAreRefusedLocally)
{
  start();
  std::vector<Order> bad;
  auto o = order(30);
  o.ord_type = Order::OrdType::MARKET;
  bad.push_back(o);
  o = order(31);
  o.ord_type = Order::OrdType::STOP_LIMIT;
  bad.push_back(o);
  o = order(32);
  o.tif = Order::TimeInForce::AT_CLOSE;
  bad.push_back(o);
  o = order(33, 10, 631234, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  bad.push_back(o);
  o = order(34);
  o.route = "BINANCE";
  bad.push_back(o);
  o = order(35);
  o.strategy = "TWAP";
  bad.push_back(o);
  o = order(36);
  o.sz = 1.5;
  bad.push_back(o);
  o = order(37);
  o.oid_to_cancel = 30;
  bad.push_back(o);
  o = order(38);
  o.venue = en::x::OKXF; // spot instrument on the swap venue
  bad.push_back(o);
  for (size_t i = 0; i < bad.size(); ++i)
    ASSERT_NE(refused(bad[i], i), nullptr) << "order " << bad[i].oid;
  settle();
  EXPECT_TRUE(okx.placed().empty());
}

TEST_F(OkxOMTest, LocalRiskLimits)
{
  auto c = demo_config();
  c.max_order_units = 50;
  c.max_order_notional = 1000; // USDT
  c.max_orders_per_sec = 2;
  start(true, c);
  auto r = refused(order(40, 51));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  // 0.02 BTC at 63123.4 is over 1000 USDT
  ASSERT_NE(refused(order(41, 2, 631234, en::bs::BUY, true), 1), nullptr);
  place(order(42, 1));
  place(order(43, 1));
  auto t = refused(order(44, 1), 2);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->reason, som::Reject::THROTTLE);
}

// ---- reports ------------------------------------------------------------------------------------

TEST_F(OkxOMTest, AckCarriesOrdIdThenPartialAndFullFill)
{
  start();
  auto r = place(order(50, 10));
  okx.accept(r, "690000000000000001");
  auto ack = somr.wait_for<som::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->id, 50u);
  EXPECT_EQ(ack->xordid, 690000000000000001ull);
  okx.state(cl(r), "live");
  okx.fill(cl(r), "0.0004", "63123.4", "3096458503", "partially_filled");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 4);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 6);
  EXPECT_EQ(f->pxi, 631234);
  EXPECT_EQ(f->venue, en::x::OKX);
  EXPECT_EQ(f->tim, 1795026561364000000ull);
  okx.fill(cl(r), "0.0006", "63123.3", "3096458504", "filled");
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 631233);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
  EXPECT_EQ(om->book().find_oid(50)->state, State::Filled);
  EXPECT_EQ(om->ord_id(50), "690000000000000001");
}

TEST_F(OkxOMTest, RejectsMapTheSCode)
{
  start();
  auto r1 = place(order(60));
  okx.op_reject(r1, "51008", "Order failed. Insufficient USDT balance in account.");
  auto j1 = somr.wait_for<som::Reject>();
  ASSERT_NE(j1, nullptr);
  EXPECT_EQ(j1->reason, som::Reject::EXCHLIMIT);
  auto r2 = place(order(61));
  okx.op_reject(r2, "50011", "Rate limit reached");
  auto j2 = somr.wait_for<som::Reject>(1);
  ASSERT_NE(j2, nullptr);
  EXPECT_EQ(j2->reason, som::Reject::THROTTLE);
  auto r3 = place(order(62));
  okx.op_format_error(r3); // code 60013, data []
  ASSERT_NE(somr.wait_for<som::Reject>(2), nullptr);
  EXPECT_EQ(om->book().find_oid(62)->state, State::Rejected);
}

TEST_F(OkxOMTest, CancelRoundTripNamesClOrdIdAndOrdId)
{
  start();
  auto r = place(order(70));
  okx.accept(r, "690000000000000070");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  auto c = cancel(70);
  EXPECT_EQ(c.arg["instIdCode"], 3);
  EXPECT_EQ(c.arg["clOrdId"], cl(r));
  EXPECT_EQ(c.arg["ordId"], "690000000000000070");
  EXPECT_EQ(c.id, "c" + cl(r) + "r1");
  okx.cancel_result(c, "0");
  settle();
  EXPECT_TRUE(somr.all<som::CancAck>().empty()); // the result alone is not the cancel
  okx.state(cl(r), "canceled", "1");
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(om->book().find_oid(70)->state, State::Canceled);
}

TEST_F(OkxOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto r = place(order(71));
  okx.accept(r, "690000000000000071");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  auto c = cancel(71);
  okx.cancel_result(c, "51400");
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_EQ(om->book().find_oid(71)->state, State::Working);
  // and it can be canceled again, under a new request id
  auto c2 = cancel(71);
  EXPECT_EQ(c2.id, "c" + cl(r) + "r2");
}

TEST_F(OkxOMTest, CancelOfUnknownOrFilledOrderIsRefusedLocally)
{
  start();
  om->send(new som::Cancel(999), &somr);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::NOTFOUND);
  auto r = place(order(72, 1));
  okx.accept(r, "690000000000000072");
  okx.fill(cl(r), "0.0001", "63123.4", "1", "filled");
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  om->send(new som::Cancel(72), &somr);
  auto cr2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(cr2, nullptr);
  EXPECT_EQ(cr2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(okx.cancels().empty());
}

TEST_F(OkxOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto r = place(order(73));
  okx.accept(r, "690000000000000073");
  okx.state(cl(r), "live");
  okx.state(cl(r), "canceled", "0"); // cancelSource 0: canceled by system
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST_F(OkxOMTest, MmpCanceledIsACancel)
{
  start();
  auto r = place(order(74));
  okx.accept(r, "690000000000000074");
  okx.state(cl(r), "mmp_canceled");
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(OkxOMTest, FillBeforeAck)
{
  start();
  auto r = place(order(80, 10));
  okx.fill(cl(r), "0.0004", "63123.4", "11", "partially_filled");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 6);
  okx.accept(r, "690000000000000080");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  okx.state(cl(r), "partially_filled");
  okx.fill(cl(r), "0.0006", "63123.4", "12", "filled");
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(80)->state, State::Filled);
}

TEST_F(OkxOMTest, FillRacingCancel)
{
  start();
  auto r = place(order(81, 10));
  okx.accept(r, "690000000000000081");
  auto c = cancel(81);
  okx.fill(cl(r), "0.0010", "63123.4", "21", "filled");
  okx.cancel_result(c, "51401"); // order already completed
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 0);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
  EXPECT_EQ(om->book().find_oid(81)->state, State::Filled);
}

TEST_F(OkxOMTest, FillAfterCancelAckStillCounts)
{
  start();
  auto r = place(order(83, 10));
  okx.accept(r, "690000000000000083");
  cancel(83);
  okx.state(cl(r), "canceled", "1");
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  okx.fill(cl(r), "0.0004", "63123.4", "31", "canceled");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 4);
  EXPECT_EQ(om->book().find_oid(83)->cum, 4);
  EXPECT_EQ(somr.all<som::CancAck>().size(), 1u);
}

TEST_F(OkxOMTest, AmendsAndZeroFillUpdatesAreNotFills)
{
  start();
  auto r = place(order(84, 10));
  okx.accept(r, "690000000000000084");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  // A system amendment (amendSource 4) and a market-order style update with
  // fillSz 0: OKX has no bust or correction message; neither is a fill.
  okx.push({{"ordId", "690000000000000084"}, {"clOrdId", cl(r)}, {"state", "live"}, {"fillSz", "0"},
            {"tradeId", ""}, {"amendSource", "4"}, {"amendResult", "0"}, {"sz", "0.0008"}});
  okx.push({{"ordId", "690000000000000084"}, {"clOrdId", cl(r)}, {"state", "live"}, {"fillSz", "0"},
            {"tradeId", "99"}});
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
}

TEST_F(OkxOMTest, SameTradeIdIsAppliedOnce)
{
  start();
  auto r = place(order(85, 10));
  okx.accept(r, "690000000000000085");
  okx.fill(cl(r), "0.0005", "63123.4", "41", "partially_filled");
  okx.fill(cl(r), "0.0005", "63123.4", "41", "partially_filled");
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(85)->cum, 5);
}

// ---- busy, reconnect -------------------------------------------------------------------------

TEST_F(OkxOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  okx.place_status = CallStatus::Busy;
  auto rej = refused(order(90));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::THROTTLE);
  EXPECT_TRUE(wait_until([&] { return om->busy(); }));
  okx.place_status = CallStatus::Success;
  auto rej2 = refused(order(91), 1);
  ASSERT_NE(rej2, nullptr);
  EXPECT_EQ(rej2->reason, som::Reject::THROTTLE);
  // A new session starts not busy.
  okx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && okx.sessions() == 2; }));
  EXPECT_FALSE(om->busy());
  place(order(92));
}

TEST_F(OkxOMTest, CancelOnABusyConnectionIsRefusedAndTheOrderStaysWorking)
{
  start();
  auto r = place(order(93));
  okx.accept(r, "690000000000000093");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  okx.cancel_status = CallStatus::Busy;
  om->send(new som::Cancel(93), &somr);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::THROTTLE);
  EXPECT_EQ(om->book().find_oid(93)->state, State::Working);
}

TEST_F(OkxOMTest, ReconnectRecoversAMissedFillExactlyOnce)
{
  start();
  auto r = place(order(100, 10));
  okx.accept(r, "690000000000000100");
  okx.fill(cl(r), "0.0004", "63123.4", "51", "partially_filled");
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);

  okx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  okx.fill(cl(r), "0.0006", "63123.5", "52", "filled", false); // while down

  ASSERT_TRUE(wait_until([&] { return om->ready() && okx.sessions() == 2; }));
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->sz, 6);
  EXPECT_EQ(f2->pxi, 631235);
  settle();
  auto rec = okx.recoveries();
  ASSERT_EQ(rec.size(), 1u);
  EXPECT_EQ(rec[0], cl(r));
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u); // fill 51 came again from REST: skipped
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(100)->state, State::Filled);
}

TEST_F(OkxOMTest, ReconnectRecoversACancelWhileDown)
{
  start();
  auto r = place(order(101, 10));
  okx.accept(r, "690000000000000101");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  okx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  okx.state(cl(r), "canceled", "0"); // not delivered: the connection is down
  ASSERT_TRUE(wait_until([&] { return om->ready() && okx.sessions() == 2; }));
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(om->book().find_oid(101)->state, State::Canceled);
}

TEST_F(OkxOMTest, OrdersAndCancelsWhileDisconnectedAreRefused)
{
  auto c = demo_config();
  c.reconnect_ms = 100000;
  start(true, c);
  auto r = place(order(110));
  okx.accept(r, "690000000000000110");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  okx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto rej = refused(order(111));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
  om->send(new som::Cancel(110), &somr);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(110)->state, State::Working);
}

TEST_F(OkxOMTest, CancelInFlightWhenTheConnectionDropsIsRefused)
{
  auto c = demo_config();
  c.reconnect_ms = 100000;
  start(true, c);
  auto r = place(order(112));
  okx.accept(r, "690000000000000112");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cancel(112);
  okx.fail_connection();
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->id, 112u);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(112)->state, State::Working);
}

TEST_F(OkxOMTest, OrderInFlightAtTheDropIsRejectedWhenOkxDoesNotKnowIt)
{
  start();
  auto lost = place(order(113)); // its op result never comes
  auto kept = place(order(114));
  okx.accept(kept, "690000000000000114");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  okx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && okx.sessions() == 2; }));
  // Recovery: OKX has no order with the lost one's clOrdId.
  auto rej = somr.wait_for<som::Reject>();
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->id, 113u);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
  settle();
  EXPECT_EQ(okx.recoveries().size(), 2u);
  EXPECT_EQ(om->book().find_oid(113)->state, State::Rejected);
  EXPECT_EQ(om->book().find_oid(114)->state, State::Working);
  EXPECT_EQ(somr.all<som::Reject>().size(), 1u);
  (void)lost;
}

TEST_F(OkxOMTest, OrdersOutsideTheOmAreLoggedAndIgnored)
{
  start();
  okx.push({{"instId", "BTC-USDT"}, {"ordId", "1"}, {"clOrdId", "manual1"}, {"state", "live"}, {"fillSz", "0"}});
  okx.push({{"instId", "BTC-USDT"}, {"ordId", "1"}, {"clOrdId", "manual1"}, {"state", "filled"},
            {"fillSz", "0.001"}, {"fillPx", "63000"}, {"tradeId", "77"}});
  okx.push({{"instId", "BTC-USDT"}, {"ordId", "2"}, {"clOrdId", "424242"}, {"state", "live"}, {"fillSz", "0"}});
  settle();
  EXPECT_TRUE(somr.replies().empty());
}

TEST_F(OkxOMTest, ClientIdsAreUniqueIncreasingAndNotReusedAfterRestart)
{
  start(true, demo_config(), frame::som::ClientIdGen(1000));
  auto r1 = place(order(120));
  auto r2 = place(order(121));
  EXPECT_LT(std::stoull(cl(r1)), std::stoull(cl(r2)));
  stop();
  start(true, demo_config(), frame::som::ClientIdGen(1001)); // a later millisecond
  auto r3 = place(order(122));
  EXPECT_GT(std::stoull(cl(r3)), std::stoull(cl(r2)));
}

TEST_F(OkxOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto r = place(order(130));
  okx.accept(r, "690000000000000130");
  for (int i = 0; i < 50; ++i)
    okx.state(cl(r), "live");
  stop();
  SUCCEED();
}
