/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LimeOM against FakeLime, with LimeOM on its own actor thread under a
// kaspar Manager as in production. "SOM" is a recorder: orders come from it,
// replies go back to it. The test thread plays the API's callback thread.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeLime.hpp"
#include "TestUtil.hpp"
#include "lime_om/act/LimeOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace lime;
using namespace lime::test;
using frame::som::State;

namespace
{
  constexpr int PX = 4012;            // $40.12 in cents
  constexpr ScaledPrice SPX = 401200; // the same, scaled by 10,000

  class LimeOMTest : public ::testing::Test
  {
  protected:
    FakeLimeServer lm;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    LimeOM *om = nullptr;
    const frame::ref::Asset *msft = nullptr;

    void SetUp() override { msft = test_stock("MSFT"); }

    static LimeOMConfig cfg()
    {
      LimeOMConfig c;
      c.account = "ACCT1";
      c.user = "trader";
      c.password = "secret";
      c.default_route = "INET";
      c.algo_route = "LSS";
      c.reconnect_ms = 50;
      return c;
    }

    void start(bool wait_login = true, LimeOMConfig c = cfg(),
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000))
    {
      om = new LimeOM(c, &somr, lm.factory(), en::x::LIME, ids);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      if (wait_login)
        ASSERT_TRUE(wait_until([&] { return om->logged_in(); })) << "never logged in";
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

    Order order(int oid, int sz = 100, int px = PX, en::bs side = en::bs::BUY)
    {
      Order o("EQ", en::x::LIME, msft->id, sz, px, side, 4);
      o.oid = oid;
      return o;
    }

    // Send as the SOM does; wait until it reaches Lime; return its OrderId.
    OrderId place(const Order &o)
    {
      auto before = lm.orders().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return lm.orders().size() > before; })) << "order never reached Lime";
      auto v = lm.orders();
      return v.empty() ? 0 : v.back().id;
    }
    NewOrder sent(const Order &o) // place and return the request as sent
    {
      place(o);
      auto v = lm.orders();
      return v.empty() ? NewOrder{} : v.back();
    }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }

    void cancel(int oid)
    {
      auto before = lm.cancels().size();
      om->send(new som::Cancel(oid), &somr);
      EXPECT_TRUE(wait_until([&] { return lm.cancels().size() > before; })) << "cancel never reached Lime";
    }
    void cancel_only(int oid) { om->send(new som::Cancel(oid), &somr); }

    // Every event the fake has numbered has been applied.
    void synced()
    {
      ASSERT_TRUE(wait_until([&] { return om->last_event_id() == lm.last_event_id(); }))
          << "applied up to " << om->last_event_id() << ", fake at " << lm.last_event_id();
    }

    void settle() { std::this_thread::sleep_for(150ms); }
  };
} // namespace

// ---- login and registration ---------------------------------------------------------------------

TEST_F(LimeOMTest, LogsInAndRegistersWithTheSom)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::LIME);
  EXPECT_EQ(reg->manager, om);
  auto s = lm.sessions();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].login.account, "ACCT1");
  EXPECT_EQ(s[0].login.user, "trader");
  EXPECT_EQ(s[0].login.password, "secret");
  EXPECT_EQ(s[0].login.event_id, 0u); // first session: no replay
  EXPECT_TRUE(s[0].login.cancel_all_on_disconnect);
}

TEST_F(LimeOMTest, BadConfigGivesNullNotCrash)
{
  auto c = cfg();
  c.default_route.clear();
  EXPECT_EQ(create_LimeOM(c, &somr), nullptr);
  c = cfg();
  c.password.clear();
  EXPECT_EQ(create_LimeOM(c, &somr), nullptr);
  c = cfg();
  c.algo_strategies.insert("POV"); // needs MaxParticipation, which the C++ API cannot send
  EXPECT_EQ(create_LimeOM(c, &somr), nullptr);
  c = cfg(); // good config, but this build has no SDK and sim is off
  EXPECT_EQ(create_LimeOM(c, &somr), nullptr);
  EXPECT_EQ(create_LimeOM("/nonexistent/lime.ini", &somr), nullptr);
  c.sim = true;
  auto a = create_LimeOM(c, &somr);
  EXPECT_NE(a, nullptr);
  delete a;
}

TEST_F(LimeOMTest, ConfigFileWithCredentialsFileIsRead)
{
  auto dir = std::filesystem::temp_directory_path() / "lime_om_test";
  std::filesystem::create_directories(dir);
  {
    std::ofstream cr(dir / "cred.ini");
    cr << "user = trader\npassword = secret\naccount = ACCT9\n";
    std::ofstream f(dir / "lime.ini");
    f << "venue = LIME\nsim = true\ndefault_route = INET\nalgo_route = LSS\nalgo_strategies = VWAP, TWAP\n"
      << "allow_market = true\nmax_order_qty = 500\ncredentials = " << (dir / "cred.ini").string() << "\n";
  }
  LimeOMConfig c;
  en::x v{};
  std::string err;
  ASSERT_TRUE(LimeOMConfig::from_file((dir / "lime.ini").string(), c, v, err)) << err;
  EXPECT_EQ(v, en::x::LIME);
  EXPECT_EQ(c.user, "trader");
  EXPECT_EQ(c.password, "secret");
  EXPECT_EQ(c.account, "ACCT9");
  EXPECT_EQ(c.algo_strategies, (std::set<std::string>{"VWAP", "TWAP"}));
  EXPECT_TRUE(c.allow_market);
  EXPECT_EQ(c.max_order_qty, 500);
  std::filesystem::remove_all(dir);
}

TEST_F(LimeOMTest, OrderBeforeLoginIsRejected)
{
  lm.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 1; }));
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
  lm.login();
  ASSERT_TRUE(wait_until([&] { return om->logged_in(); }));
  place(order(2));
}

TEST_F(LimeOMTest, LoginFailedStaysDown)
{
  lm.login_failure = "fake: bad password";
  start(false);
  ASSERT_TRUE(wait_until([&] { return om->down(); }));
  settle();
  EXPECT_EQ(lm.sessions().size(), 1u); // not retried
  auto r = refused(order(1));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
}

TEST_F(LimeOMTest, ConstructorThrowingIsRetried)
{
  lm.throw_on_construct = true;
  start(false);
  settle();
  EXPECT_FALSE(om->logged_in());
  lm.throw_on_construct = false;
  ASSERT_TRUE(wait_until([&] { return om->logged_in(); }));
  EXPECT_GE(om->sessions_created(), 2);
}

// The API must be built and destroyed on one thread, never a callback (TO 2).
TEST_F(LimeOMTest, SessionIsCreatedOnTheActorThread)
{
  start();
  lm.connection_failed();
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 2 && om->logged_in(); }));
  auto s = lm.sessions();
  EXPECT_EQ(s[0].created_on, s[1].created_on);
  EXPECT_NE(s[0].created_on, std::this_thread::get_id());
}

// ---- order fields ---------------------------------------------------------------------------------

TEST_F(LimeOMTest, NewOrderFields)
{
  start();
  auto o = sent(order(10, 300));
  EXPECT_EQ(o.id, om->book().find_oid(10)->cid); // the book's client id is the Lime OrderId
  EXPECT_EQ(o.route, "INET");
  EXPECT_EQ(o.symbol, "MSFT");
  EXPECT_EQ(o.side, Side::Buy);
  EXPECT_EQ(o.quantity, 300);
  EXPECT_EQ(o.price, SPX);
  EXPECT_EQ(o.tif, Tif::Day);
  EXPECT_TRUE(o.strategy.empty());
  EXPECT_FALSE(o.sale_affirm);
  EXPECT_EQ(om->book().find_oid(10)->state, State::PendingNew);
}

TEST_F(LimeOMTest, SidesAndShortSales)
{
  auto c = cfg();
  c.sale_affirm = true;
  start(true, c);
  auto s = order(11, 100, PX, en::bs::SEL);
  EXPECT_EQ(sent(s).side, Side::Sell);
  EXPECT_TRUE(lm.orders().back().sale_affirm);
  s = order(12, 100, PX, en::bs::SEL);
  s.short_sale = Order::ShortSale::SHORT;
  EXPECT_EQ(sent(s).side, Side::SellShort);
  s = order(13, 100, PX, en::bs::SEL);
  s.short_sale = Order::ShortSale::SHORT_EXEMPT;
  EXPECT_EQ(sent(s).side, Side::SellShortExempt);
  auto b = order(14);
  EXPECT_EQ(sent(b).side, Side::Buy);
  EXPECT_FALSE(lm.orders().back().sale_affirm); // only on sells
}

TEST_F(LimeOMTest, TimeInForceMapping)
{
  auto c = cfg();
  c.regular_session_only = true;
  start(true, c);
  std::pair<Order::TimeInForce, Tif> map[] = {{Order::TimeInForce::DAY, Tif::Day},
                                              {Order::TimeInForce::IOC, Tif::Ioc},
                                              {Order::TimeInForce::AT_OPEN, Tif::Opg},
                                              {Order::TimeInForce::AT_CLOSE, Tif::AtTheClose},
                                              {Order::TimeInForce::EXTENDED_DAY, Tif::ExtendedDay}};
  int oid = 20;
  for (auto [in, out] : map)
  {
    auto o = order(oid++);
    o.tif = in;
    auto s = sent(o);
    EXPECT_EQ(s.tif, out);
    EXPECT_EQ(s.regular_session_only, out == Tif::Day);
  }
}

TEST_F(LimeOMTest, RoutesAndAlgos)
{
  start();
  auto o = order(30);
  o.route = "BZXB";
  EXPECT_EQ(sent(o).route, "BZXB");
  auto a = order(31);
  a.strategy = "VWAP";
  auto s = sent(a);
  EXPECT_EQ(s.route, "LSS");
  EXPECT_EQ(s.strategy, "VWAP");
  a = order(32);
  a.strategy = "TWAP";
  a.route = "LARE";
  s = sent(a);
  EXPECT_EQ(s.route, "LARE");
  EXPECT_EQ(s.strategy, "TWAP");
}

TEST_F(LimeOMTest, MarketOrderWhenAllowed)
{
  auto c = cfg();
  c.allow_market = true;
  start(true, c);
  auto o = order(35);
  o.ord_type = Order::OrdType::MARKET;
  EXPECT_EQ(sent(o).price, MARKET_PRICE);
}

TEST_F(LimeOMTest, UnsupportedOrdersAreRefusedLocally)
{
  auto c = cfg();
  c.algo_route.clear();
  start(true, c);
  std::vector<Order> bad;
  auto o = order(40);
  o.ord_type = Order::OrdType::STOP;
  bad.push_back(o);
  o = order(41);
  o.ord_type = Order::OrdType::STOP_LIMIT;
  bad.push_back(o);
  o = order(42);
  o.ord_type = Order::OrdType::MARKET; // allow_market is off
  bad.push_back(o);
  o = order(43);
  o.tif = Order::TimeInForce::FOK;
  bad.push_back(o);
  o = order(44);
  o.tif = Order::TimeInForce::GTC;
  bad.push_back(o);
  o = order(45, 100, PX, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT;
  o.locate_id = "LOC123"; // no locate field in the C++ API
  bad.push_back(o);
  o = order(46);
  o.strategy = "POV"; // not allowed
  bad.push_back(o);
  o = order(47);
  o.strategy = "VWAP"; // allowed, but no algo route
  bad.push_back(o);
  o = order(48);
  o.oid_to_cancel = 3; // cancel/replace
  bad.push_back(o);
  o = order(49, 100, 0); // zero limit price
  bad.push_back(o);
  o = order(50);
  o.sz = 10.5;
  bad.push_back(o);
  for (size_t i = 0; i < bad.size(); ++i)
    ASSERT_NE(refused(bad[i], i), nullptr) << "order " << bad[i].oid << " was not refused";
  settle();
  EXPECT_TRUE(lm.orders().empty());
  EXPECT_EQ(om->book().size(), 0u);
}

TEST_F(LimeOMTest, ShortSalesRefusedWhenDisabled)
{
  auto c = cfg();
  c.allow_short = false;
  start(true, c);
  auto o = order(55, 100, PX, en::bs::SEL);
  o.short_sale = Order::ShortSale::SHORT_EXEMPT;
  ASSERT_NE(refused(o), nullptr);
  EXPECT_EQ(sent(order(56, 100, PX, en::bs::SEL)).side, Side::Sell);
}

TEST_F(LimeOMTest, LocalRiskLimits)
{
  auto c = cfg();
  c.max_order_qty = 1000;
  c.max_order_notional = 50'000;
  c.max_orders_per_sec = 2;
  start(true, c);
  auto r = refused(order(60, 1001));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  r = refused(order(61, 1000, 5001), 1); // $50,010
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::OVERLIMIT);
  place(order(62));
  place(order(63));
  r = refused(order(64), 2);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
}

TEST_F(LimeOMTest, DuplicateOidIsRefused)
{
  start();
  place(order(65));
  auto r = refused(order(65));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(lm.orders().size(), 1u);
}

// ---- reports ----------------------------------------------------------------------------------------

TEST_F(LimeOMTest, AckCarriesLimeOrderId)
{
  start();
  auto id = place(order(70));
  lm.accept(id, 123456789);
  auto a = somr.wait_for<som::Ack>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->id, 70u);
  EXPECT_EQ(a->xordid, 123456789u);
  EXPECT_EQ(om->book().find_oid(70)->state, State::Working);
}

TEST_F(LimeOMTest, PartialThenFullFill)
{
  start();
  auto id = place(order(71, 300));
  lm.accept(id, 77);
  lm.fill(id, 100, 401100, 200, "E71A");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->id, 71u);
  EXPECT_DOUBLE_EQ(f->sz, 100);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 200);
  EXPECT_EQ(f->pxi, 4011);
  EXPECT_EQ(f->side, en::bs::BUY);
  EXPECT_EQ(f->venue, en::x::LIME);
  EXPECT_EQ(f->xordid, 77u);
  EXPECT_EQ(f->tim, 1'700'000'000'123'456'789ULL);
  lm.fill(id, 200, 401200, 0, "E71B");
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->still_to_be_filled, 0);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(71)->state == State::Filled; }));
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

TEST_F(LimeOMTest, RejectGivesReject)
{
  start();
  auto id = place(order(72));
  lm.reject(id, "Insufficient buying power");
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->id, 72u);
  EXPECT_EQ(om->book().find_oid(72)->state, State::Rejected);
}

TEST_F(LimeOMTest, RejectFromTheApiItselfHasNoEventId)
{
  start();
  auto id = place(order(73));
  lm.api_reject(id);
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
  EXPECT_EQ(om->last_event_id(), 0u);
}

TEST_F(LimeOMTest, CancelRoundTripUsesTheOrdersOwnId)
{
  start();
  auto id = place(order(80, 300));
  lm.accept(id);
  lm.fill(id, 100, SPX, 200, "E80");
  cancel(80);
  EXPECT_EQ(lm.cancels().back(), id);
  EXPECT_EQ(om->book().find_oid(80)->state, State::PendingCancel);
  lm.cancel(id);
  auto c = somr.wait_for<som::CancAck>();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->id, 80u);
  EXPECT_EQ(c->fillsz, 100);
  EXPECT_EQ(om->book().find_oid(80)->state, State::Canceled);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
}

TEST_F(LimeOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto id = place(order(81));
  lm.accept(id);
  cancel(81);
  lm.cancel_reject(id);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_EQ(om->book().find_oid(81)->state, State::Working);
  cancel(81); // can be cancelled again
  lm.cancel(id);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(LimeOMTest, CancelOfUnknownOrFilledOrder)
{
  start();
  cancel_only(999);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::NOTFOUND);
  auto id = place(order(82));
  lm.accept(id);
  lm.fill(id, 100, SPX, 0, "E82");
  ASSERT_TRUE(wait_until([&] { return om->book().find_oid(82)->state == State::Filled; }));
  cancel_only(82);
  auto cr2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(cr2, nullptr);
  EXPECT_EQ(cr2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(lm.cancels().empty());
}

TEST_F(LimeOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto id = place(order(83));
  lm.accept(id);
  lm.cancel(id, "market close");
  auto c = somr.wait_for<som::CancAck>();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->id, 83u);
  // An IOC never acknowledged and cancelled by the market.
  auto id2 = place(order(84));
  lm.cancel(id2);
  ASSERT_NE(somr.wait_for<som::CancAck>(1), nullptr);
}

TEST_F(LimeOMTest, FillBeforeAck)
{
  start();
  auto id = place(order(85, 300));
  lm.fill(id, 100, SPX, 200, "E85A");
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 200);
  lm.accept(id, 55);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  lm.fill(id, 200, SPX, 0, "E85B");
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(85)->state, State::Filled);
}

TEST_F(LimeOMTest, FillRacingCancel)
{
  start();
  auto id = place(order(86));
  lm.accept(id);
  cancel(86);
  lm.fill(id, 100, SPX, 0, "E86");
  lm.cancel_reject(id, "too late to cancel");
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  synced();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
  EXPECT_EQ(om->book().find_oid(86)->state, State::Filled);
}

TEST_F(LimeOMTest, FillAfterCancelAckStillCounts)
{
  start();
  auto id = place(order(87));
  lm.accept(id);
  cancel(87);
  lm.cancel(id);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  lm.fill(id, 100, SPX, 0, "E87");
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck", "Fill"}));
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(87)->cum == 100; }));
}

TEST_F(LimeOMTest, BustIsNotAFill)
{
  start();
  auto id = place(order(88, 200));
  lm.accept(id);
  lm.fill(id, 100, SPX, 100, "E88");
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  lm.fill(id, 100, SPX, 100, "E88", ExecType::Bust); // same execId: the busted fill's
  synced();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(88)->cum, 100);
}

TEST_F(LimeOMTest, CorrectionIsNotAFill)
{
  start();
  auto id = place(order(89, 200));
  lm.accept(id);
  lm.fill(id, 100, SPX, 100, "E89");
  lm.fill(id, 100, SPX, 100, "E89", ExecType::Bust);
  lm.fill(id, 100, SPX - 100, 100, "E89", ExecType::Correction);
  synced();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(89)->cum, 100);
  EXPECT_EQ(om->book().find_oid(89)->state, State::Working);
}

// ---- busy connection -------------------------------------------------------------------------------

TEST_F(LimeOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  lm.busy();
  ASSERT_TRUE(wait_until([&] { return om->busy(); }));
  auto r = refused(order(90));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::THROTTLE);
  lm.available();
  ASSERT_TRUE(wait_until([&] { return !om->busy(); }));
  place(order(91));
  // A call that returns busy refuses the order and marks the connection busy.
  lm.place_status = CallStatus::ConnectionBusy;
  auto r2 = refused(order(92), 1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::THROTTLE);
  EXPECT_TRUE(om->busy());
  EXPECT_EQ(om->book().find_oid(92)->state, State::Rejected);
  lm.place_status = CallStatus::Success;
  lm.available();
  ASSERT_TRUE(wait_until([&] { return !om->busy(); }));
  place(order(93));
}

TEST_F(LimeOMTest, CancelOnABusyConnectionIsRefusedAndTheOrderStaysWorking)
{
  start();
  auto id = place(order(94));
  lm.accept(id);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  lm.cancel_status = CallStatus::ConnectionBusy;
  cancel_only(94);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::THROTTLE);
  EXPECT_EQ(om->book().find_oid(94)->state, State::Working);
}

// ---- reconnect and replay -------------------------------------------------------------------------

TEST_F(LimeOMTest, ReconnectReplaysFromTheNextEventId)
{
  start();
  auto id = place(order(101, 300));
  lm.accept(id);
  lm.fill(id, 100, SPX, 200, "E101A");
  synced();
  uint64_t last = om->last_event_id();
  lm.connection_failed();
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 2 && om->logged_in(); }));
  EXPECT_EQ(lm.sessions()[1].login.event_id, last + 1);
}

TEST_F(LimeOMTest, MissedFillIsReplayedOnceAfterReconnect)
{
  start();
  auto id = place(order(102, 300));
  lm.accept(id);
  lm.fill(id, 100, SPX, 200, "E102A");
  synced();
  // The connection drops; Lime numbers a fill no session receives.
  lm.auto_login = false;
  lm.connection_failed();
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 2; }));
  lm.fill(id, 200, SPX, 0, "E102B"); // not delivered: the new session is not logged in
  lm.login();                        // replays from last applied + 1: the missed fill
  ASSERT_TRUE(wait_until([&] { return om->book().find_oid(102)->state == State::Filled; }));
  synced();
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u);
  EXPECT_EQ(om->book().find_oid(102)->cum, 300);
}

// Lime replays more than was asked for: what was applied is skipped by eventId.
TEST_F(LimeOMTest, ReplayOverlapIsSkipped)
{
  start();
  auto id = place(order(106, 300));
  lm.accept(id);
  lm.fill(id, 100, SPX, 200, "E106A");
  synced();
  lm.replay_from_override = 1;
  lm.connection_failed();
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 2 && om->logged_in(); }));
  lm.fill(id, 200, SPX, 0, "E106B");
  synced();
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
  EXPECT_EQ(om->book().find_oid(106)->cum, 300);
}

TEST_F(LimeOMTest, ReplayedEventsAreNotAppliedTwice)
{
  start();
  auto id = place(order(103, 300));
  lm.accept(id);
  lm.fill(id, 100, SPX, 200, "E103A");
  synced();
  lm.redeliver(1); // the accept again
  lm.redeliver(2); // the fill again
  lm.fill(id, 50, SPX, 150, "E103B");
  synced();
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u);
  EXPECT_EQ(om->book().find_oid(103)->cum, 150);
}

TEST_F(LimeOMTest, SameExecIdIsAppliedOnce)
{
  start();
  auto id = place(order(104, 300));
  lm.accept(id);
  lm.fill(id, 100, SPX, 200, "E104");
  lm.fill(id, 100, SPX, 200, "E104"); // new eventId, same execution
  synced();
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(104)->cum, 100);
}

TEST_F(LimeOMTest, ReconnectBeforeAnyEventReplaysTheDay)
{
  start();
  place(order(105));
  lm.connection_failed();
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 2 && om->logged_in(); }));
  EXPECT_EQ(lm.sessions()[1].login.event_id, 1u);
}

TEST_F(LimeOMTest, FirstEventIdIsUsedOnTheFirstLogin)
{
  lm.accept(424242); // an order from an earlier process: eventId 1
  auto c = cfg();
  c.first_event_id = 1;
  start(true, c);
  synced();
  EXPECT_EQ(lm.sessions()[0].login.event_id, 1u);
  EXPECT_TRUE(somr.all<som::Ack>().empty()); // unknown order: logged, ignored
}

TEST_F(LimeOMTest, OrdersAndCancelsWhileDisconnectedAreRefused)
{
  auto c = cfg();
  c.reconnect_ms = 10'000;
  start(true, c);
  auto id = place(order(110));
  lm.accept(id);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  lm.connection_failed();
  ASSERT_TRUE(wait_until([&] { return !om->logged_in(); }));
  auto r = refused(order(111));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
  cancel_only(110);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(110)->state, State::Working);
}

TEST_F(LimeOMTest, ApiExceptionOnPlaceDropsTheSessionAndReconnects)
{
  start();
  lm.throw_on_place = true;
  auto r = refused(order(112));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::DISCONNECTED);
  lm.throw_on_place = false;
  ASSERT_TRUE(wait_until([&] { return lm.sessions().size() == 2 && om->logged_in(); }));
  place(order(113));
}

// ---- other events -----------------------------------------------------------------------------------

TEST_F(LimeOMTest, UnknownReportsAndManualOrdersAreLoggedAndIgnored)
{
  start();
  auto id = place(order(120));
  lm.accept(id);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  lm.accept(777);                // unknown order
  lm.fill(777, 100, SPX, 0, "X"); // fill on an unknown order: logged as an error
  lm.cancel(778);
  lm.reject(779);
  lm.cancel_reject(780);
  lm.manual_order(781);
  lm.manual_replace(781, 782);
  lm.replace(id, 790);           // never requested
  lm.replace_reject(id, 791);
  lm.partial_cancel(id, 50);
  synced();
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack"}));
  EXPECT_EQ(om->book().find_oid(120)->state, State::Working);
}

TEST_F(LimeOMTest, IdsAreUniqueAndIncreasingAcrossRestart)
{
  start(true, cfg(), frame::som::ClientIdGen::from_clock());
  std::vector<OrderId> ids;
  for (int i = 0; i < 3; ++i)
    ids.push_back(place(order(130 + i)));
  EXPECT_LT(ids[0], ids[1]);
  EXPECT_LT(ids[1], ids[2]);
  EXPECT_LT(ids[2], 1ULL << 63);
  stop();
  start(true, cfg(), frame::som::ClientIdGen::from_clock());
  auto id = place(order(130)); // the SOM's oids restart too
  EXPECT_GT(id, ids[2]);
}

TEST_F(LimeOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto id = place(order(140, 1'000'000));
  lm.accept(id);
  std::atomic<bool> go{true};
  std::thread venue([&] {
    int n = 0;
    while (go)
    {
      lm.fill(id, 1, SPX, 1, "S" + std::to_string(n++));
      std::this_thread::sleep_for(100us);
    }
  });
  std::this_thread::sleep_for(20ms);
  mgr->end();
  go = false;
  venue.join();
  EXPECT_FALSE(lm.connected());
  mgr.reset();
  delete om;
  om = nullptr;
}
