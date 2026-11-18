/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitOM against FakeDeribit, with DeribitOM on its own actor thread under
// a kaspar Manager. "SOM" is a recorder. The test thread plays the session's
// thread. The list follows order_entry.md, "Tests".

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeDeribit.hpp"
#include "TestUtil.hpp"
#include "deribit_oe/act/DeribitOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace deribit::oe;
using namespace deribit::oe::test;
using frame::som::State;

namespace
{
  class DeribitOMTest : public ::testing::Test
  {
  protected:
    FakeDeribitServer dbt;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    DeribitOM *om = nullptr;
    Assets a;

    void SetUp() override { a = assets(); }

    void start(bool wait_ready = true, DeribitOMConfig c = test_config(),
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000))
    {
      om = new DeribitOM(c, &somr, dbt.factory(), ids);
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

    // 10 contracts (100 USD) of BTC-PERPETUAL at 63000.0 (ticks of 0.5)
    Order order(int oid, int units = 10, int px = 126000, en::bs side = en::bs::BUY, bool eth = false)
    {
      Order o("CR", en::x::DERIBIT, uint(eth ? a.eth : a.btc), units, px, side, 4);
      o.oid = oid;
      return o;
    }

    FakeDeribitServer::Req place(const Order &o)
    {
      auto before = dbt.placed().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return dbt.placed().size() > before; })) << "order never reached Deribit";
      auto p = dbt.placed();
      return p.empty() ? FakeDeribitServer::Req{} : p.back();
    }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }

    FakeDeribitServer::Req cancel(int oid)
    {
      auto before = dbt.cancels().size();
      om->send(new som::Cancel(oid), &somr);
      EXPECT_TRUE(wait_until([&] { return dbt.cancels().size() > before; }));
      auto c = dbt.cancels();
      return c.empty() ? FakeDeribitServer::Req{} : c.back();
    }

    void settle() { std::this_thread::sleep_for(150ms); }
  };
}

// ---- logon and registration ---------------------------------------------------------------------

TEST_F(DeribitOMTest, LogsInAndRegistersForDeribit)
{
  start();
  ASSERT_NE(somr.wait_for<som::SetExchManager>(), nullptr);
  auto regs = somr.all<som::SetExchManager>();
  ASSERT_EQ(regs.size(), 1u);
  EXPECT_EQ(regs[0]->venue, en::x::DERIBIT);
  EXPECT_EQ(dbt.sessions(), 1);
}

TEST_F(DeribitOMTest, BadConfigOrRefusedEnvironmentGivesNullNotCrash)
{
  assets();
  auto c = test_config();
  c.instruments[0].asset = "NO_SUCH_ASSET";
  EXPECT_EQ(create_DeribitOM(c, &somr, dbt.factory()), nullptr);
  c = test_config();
  c.ep.host = "www.deribit.com";   // production host while test
  EXPECT_EQ(create_DeribitOM(c, &somr, dbt.factory()), nullptr);
  c = test_config();
  c.ep.test = false;
  c.ep.host = "www.deribit.com";   // production without allow_production
  EXPECT_EQ(create_DeribitOM(c, &somr, dbt.factory()), nullptr);
  EXPECT_EQ(create_DeribitOM("/nonexistent.ini", &somr), nullptr);
  c = test_config();
  auto *ok = create_DeribitOM(c, &somr, dbt.factory());
  EXPECT_NE(ok, nullptr);
  delete ok;
}

TEST_F(DeribitOMTest, OrderBeforeLoginIsRejectedLocally)
{
  dbt.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return dbt.sessions() == 1; }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(dbt.placed().empty());
}

TEST_F(DeribitOMTest, LoginRejectedStaysDown)
{
  dbt.reject_login = true;
  start(false);
  ASSERT_TRUE(wait_until([&] { return dbt.sessions() == 1; }));
  settle();
  EXPECT_EQ(dbt.sessions(), 1);
  auto r = refused(order(2));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
}

// ---- new orders -----------------------------------------------------------------------------------

TEST_F(DeribitOMTest, OrderFieldsForInverseAndLinear)
{
  start();
  auto r = place(order(10, 25, 126001, en::bs::BUY));
  EXPECT_EQ(r.method, "private/buy");
  EXPECT_EQ(r.params["instrument_name"], "BTC-PERPETUAL");
  EXPECT_EQ(r.params["type"], "limit");
  EXPECT_DOUBLE_EQ(r.params["amount"].get<double>(), 250);   // USD
  EXPECT_DOUBLE_EQ(r.params["price"].get<double>(), 63000.5);
  EXPECT_EQ(r.params["time_in_force"], "good_til_day");   // Order defaults to DAY
  EXPECT_FALSE(r.params.contains("post_only"));
  EXPECT_EQ(r.label(), std::to_string(om->book().find_oid(10)->cid));
  EXPECT_LE(r.label().size(), 64u);

  auto e = place(order(11, 150, 25001, en::bs::SEL, true));
  EXPECT_EQ(e.method, "private/sell");
  EXPECT_EQ(e.params["instrument_name"], "ETH_USDC-PERPETUAL");
  EXPECT_DOUBLE_EQ(e.params["amount"].get<double>(), 0.15);   // ETH
  EXPECT_DOUBLE_EQ(e.params["price"].get<double>(), 2500.1);
}

TEST_F(DeribitOMTest, TimeInForceAndPostOnly)
{
  auto c = test_config();
  start();
  auto o = order(20);
  o.tif = Order::TimeInForce::IOC;
  EXPECT_EQ(place(o).params["time_in_force"], "immediate_or_cancel");
  o = order(21);
  o.tif = Order::TimeInForce::FOK;
  EXPECT_EQ(place(o).params["time_in_force"], "fill_or_kill");
  o = order(22);
  o.tif = Order::TimeInForce::GTC;
  EXPECT_EQ(place(o).params["time_in_force"], "good_til_cancelled");
  stop();
  c.post_only = true;
  start(true, c, frame::som::ClientIdGen(6000));
  auto p = place(order(23));
  EXPECT_EQ(p.params["post_only"], true);
  EXPECT_EQ(p.params["reject_post_only"], true);   // never repriced
  o = order(24);
  o.tif = Order::TimeInForce::IOC;
  EXPECT_FALSE(place(o).params.contains("post_only"));
}

TEST_F(DeribitOMTest, UnsupportedOrdersAreRefusedLocally)
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
  o = order(33, 10, 126000, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  bad.push_back(o);
  o = order(34);
  o.route = "OKX";
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
  o.venue = en::x::OKXF;
  bad.push_back(o);
  for (size_t i = 0; i < bad.size(); ++i)
    ASSERT_NE(refused(bad[i], i), nullptr) << "order " << bad[i].oid;
  settle();
  EXPECT_TRUE(dbt.placed().empty());
}

TEST_F(DeribitOMTest, LocalRiskLimits)
{
  auto c = test_config();
  c.max_order_units = 50;
  c.max_order_notional = 1000;   // USD
  c.max_orders_per_sec = 2;
  start(true, c);
  auto r = refused(order(40, 51));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  // linear: 0.5 ETH at 2500.1 is over 1000 USD
  ASSERT_NE(refused(order(41, 500, 25001, en::bs::BUY, true), 1), nullptr);
  place(order(42, 1));
  place(order(43, 1));
  auto t = refused(order(44, 1), 2);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->reason, som::Reject::THROTTLE);
}

// ---- reports ----------------------------------------------------------------------------------------

TEST_F(DeribitOMTest, AckCarriesOrderIdThenPartialAndFullFill)
{
  start();
  auto r = place(order(50, 10));
  dbt.accept(r, "BTC-31975418392");
  auto ack = somr.wait_for<som::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->id, 50u);
  EXPECT_EQ(ack->xordid, 31975418392ull);
  dbt.fill(r.label(), 40, 63000.0, "BTC-100001", "open");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 4);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 6);
  EXPECT_EQ(f->pxi, 126000);
  EXPECT_EQ(f->venue, en::x::DERIBIT);
  EXPECT_EQ(f->tim, 1795026561364000000ull);
  dbt.fill(r.label(), 60, 62999.5, "BTC-100002", "filled");
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 125999);
  dbt.state(r.label(), "filled");
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
  EXPECT_EQ(om->book().find_oid(50)->state, State::Filled);
  EXPECT_EQ(om->order_id(50), "BTC-31975418392");
}

TEST_F(DeribitOMTest, ImmediateFillsInTheResponseCountOnceWithTheNotification)
{
  start();
  auto r = place(order(51, 10));
  auto t = dbt.fill(r.label(), 100, 63000.0, "BTC-200001", "filled", false);
  dbt.accept(r, "BTC-31975418393", "filled", {t});
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  dbt.push_trade(t);   // the same trade on user.trades
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(51)->state, State::Filled);
}

TEST_F(DeribitOMTest, IocRemainderIsCancelled)
{
  start();
  auto o = order(52, 10);
  o.tif = Order::TimeInForce::IOC;
  auto r = place(o);
  auto t = dbt.fill(r.label(), 30, 63000.0, "BTC-200002", "cancelled", false);
  dbt.accept(r, "BTC-31975418394", "cancelled", {t});
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  EXPECT_EQ(om->book().find_oid(52)->cum, 3);
}

TEST_F(DeribitOMTest, RejectsMapTheErrorCode)
{
  start();
  auto r1 = place(order(60));
  dbt.reject(r1, 10009, "not_enough_funds");
  auto j1 = somr.wait_for<som::Reject>();
  ASSERT_NE(j1, nullptr);
  EXPECT_EQ(j1->reason, som::Reject::EXCHLIMIT);
  auto r2 = place(order(61));
  dbt.reject(r2, 10028, "too_many_requests");
  auto j2 = somr.wait_for<som::Reject>(1);
  ASSERT_NE(j2, nullptr);
  EXPECT_EQ(j2->reason, som::Reject::THROTTLE);
  auto r3 = place(order(62));
  dbt.reject(r3, 11054, "post_only_reject");
  auto j3 = somr.wait_for<som::Reject>(2);
  ASSERT_NE(j3, nullptr);
  EXPECT_EQ(j3->reason, som::Reject::UNKNOWN);
  EXPECT_EQ(om->book().find_oid(62)->state, State::Rejected);
}

TEST_F(DeribitOMTest, CancelRoundTripByOrderId)
{
  start();
  auto r = place(order(70));
  dbt.accept(r, "BTC-70");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  auto c = cancel(70);
  EXPECT_EQ(c.method, "private/cancel");
  EXPECT_EQ(c.params["order_id"], "BTC-70");
  dbt.cancel_ok(c, r.label());
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  dbt.state(r.label(), "cancelled", "user_request");   // the notification as well
  settle();
  EXPECT_EQ(somr.all<som::CancAck>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(70)->state, State::Canceled);
}

TEST_F(DeribitOMTest, CancelBeforeTheResponseUsesTheLabel)
{
  start();
  auto r = place(order(72));
  auto c = cancel(72);
  EXPECT_EQ(c.method, "private/cancel_by_label");
  EXPECT_EQ(c.params["label"], r.label());
  EXPECT_EQ(c.params["currency"], "BTC");
  dbt.accept(r, "BTC-72");
  dbt.cancel_count(c, 1);
  dbt.state(r.label(), "cancelled", "user_request");
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST_F(DeribitOMTest, CancelByLabelThatCancelsNothingIsACancelReject)
{
  start();
  auto r = place(order(73));
  auto c = cancel(73);
  dbt.cancel_count(c, 0);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  dbt.accept(r, "BTC-73");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  EXPECT_EQ(om->book().find_oid(73)->state, State::Working);
}

TEST_F(DeribitOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto r = place(order(71));
  dbt.accept(r, "BTC-71");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  auto c = cancel(71);
  dbt.cancel_error(c, 10028, "too_many_requests");
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_EQ(om->book().find_oid(71)->state, State::Working);
  auto c2 = cancel(71);
  EXPECT_NE(c2.id, c.id);
}

TEST_F(DeribitOMTest, CancelOfUnknownOrFilledOrderIsRefusedLocally)
{
  start();
  om->send(new som::Cancel(999), &somr);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::NOTFOUND);
  auto r = place(order(74, 1));
  dbt.accept(r, "BTC-74");
  dbt.fill(r.label(), 10, 63000.0, "BTC-1", "filled");
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  om->send(new som::Cancel(74), &somr);
  auto cr2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(cr2, nullptr);
  EXPECT_EQ(cr2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(dbt.cancels().empty());
}

TEST_F(DeribitOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto r = place(order(75));
  dbt.accept(r, "BTC-75");
  dbt.state(r.label(), "cancelled", "cancel_on_disconnect");
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST_F(DeribitOMTest, RejectedStateOnTheChannelIsAReject)
{
  start();
  auto r = place(order(76));
  dbt.state(r.label(), "rejected");
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
  EXPECT_EQ(om->book().find_oid(76)->state, State::Rejected);
}

TEST_F(DeribitOMTest, FillBeforeAck)
{
  start();
  auto r = place(order(80, 10));
  dbt.fill(r.label(), 40, 63000.0, "BTC-11", "open");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 6);
  dbt.accept(r, "BTC-80");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  dbt.fill(r.label(), 60, 63000.0, "BTC-12", "filled");
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(80)->state, State::Filled);
}

TEST_F(DeribitOMTest, FillRacingCancel)
{
  start();
  auto r = place(order(81, 10));
  dbt.accept(r, "BTC-81");
  auto c = cancel(81);
  dbt.fill(r.label(), 100, 63000.0, "BTC-21", "filled");
  dbt.cancel_error(c, 11044, "not_open_order");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 0);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
  EXPECT_EQ(om->book().find_oid(81)->state, State::Filled);
}

TEST_F(DeribitOMTest, FillAfterCancelAckStillCounts)
{
  start();
  auto r = place(order(83, 10));
  dbt.accept(r, "BTC-83");
  cancel(83);
  dbt.state(r.label(), "cancelled", "user_request");
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  dbt.fill(r.label(), 40, 63000.0, "BTC-31", "cancelled");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 4);
  EXPECT_EQ(om->book().find_oid(83)->cum, 4);
  EXPECT_EQ(somr.all<som::CancAck>().size(), 1u);
}

TEST_F(DeribitOMTest, EditsAndUntriggeredStatesAreNotFills)
{
  start();
  auto r = place(order(84, 10));
  dbt.accept(r, "BTC-84");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  // Deribit has no bust or correction message. An edit made outside the OM
  // (replaced: true) and an untriggered state change are not fills.
  dbt.push_order({{"order_id", "BTC-84"}, {"label", r.label()}, {"order_state", "open"}, {"replaced", true},
                  {"amount", 50}, {"filled_amount", 0}});
  dbt.push_order({{"order_id", "BTC-84"}, {"label", r.label()}, {"order_state", "untriggered"}});
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
}

TEST_F(DeribitOMTest, SameTradeIdIsAppliedOnce)
{
  start();
  auto r = place(order(85, 10));
  dbt.accept(r, "BTC-85");
  auto t = dbt.fill(r.label(), 50, 63000.0, "BTC-41", "open");
  dbt.push_trade(t);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(85)->cum, 5);
}

TEST_F(DeribitOMTest, FractionalAmountIsLoggedAndRounded)
{
  start();
  auto r = place(order(86, 10));
  dbt.accept(r, "BTC-86");
  dbt.fill(r.label(), 5, 63000.0, "BTC-51", "open");   // half a contract: not a whole unit
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty() || somr.all<som::Fill>()[0]->sz == 1);
}

// ---- busy, reconnect ------------------------------------------------------------------------------

TEST_F(DeribitOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  dbt.call_status = CallStatus::Busy;
  auto rej = refused(order(90));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::THROTTLE);
  EXPECT_TRUE(wait_until([&] { return om->busy(); }));
  dbt.call_status = CallStatus::Success;
  auto rej2 = refused(order(91), 1);
  ASSERT_NE(rej2, nullptr);
  EXPECT_EQ(rej2->reason, som::Reject::THROTTLE);
  dbt.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && dbt.sessions() == 2; }));
  EXPECT_FALSE(om->busy());
  place(order(92));
}

TEST_F(DeribitOMTest, ReconnectRecoversAMissedFillExactlyOnce)
{
  start();
  auto r = place(order(100, 10));
  dbt.accept(r, "BTC-100");
  dbt.fill(r.label(), 40, 63000.0, "BTC-61", "open");
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  dbt.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  dbt.fill(r.label(), 60, 63000.5, "BTC-62", "filled", false);   // while down
  ASSERT_TRUE(wait_until([&] { return om->ready() && dbt.sessions() == 2; }));
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->sz, 6);
  EXPECT_EQ(f2->pxi, 126001);
  settle();
  auto rec = dbt.recoveries();
  ASSERT_EQ(rec.size(), 1u);
  EXPECT_EQ(rec[0], "BTC-100");
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u);   // BTC-61 came again: skipped
  EXPECT_EQ(om->book().find_oid(100)->state, State::Filled);
}

TEST_F(DeribitOMTest, ReconnectRecoversAnOrderWhoseResponseWasLost)
{
  start();
  auto r = place(order(101, 10));
  dbt.fail_connection();   // the buy result never arrives
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  dbt.accept(r, "BTC-101");   // Deribit has the order (no session: nothing delivered)
  dbt.state(r.label(), "cancelled", "cancel_on_disconnect");
  ASSERT_TRUE(wait_until([&] { return om->ready() && dbt.sessions() == 2; }));
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  auto rec = dbt.recoveries();
  ASSERT_EQ(rec.size(), 1u);
  EXPECT_EQ(rec[0], "label:" + r.label());
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST_F(DeribitOMTest, CancelInFlightWhenTheConnectionDropsIsRefused)
{
  auto c = test_config();
  c.reconnect_ms = 100000;
  start(true, c);
  auto r = place(order(102));
  dbt.accept(r, "BTC-102");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cancel(102);
  dbt.fail_connection();
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(102)->state, State::Working);
}

TEST_F(DeribitOMTest, OrdersAndCancelsWhileDisconnectedAreRefused)
{
  auto c = test_config();
  c.reconnect_ms = 100000;
  start(true, c);
  auto r = place(order(110));
  dbt.accept(r, "BTC-110");
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  dbt.fail_connection();
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

TEST_F(DeribitOMTest, OrdersOutsideTheOmAreLoggedAndIgnored)
{
  start();
  dbt.push_order({{"order_id", "BTC-1"}, {"label", "manual"}, {"order_state", "open"}});
  dbt.push_trade({{"order_id", "BTC-1"}, {"label", "manual"}, {"trade_id", "BTC-77"}, {"amount", 10},
                  {"price", 63000.0}, {"direction", "buy"}});
  dbt.push_order({{"order_id", "BTC-2"}, {"label", "424242"}, {"order_state", "open"}});
  settle();
  EXPECT_TRUE(somr.replies().empty());
}

TEST_F(DeribitOMTest, ClientIdsAreUniqueIncreasingAndNotReusedAfterRestart)
{
  start(true, test_config(), frame::som::ClientIdGen(1000));
  auto r1 = place(order(120));
  auto r2 = place(order(121));
  EXPECT_LT(std::stoull(r1.label()), std::stoull(r2.label()));
  EXPECT_NE(r1.id, r2.id);
  stop();
  start(true, test_config(), frame::som::ClientIdGen(1001));
  auto r3 = place(order(122));
  EXPECT_GT(std::stoull(r3.label()), std::stoull(r2.label()));
}

TEST_F(DeribitOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto r = place(order(130));
  dbt.accept(r, "BTC-130");
  for (int i = 0; i < 50; ++i)
    dbt.state(r.label(), "open");
  stop();
  SUCCEED();
}

TEST(DeribitOMStatic, XordidFromOrderId)
{
  EXPECT_EQ(DeribitOM::xordid("ETH-584849853"), 584849853ull);
  EXPECT_EQ(DeribitOM::xordid("31975418392"), 31975418392ull);
  EXPECT_EQ(DeribitOM::xordid(""), 0ull);
  EXPECT_EQ(DeribitOM::xordid("BTC-x"), 0ull);
}
