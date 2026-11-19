/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenOM against FakeKraken (order_entry.md, "Tests"), with KrakenOM on its
// own actor thread under a kaspar Manager. "SOM" is a recorder; the test
// thread plays the session's thread.

#include <gtest/gtest.h>

#include <set>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeKraken.hpp"
#include "TestUtil.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace kraken::oe;
using namespace kraken::oe::test;

namespace
{
  class KrakenOMTest : public ::testing::Test
  {
  protected:
    FakeKraken kr;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    KrakenOM *om = nullptr;
    Assets a;

    void SetUp() override { a = assets(); }

    void start(bool wait_ready = true, KrakenOMConfig c = om_config(), en::x venue = en::x::KRAKEN,
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000))
    {
      om = new KrakenOM(c, &somr, kr.factory(), venue, ids);
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

    // 10 units of 0.0001 BTC at 63123.4 (ticks of 0.1), GTC limit
    Order order(int oid, int units = 10, int px = 631234, en::bs side = en::bs::BUY)
    {
      Order o("KR", en::x::KRAKEN, uint(a.spot), units, px, side, 4);
      o.oid = oid;
      o.tif = Order::TimeInForce::GTC;
      return o;
    }

    NewOrder place(const Order &o)
    {
      auto before = kr.orders().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return kr.orders().size() > before; })) << "order never reached the session";
      auto v = kr.orders();
      return v.empty() ? NewOrder{} : v.back();
    }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }

    FakeKraken::Cancel cancel(int oid)
    {
      auto before = kr.cancels().size();
      om->send(new som::Cancel(oid), &somr);
      EXPECT_TRUE(wait_until([&] { return kr.cancels().size() > before; }));
      auto c = kr.cancels();
      return c.empty() ? FakeKraken::Cancel{} : c.back();
    }

    static Report rep(Report::Kind k, const std::string &cl, const std::string &oid = "")
    {
      Report r;
      r.kind = k;
      r.cl_ord_id = cl;
      r.order_id = oid;
      return r;
    }
    static Report trade(const std::string &cl, const std::string &oid, const std::string &exec, const std::string &qty,
                        const std::string &px = "63123.4")
    {
      Report r = rep(Report::Trade, cl, oid);
      r.exec_id = exec;
      r.last_qty = qty;
      r.last_price = px;
      return r;
    }

    // An order placed and acknowledged; returns its client order id.
    std::string working(int oid, const std::string &venue_id, int units = 10)
    {
      auto n = place(order(oid, units));
      size_t acks = somr.all<som::Ack>().size();
      kr.report(rep(Report::Accepted, n.cl_ord_id, venue_id));
      EXPECT_NE(somr.wait_for<som::Ack>(acks), nullptr);
      return n.cl_ord_id;
    }

    void settle() { std::this_thread::sleep_for(100ms); }
  };
}

// ---- logon, registration, configuration ------------------------------------------------------

TEST_F(KrakenOMTest, LogsInAndRegistersWithTheSom)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>(0);
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::KRAKEN);
  EXPECT_EQ(kr.sessions(), 1);
}

TEST_F(KrakenOMTest, FuturesDemoRegistersAsKrakenF)
{
  start(true, om_config(), en::x::KRAKENF);
  auto reg = somr.wait_for<som::SetExchManager>(0);
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::KRAKENF);
}

TEST_F(KrakenOMTest, BadConfigOrNoFactoryGivesNullNotCrash)
{
  auto c = om_config();
  c.qty_unit = 0;
  EXPECT_EQ(create_KrakenOM(c, &somr, kr.factory()), nullptr);
  c = om_config();
  c.reconnect_ms = 1;
  EXPECT_EQ(create_KrakenOM(c, &somr, kr.factory()), nullptr);
  EXPECT_EQ(create_KrakenOM(om_config(), &somr, SessionFactory{}), nullptr);
  auto *ok = create_KrakenOM(om_config(), &somr, kr.factory());
  EXPECT_NE(ok, nullptr);
  delete ok;
}

TEST_F(KrakenOMTest, OrderBeforeLoginIsRejectedLocally)
{
  kr.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return kr.sessions() == 1; }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(kr.orders().empty());
}

TEST_F(KrakenOMTest, LoginFailureIsNotRetried)
{
  kr.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return kr.sessions() == 1; }));
  kr.ev()->login_failed("EAPI:Invalid key");
  settle();
  settle();
  EXPECT_EQ(kr.sessions(), 1);
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
}

// ---- fields as sent, and what is refused ------------------------------------------------------

TEST_F(KrakenOMTest, LimitOrderFieldsAsSent)
{
  start();
  auto n = place(order(1, 10, 631234, en::bs::SEL));
  EXPECT_EQ(n.symbol, "BTC/USD");
  EXPECT_FALSE(n.buy);
  EXPECT_EQ(n.qty, "0.0010");
  EXPECT_EQ(n.price, "63123.4");
  EXPECT_FALSE(n.market);
  EXPECT_EQ(n.tif, Tif::GTC);
  EXPECT_FALSE(n.post_only);
  frame::som::ClientId cid;
  ASSERT_TRUE(KrakenOM::decode_cid(n.cl_ord_id, cid));
  EXPECT_LE(n.cl_ord_id.size(), 18u); // spot free-text cl_ord_id limit
  EXPECT_EQ(om->client_order_id(1), n.cl_ord_id);
}

TEST_F(KrakenOMTest, DayGoesOutGtcIocFokAndMarketAsSent)
{
  start();
  auto o = order(1);
  o.tif = Order::TimeInForce::DAY;
  EXPECT_EQ(place(o).tif, Tif::GTC);
  o = order(2);
  o.tif = Order::TimeInForce::IOC;
  EXPECT_EQ(place(o).tif, Tif::IOC);
  o = order(3);
  o.tif = Order::TimeInForce::FOK;
  EXPECT_EQ(place(o).tif, Tif::FOK);
  o = order(4);
  o.ord_type = Order::OrdType::MARKET;
  auto m = place(o);
  EXPECT_TRUE(m.market);
  EXPECT_TRUE(m.price.empty());
}

TEST_F(KrakenOMTest, PostOnlyConfigMarksLimitOrders)
{
  auto c = om_config();
  c.post_only = true;
  start(true, c);
  EXPECT_TRUE(place(order(1)).post_only);
}

TEST_F(KrakenOMTest, UnsupportedCombinationsAreRefusedLocally)
{
  auto c = om_config();
  c.max_order_units = 100;
  c.max_order_notional = 1000; // USD
  c.day_as_gtc = false;
  start(true, c);
  size_t n = 0;
  auto expect_refused = [&](Order o, int reason) {
    auto r = refused(o, n++);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->reason, reason);
  };
  auto o = order(1);
  o.ord_type = Order::OrdType::STOP;
  expect_refused(o, som::Reject::UNKNOWN);
  o = order(2);
  o.strategy = "VWAP";
  expect_refused(o, som::Reject::UNKNOWN);
  o = order(3);
  o.route = "NYSE";
  expect_refused(o, som::Reject::UNKNOWN);
  o = order(4, 10, 631234, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  expect_refused(o, som::Reject::UNKNOWN);
  o = order(5);
  o.tif = Order::TimeInForce::AT_OPEN;
  expect_refused(o, som::Reject::UNKNOWN);
  o = order(6);
  o.tif = Order::TimeInForce::DAY; // day_as_gtc = false
  expect_refused(o, som::Reject::UNKNOWN);
  o = order(7);
  o.sz = 1.5;
  expect_refused(o, som::Reject::OVERLIMIT);
  o = order(8, 101);
  expect_refused(o, som::Reject::OVERLIMIT);
  o = order(9, 100, 631234); // 0.01 BTC at 63123.4 = 631 USD: under; then 1000+
  o.sz = 100;
  o.px = 1100000; // 110000.0 USD * 0.01 = 1100 USD > 1000
  expect_refused(o, som::Reject::OVERLIMIT);
  o = order(10);
  o.oid_to_cancel = 3;
  expect_refused(o, som::Reject::UNKNOWN);
  EXPECT_TRUE(kr.orders().empty());
}

TEST_F(KrakenOMTest, OrderRateLimitThrottles)
{
  auto c = om_config();
  c.max_orders_per_sec = 2;
  start(true, c);
  place(order(1));
  place(order(2));
  auto r = refused(order(3));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
}

// ---- ack, fills, rejects --------------------------------------------------------------------

TEST_F(KrakenOMTest, AckCarriesHashOfVenueOrderId)
{
  start();
  working(1, "OTCFZG-YOE2Q-LQKNM3");
  auto ack = somr.wait_for<som::Ack>(0);
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->id, 1u);
  EXPECT_EQ(ack->xordid, KrakenOM::fnv1a("OTCFZG-YOE2Q-LQKNM3"));
}

TEST_F(KrakenOMTest, PartialThenFullFill)
{
  start();
  auto cl = working(1, "O1");
  kr.report(trade(cl, "O1", "T1", "0.0004", "63123.4"));
  auto f1 = somr.wait_for<som::Fill>(0);
  ASSERT_NE(f1, nullptr);
  EXPECT_EQ(f1->sz, 4);
  EXPECT_EQ(f1->still_to_be_filled, 6);
  EXPECT_EQ(f1->pxi, 631234);
  EXPECT_EQ(f1->venue, en::x::KRAKEN);
  kr.report(trade("", "O1", "T2", "0.0006", "63123.5"));
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_EQ(f2->sz, 6);
  EXPECT_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 631235);
  kr.report(rep(Report::Filled, "", "O1"));
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

TEST_F(KrakenOMTest, VenueRejectMapsReason)
{
  start();
  auto n = place(order(1));
  Report r = rep(Report::Rejected, n.cl_ord_id);
  r.reason = "EOrder:Insufficient funds";
  kr.report(r);
  auto rj = somr.wait_for<som::Reject>(0);
  ASSERT_NE(rj, nullptr);
  EXPECT_EQ(rj->reason, som::Reject::EXCHLIMIT);
  auto n2 = place(order(2));
  r = rep(Report::Rejected, n2.cl_ord_id);
  r.reason = "apiLimitExceeded";
  kr.report(r);
  auto rj2 = somr.wait_for<som::Reject>(1);
  ASSERT_NE(rj2, nullptr);
  EXPECT_EQ(rj2->reason, som::Reject::THROTTLE);
}

// ---- cancels -------------------------------------------------------------------------------------

TEST_F(KrakenOMTest, CancelRoundTripUsesVenueOrderId)
{
  start();
  auto cl = working(1, "O1");
  auto c = cancel(1);
  EXPECT_EQ(c.order_id, "O1");
  kr.report(rep(Report::Canceled, cl, "O1"));
  auto ca = somr.wait_for<som::CancAck>(0);
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 1u);
}

TEST_F(KrakenOMTest, CancelBeforeAckUsesClientOrderId)
{
  start();
  auto n = place(order(1));
  auto c = cancel(1);
  EXPECT_TRUE(c.order_id.empty());
  EXPECT_EQ(c.cl_ord_id, n.cl_ord_id);
}

TEST_F(KrakenOMTest, CancelRejectLeavesOrderWorking)
{
  start();
  auto cl = working(1, "O1");
  cancel(1);
  kr.report(rep(Report::CancelRejected, cl));
  auto cr = somr.wait_for<som::CancReject>(0);
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  kr.report(trade(cl, "O1", "T1", "0.0010"));
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
}

TEST_F(KrakenOMTest, CancelOfUnknownOrFilledOrder)
{
  start();
  om->send(new som::Cancel(77), &somr);
  auto r1 = somr.wait_for<som::CancReject>(0);
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->reason, som::CancReject::NOTFOUND);
  auto cl = working(1, "O1");
  kr.report(trade(cl, "O1", "T1", "0.0010"));
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  om->send(new som::Cancel(1), &somr);
  auto r2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(kr.cancels().empty());
}

TEST_F(KrakenOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto cl = working(1, "O1");
  Report r = rep(Report::Canceled, "", "O1");
  r.reason = "Post only order";
  kr.report(r);
  auto ca = somr.wait_for<som::CancAck>(0);
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 1u);
}

// ---- races ----------------------------------------------------------------------------------------

TEST_F(KrakenOMTest, FillBeforeAck)
{
  start();
  auto n = place(order(1));
  kr.report(trade(n.cl_ord_id, "O1", "T1", "0.0004"));
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  kr.report(rep(Report::Accepted, n.cl_ord_id, "O1"));
  settle();
  EXPECT_LE(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
}

TEST_F(KrakenOMTest, FillRacingCancelAndFillAfterCancelAckBothCount)
{
  start();
  auto cl = working(1, "O1");
  cancel(1);
  kr.report(trade(cl, "O1", "T1", "0.0003"));
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  kr.report(rep(Report::Canceled, cl, "O1"));
  ASSERT_NE(somr.wait_for<som::CancAck>(0), nullptr);
  kr.report(trade(cl, "O1", "T2", "0.0002")); // reported after the cancel
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_EQ(f2->sz, 2);
}

TEST_F(KrakenOMTest, ReportNamingOnlyVenueIdIsHeldUntilPaired)
{
  start();
  auto n = place(order(1));
  kr.report(trade("", "O1", "T1", "0.0004")); // venue id not yet known
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
  EXPECT_EQ(om->parked(), 1u);
  kr.report(rep(Report::Accepted, n.cl_ord_id, "O1"));
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  EXPECT_EQ(om->parked(), 0u);
}

TEST_F(KrakenOMTest, OverfillAndUnreadableQuantityAreNotForwarded)
{
  start();
  auto cl = working(1, "O1");
  kr.report(trade(cl, "O1", "T1", "0.0020")); // twice the order
  kr.report(trade(cl, "O1", "T2", "abc"));
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
}

// ---- connection --------------------------------------------------------------------------------

TEST_F(KrakenOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  kr.ev()->busy();
  settle();
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  kr.ev()->available();
  settle();
  place(order(2));
}

TEST_F(KrakenOMTest, SessionBusyOnSendGivesThrottle)
{
  start();
  kr.next_status = CallStatus::Busy;
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
}

TEST_F(KrakenOMTest, ReconnectDeliversMissedFillExactlyOnce)
{
  start();
  auto cl = working(1, "O1");
  kr.report(trade(cl, "O1", "T1", "0.0004"));
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  kr.drop("link down");
  ASSERT_TRUE(wait_until([&] { return kr.sessions() == 2 && om->ready(); }));
  // The new session's snapshot: the open order, the trade already applied,
  // and a trade made while disconnected.
  Report open = rep(Report::Accepted, cl, "O1");
  open.snapshot = true;
  kr.report(open);
  Report t1 = trade(cl, "O1", "T1", "0.0004");
  t1.snapshot = true;
  kr.report(t1);
  Report t2 = trade(cl, "O1", "T2", "0.0003");
  t2.snapshot = true;
  kr.report(t2);
  auto f = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 3);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u);
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
}

TEST_F(KrakenOMTest, SequenceGapReconnects)
{
  start();
  kr.ev()->gap(10, 12);
  ASSERT_TRUE(wait_until([&] { return kr.sessions() == 2 && om->ready(); }));
}

TEST_F(KrakenOMTest, OrderWhileDisconnectedIsRejected)
{
  auto c = om_config();
  c.reconnect_ms = 100000;
  start(true, c);
  kr.drop();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
}

TEST_F(KrakenOMTest, CancelInFlightWhenTheConnectionDropsIsRefused)
{
  auto c = om_config();
  c.reconnect_ms = 100000;
  start(true, c);
  working(1, "O1");
  cancel(1);
  kr.drop();
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->id, 1u);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(1)->state, frame::som::State::Working);
}

TEST_F(KrakenOMTest, OrderInFlightAtTheDropIsRejectedWhenKrakenDoesNotKnowIt)
{
  start();
  auto lost = place(order(1)); // no report ever comes for it
  auto kept = working(2, "O2");
  kr.drop();
  ASSERT_TRUE(wait_until([&] { return kr.sessions() == 2 && om->ready(); }));
  // After the logon the OM asks the session about each live order.
  ASSERT_TRUE(wait_until([&] { return kr.recoveries().size() == 2; }));
  auto rec = kr.recoveries();
  std::set<std::string> asked{rec[0].cl_ord_id, rec[1].cl_ord_id};
  EXPECT_EQ(asked, (std::set<std::string>{lost.cl_ord_id, kept}));
  // The venue knows order 2 (its snapshot) and has nothing under order 1's id.
  Report open = rep(Report::Accepted, kept, "O2");
  open.snapshot = true;
  kr.report(open);
  Report unknown = rep(Report::Unknown, lost.cl_ord_id);
  unknown.reason = "test";
  kr.report(unknown);
  auto rej = somr.wait_for<som::Reject>();
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->id, 1u);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
  settle();
  EXPECT_EQ(om->book().find_oid(1)->state, frame::som::State::Rejected);
  EXPECT_EQ(om->book().find_oid(2)->state, frame::som::State::Working);
  EXPECT_EQ(somr.all<som::Reject>().size(), 1u);
  // An Unknown for a working order changes nothing.
  kr.report(rep(Report::Unknown, kept));
  settle();
  EXPECT_EQ(om->book().find_oid(2)->state, frame::som::State::Working);
  EXPECT_EQ(somr.all<som::Reject>().size(), 1u);
}

TEST_F(KrakenOMTest, UnknownReportsAreIgnored)
{
  start();
  kr.report(trade("zzz", "OX", "T9", "1.0"));
  kr.report(rep(Report::Canceled, "yyy"));
  kr.report(rep(Report::Other, "", "OQ"));
  settle();
  EXPECT_TRUE(somr.replies().empty());
}

// ---- identifiers ---------------------------------------------------------------------------------

TEST_F(KrakenOMTest, ClientIdsUniqueIncreasingAndNotReusedAfterRestart)
{
  start(true, om_config(), en::x::KRAKEN, frame::som::ClientIdGen(1000));
  auto a1 = place(order(1)).cl_ord_id, a2 = place(order(2)).cl_ord_id;
  stop();
  start(true, om_config(), en::x::KRAKEN, frame::som::ClientIdGen(2000)); // a later restart
  auto b1 = place(order(1)).cl_ord_id;
  frame::som::ClientId x1, x2, y1;
  ASSERT_TRUE(KrakenOM::decode_cid(a1, x1));
  ASSERT_TRUE(KrakenOM::decode_cid(a2, x2));
  ASSERT_TRUE(KrakenOM::decode_cid(b1, y1));
  EXPECT_LT(x1, x2);
  EXPECT_LT(x2, y1);
}

TEST(KrakenOMIds, Base36RoundTripAndDecimal)
{
  for (frame::som::ClientId c : {0ull, 35ull, 36ull, 123456789012ull, (1ull << 63) - 1})
  {
    frame::som::ClientId back;
    ASSERT_TRUE(KrakenOM::decode_cid(KrakenOM::encode_cid(c), back));
    EXPECT_EQ(back, c);
  }
  frame::som::ClientId x;
  EXPECT_FALSE(KrakenOM::decode_cid("ABC", x));
  EXPECT_FALSE(KrakenOM::decode_cid("", x));
  EXPECT_EQ(KrakenOM::decimal(12345, 1e-4), "1.2345");
  EXPECT_EQ(KrakenOM::decimal(1, 1e-8), "0.00000001");
  EXPECT_EQ(KrakenOM::decimal(631234, 0.1), "63123.4");
  EXPECT_EQ(KrakenOM::decimal(5, 1), "5");
}

TEST_F(KrakenOMTest, ShutdownWithReportInFlight)
{
  start();
  auto cl = working(1, "O1");
  kr.report(trade(cl, "O1", "T1", "0.0004"));
  stop(); // must not hang or crash
  SUCCEED();
}
