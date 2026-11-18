/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CurrenexOM against FakeCurrenex, with CurrenexOM on its own actor thread
// under a kaspar Manager as in production. "SOM" is a recorder: orders come
// from it, replies go back to it. The test thread plays the session's
// thread.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeCurrenex.hpp"
#include "TestUtil.hpp"
#include "currenex_ust/act/CurrenexOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace currenex::ouch;
using namespace currenex::ouch::test;
using frame::som::State;

namespace
{
  constexpr int PX = 25345;              // 99-00+ in 1/256ths
  constexpr double RATE = 99.00390625;   // the same, as a decimal
  constexpr int64_t RATE9 = 99'003'906'250LL;
  constexpr int64_t MM = 1'000'000;      // dollars per unit (the default face_per_unit)

  class CxOMTest : public ::testing::Test
  {
  protected:
    FakeCurrenexServer cx;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    CurrenexOM *om = nullptr;
    const frame::ref::Asset *note = nullptr;  // CUSIP 91282CJY8, InstrumentIndex 7 at the fake
    const frame::ref::Asset *note5 = nullptr; // CUSIP 91282CKA8, InstrumentIndex 3

    void SetUp() override
    {
      note = test_bond("91282CJY8");
      note5 = test_bond("91282CKA8");
    }

    static CurrenexOMConfig cfg()
    {
      CurrenexOMConfig c;
      c.session.host = "127.0.0.1"; // never used: the fake has no socket
      c.session.port = "1";
      c.user = "AbcUser";
      c.password = "123pswd";
      c.reconnect_ms = 50;
      c.persistence = true;
      return c;
    }

    void start(bool wait_ready = true, CurrenexOMConfig c = cfg(),
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000), ClOrderIdGen clids = ClOrderIdGen())
    {
      om = new CurrenexOM(c, &somr, cx.factory(), en::x::SIM, ids, clids);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      if (wait_ready)
      {
        ASSERT_TRUE(wait_until([&] { return om->ready() && cx.instrument_requests() >= 1; })) << "never ready";
        synced();
      }
    }

    // Every message the fake has numbered has been applied.
    void synced()
    {
      ASSERT_TRUE(wait_until([&] { return om->next_sequence() == cx.last_sequence() + 1; }))
          << "applied up to " << om->next_sequence() << ", fake at " << cx.last_sequence();
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

    Order order(int oid, int sz = 5, int px = PX, en::bs side = en::bs::BUY, const frame::ref::Asset *a = nullptr)
    {
      Order o("UST", en::x::SIM, (a ? a : note)->id, sz, px, side, 4);
      o.oid = oid;
      return o;
    }

    // Send as the SOM does; wait until it reaches Currenex; return its ClOrderID.
    int32_t place(const Order &o)
    {
      auto before = cx.orders().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return cx.orders().size() > before; })) << "order never reached Currenex";
      auto v = cx.orders();
      return v.empty() ? 0 : int32_t(v.back().ClOrderID);
    }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }

    // Send a cancel; wait until it reaches Currenex; return its NewClOrderID.
    int32_t cancel(int oid)
    {
      auto before = cx.cancels().size();
      om->send(new som::Cancel(oid), &somr);
      EXPECT_TRUE(wait_until([&] { return cx.cancels().size() > before; })) << "cancel never reached Currenex";
      auto v = cx.cancels();
      return v.empty() ? 0 : int32_t(v.back().NewClOrderID);
    }
    void cancel_only(int oid) { om->send(new som::Cancel(oid), &somr); }

    void settle() { std::this_thread::sleep_for(150ms); }
  };
} // namespace

// ---- logon and registration ---------------------------------------------------------------

TEST_F(CxOMTest, LogsOnRegistersAndAsksForInstruments)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::SIM);
  EXPECT_EQ(reg->manager, om);
  auto s = cx.sessions();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].logon.user, "AbcUser");
  EXPECT_EQ(s[0].logon.password, "123pswd");
  EXPECT_EQ(s[0].logon.version, 200);
  EXPECT_EQ(cx.instrument_requests(), 1);
  EXPECT_EQ(cx.routing_requests(), 0); // no on-behalf-of user configured
  EXPECT_TRUE(cx.resends().empty());   // first logon: no replay asked
  EXPECT_EQ(om->instrument_index("91282CJY8"), 7);
  EXPECT_EQ(om->instrument_index("91282CKA8"), 3);
  EXPECT_EQ(om->instrument_index("912828XX0"), -1);
  EXPECT_EQ(om->routed_user_key(), -1);
}

TEST_F(CxOMTest, BadConfigGivesNullNotCrash)
{
  auto c = cfg();
  c.session.host.clear();
  EXPECT_EQ(create_CurrenexOM(c, &somr, en::x::SIM, cx.factory()), nullptr);
  c = cfg();
  c.user = std::string(21, 'u');
  EXPECT_EQ(create_CurrenexOM(c, &somr, en::x::SIM, cx.factory()), nullptr);
  c = cfg();
  c.version = 300; // the Firm LP protocol
  EXPECT_EQ(create_CurrenexOM(c, &somr, en::x::SIM, cx.factory()), nullptr);
  c = cfg();
  c.face_per_unit = 0;
  EXPECT_EQ(create_CurrenexOM(c, &somr, en::x::SIM, cx.factory()), nullptr);
  EXPECT_EQ(create_CurrenexOM(std::string("/nonexistent/currenex.ini"), &somr), nullptr);

  auto ok = create_CurrenexOM(cfg(), &somr, en::x::SIM, cx.factory());
  EXPECT_NE(ok, nullptr);
  delete ok;
}

TEST_F(CxOMTest, ConfigFileWithCredentialsFileIsRead)
{
  auto dir = std::filesystem::temp_directory_path();
  auto cred = (dir / "cx_test_cred.ini").string();
  auto ini = (dir / "cx_test.ini").string();
  std::ofstream(cred) << "user = AbcUser\npassword = 123pswd\n";
  std::ofstream(ini) << "venue = SIM\nhost = 10.0.0.1\nport = 9000\ncredentials = " << cred
                     << "\npersistence = true\nrouted_user_id = Desk2\nface_per_unit = 100000\nmax_order_face = 5000000\n";
  CurrenexOMConfig c;
  en::x v{};
  std::string err;
  ASSERT_TRUE(CurrenexOMConfig::from_file(ini, c, v, err)) << err;
  EXPECT_EQ(v, en::x::SIM);
  EXPECT_EQ(c.user, "AbcUser");
  EXPECT_EQ(c.password, "123pswd");
  EXPECT_EQ(c.session.port, "9000");
  EXPECT_TRUE(c.persistence);
  EXPECT_EQ(c.routed_user_id, "Desk2");
  EXPECT_DOUBLE_EQ(c.face_per_unit, 100000);
  EXPECT_DOUBLE_EQ(c.max_order_face, 5000000);
  std::ofstream(ini) << "venue = NOSUCHVENUE\nhost = 10.0.0.1\nport = 9000\ncredentials = " << cred << "\n";
  EXPECT_FALSE(CurrenexOMConfig::from_file(ini, c, v, err));
}

TEST_F(CxOMTest, OrderBeforeLogonIsRejected)
{
  cx.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return cx.sessions().size() == 1; }));
  auto rej = refused(order(1));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(cx.orders().empty());
}

// Authentication failure: retrying will not help, so no reconnect loop.
TEST_F(CxOMTest, LogonRefusedStaysDown)
{
  cx.logon_logout = "A5";
  start(false);
  std::this_thread::sleep_for(300ms);
  EXPECT_FALSE(om->ready());
  EXPECT_EQ(cx.sessions().size(), 1u);
  auto rej = refused(order(1));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
}

// Second missed heartbeat: the next connection may work.
TEST_F(CxOMTest, LogoutForMissedHeartbeatReconnects)
{
  start();
  cx.logout("A9");
  ASSERT_TRUE(wait_until([&] { return om->sessions_created() == 2 && om->ready(); }));
  EXPECT_EQ(cx.instrument_requests(), 2);
}

// Replaced by a new session: reconnecting would fight the other session.
TEST_F(CxOMTest, LogoutReplacedByNewSessionStaysDown)
{
  start();
  cx.logout("A1");
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(om->sessions_created(), 1);
}

TEST_F(CxOMTest, SessionIsCreatedOnTheActorThread)
{
  start();
  auto s = cx.sessions();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_NE(s[0].created_on, std::this_thread::get_id());
}

// ---- what goes to Currenex ----------------------------------------------------------------

TEST_F(CxOMTest, NewOrderFields)
{
  start();
  auto clid = place(order(10, 5, PX));
  auto m = cx.orders().back();
  EXPECT_GT(clid, 0);
  EXPECT_EQ(om->cl_order_id(10), clid);
  EXPECT_EQ(m.OrderType, 'F');                         // limit
  EXPECT_EQ(int16_t(m.InstrumentIndex), 7);            // from Instrument Info, by CUSIP
  EXPECT_EQ(m.Side, 'B');
  EXPECT_EQ(int64_t(m.OrderAmt), 5 * MM * 100);        // $5,000,000.00
  EXPECT_EQ(int64_t(m.MinAmt), 0);
  EXPECT_EQ(int64_t(m.Price), RATE9);
  EXPECT_EQ(int64_t(m.ShowAmt), 0);
  EXPECT_EQ(m.ExpireType, 'G');                        // day goes as GTC
  EXPECT_EQ(int32_t(m.RoutedUserKey), -1);
}

TEST_F(CxOMTest, TimeInForceSideInstrumentAndMinAmt)
{
  auto c = cfg();
  c.min_amt = 1'000'000;
  start(true, c);
  auto o = order(11, 2, PX, en::bs::SEL, note5);
  o.tif = Order::TimeInForce::IOC;
  place(o);
  auto m = cx.orders().back();
  EXPECT_EQ(m.ExpireType, 'I');
  EXPECT_EQ(m.Side, 'S');
  EXPECT_EQ(int16_t(m.InstrumentIndex), 3);
  EXPECT_EQ(int64_t(m.MinAmt), 1 * MM * 100);
  auto g = order(12);
  g.tif = Order::TimeInForce::GTC;
  place(g);
  EXPECT_EQ(cx.orders().back().ExpireType, 'G');
  // A CUSIP on the order wins over the asset's.
  auto x = order(13);
  x.cusip = "91282CKA8";
  place(x);
  EXPECT_EQ(int16_t(cx.orders().back().InstrumentIndex), 3);
}

TEST_F(CxOMTest, UnsupportedOrdersAreRefusedLocally)
{
  start();
  std::vector<Order> bad;
  auto add = [&](auto f) {
    auto o = order(40 + int(bad.size()));
    f(o);
    bad.push_back(o);
  };
  add([](Order &o) { o.ord_type = Order::OrdType::MARKET; });
  add([](Order &o) { o.ord_type = Order::OrdType::STOP; });
  add([](Order &o) { o.ord_type = Order::OrdType::STOP_LIMIT; });
  add([](Order &o) { o.tif = Order::TimeInForce::FOK; });
  add([](Order &o) { o.tif = Order::TimeInForce::AT_OPEN; });
  add([](Order &o) { o.tif = Order::TimeInForce::AT_CLOSE; });
  add([](Order &o) { o.tif = Order::TimeInForce::EXTENDED_DAY; });
  add([](Order &o) { o.strategy = "TWAP"; });
  add([](Order &o) { o.route = "BTEC"; });
  add([](Order &o) { o.oid_to_cancel = 7; });                    // cancel/replace
  add([](Order &o) { o.sz = 2.5; });                             // whole units only
  add([](Order &o) { o.px = 0; });
  add([](Order &o) { o.cusip = "912828ZZ9"; });                  // not listed in this session
  add([&](Order &o) { o.sym = test_bond("")->id; o.cusip = ""; }); // no CUSIP
  size_t n = 0;
  for (auto &o : bad)
  {
    auto rej = refused(o, n++);
    ASSERT_NE(rej, nullptr) << "order " << o.oid;
    EXPECT_EQ(rej->id, uint(o.oid));
  }
  settle();
  EXPECT_TRUE(cx.orders().empty());
  EXPECT_EQ(om->book().size(), 0u);
}

// A CUSIP listed twice (two settlement dates, say) is ambiguous.
TEST_F(CxOMTest, CusipListedTwiceIsRefused)
{
  cx.instruments = {{"91282CJY8", 7}, {"91282CJY8", 8}, {"91282CKA8", 3}};
  start();
  EXPECT_EQ(om->instrument_index("91282CJY8"), -2);
  auto rej = refused(order(45));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::UNKNOWN);
  place(order(46, 1, PX, en::bs::BUY, note5));
  EXPECT_EQ(cx.orders().size(), 1u);
}

TEST_F(CxOMTest, LocalRiskLimits)
{
  auto c = cfg();
  c.max_order_face = 10'000'000;
  c.max_orders_per_sec = 2;
  start(true, c);
  auto r1 = refused(order(50, 11));
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->reason, som::Reject::OVERLIMIT);
  place(order(51, 10));
  place(order(52, 1));
  auto r2 = refused(order(53, 1), 1); // third order in the same second
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::THROTTLE);
  EXPECT_EQ(cx.orders().size(), 2u);
}

// On-behalf-of: no order until the routed user's key is known; then every
// order carries it.
TEST_F(CxOMTest, RoutedUserKeyIsLookedUp)
{
  auto c = cfg();
  c.routed_user_id = "Desk2";
  cx.routed_users = {{"AbcUser", 1000}, {"Desk2", 1329}};
  start(true, c);
  EXPECT_EQ(cx.routing_requests(), 1);
  EXPECT_EQ(om->routed_user_key(), 1329);
  place(order(55));
  EXPECT_EQ(int32_t(cx.orders().back().RoutedUserKey), 1329);
  auto clid = cx.orders().back().ClOrderID;
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cancel(55);
  EXPECT_EQ(int32_t(cx.cancels().back().RoutedUserKey), 1329);
}

TEST_F(CxOMTest, RoutedUserNotListedMeansNotReady)
{
  auto c = cfg();
  c.routed_user_id = "Desk9";
  cx.routed_users = {{"AbcUser", 1000}};
  start(false, c);
  ASSERT_TRUE(wait_until([&] { return cx.routing_requests() == 1; }));
  synced();
  EXPECT_FALSE(om->ready());
  auto rej = refused(order(56));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::NOTLOGGEDIN);
}

// ---- reports back to the SOM --------------------------------------------------------------

TEST_F(CxOMTest, AckCarriesCurrenexOrderId)
{
  start();
  auto clid = place(order(60));
  cx.ack(clid, 10411178932LL);
  auto ack = somr.wait_for<som::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->id, 60u);
  EXPECT_EQ(ack->xordid, 10411178932ULL);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(60)->state == State::Working; }));
}

TEST_F(CxOMTest, PartialThenFullFill)
{
  start();
  auto clid = place(order(61, 5));
  cx.ack(clid);
  cx.trade(clid, 2'000'000, RATE, "E61A", 3'000'000, 1'700'000'000'123LL);
  auto f1 = somr.wait_for<som::Fill>(0);
  ASSERT_NE(f1, nullptr);
  EXPECT_EQ(f1->id, 61u);
  EXPECT_DOUBLE_EQ(f1->sz, 2);
  EXPECT_DOUBLE_EQ(f1->still_to_be_filled, 3);
  EXPECT_EQ(f1->pxi, PX);
  EXPECT_EQ(f1->venue, en::x::SIM);
  EXPECT_EQ(f1->side, en::bs::BUY);
  EXPECT_EQ(f1->tim, 1'700'000'000'123'000'000ULL); // TransactTime, ms since epoch
  cx.trade(clid, 3'000'000, 99.0, "E61B", 0);
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 99 * 256);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(61)->state == State::Filled; }));
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

// Trades smaller than a unit reach the SOM with their size; the book counts
// whole units and the order ends filled.
TEST_F(CxOMTest, FractionalFillsAddUpToWholeUnits)
{
  start();
  auto clid = place(order(62, 1));
  cx.ack(clid);
  for (int i = 0; i < 4; ++i)
    cx.trade(clid, 250'000, RATE, "E62" + std::to_string(i), 750'000 - 250'000 * i);
  ASSERT_NE(somr.wait_for<som::Fill>(3), nullptr);
  auto fills = somr.all<som::Fill>();
  for (int i = 0; i < 4; ++i)
  {
    EXPECT_DOUBLE_EQ(fills[i]->sz, 0.25);
    EXPECT_DOUBLE_EQ(fills[i]->still_to_be_filled, 0.75 - 0.25 * i);
  }
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(62)->state == State::Filled; }));
  EXPECT_EQ(om->book().find_oid(62)->cum, 1);
}

TEST_F(CxOMTest, RejectGivesRejectWithMappedReason)
{
  start();
  auto c1 = place(order(63));
  cx.reject(c1, 0x01); // Invalid Instrument
  auto r1 = somr.wait_for<som::Reject>(0);
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->id, 63u);
  EXPECT_EQ(r1->reason, som::Reject::UNKNOWN);
  auto c2 = place(order(64));
  cx.reject(c2, 0x0c); // Maximum Permitted Operations Exceeded
  auto r2 = somr.wait_for<som::Reject>(1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::THROTTLE);
  auto c3 = place(order(65));
  cx.reject(c3, 0x1a); // Absolute Trading Limit Exceeded
  auto r3 = somr.wait_for<som::Reject>(2);
  ASSERT_NE(r3, nullptr);
  EXPECT_EQ(r3->reason, som::Reject::EXCHLIMIT);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(65)->state == State::Rejected; }));
}

// The pending fill workflow (UST OUCH 8.5): the PendingFill is not a fill;
// the trade after it is, once.
TEST_F(CxOMTest, PendingFillThenTradeIsOneFill)
{
  start();
  auto clid = place(order(66, 5));
  cx.ack(clid);
  cx.pending(clid, 21, 5'000'000, RATE);
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
  cx.pending_trade(clid, 21, 5'000'000, RATE, "PT66", 0);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(66)->state == State::Filled; }));
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
}

TEST_F(CxOMTest, PendingFillCancelIsNotAFill)
{
  start();
  auto clid = place(order(67, 5));
  cx.ack(clid);
  cx.pending(clid, 21, 5'000'000, RATE);
  cx.pending_cancel(clid, 21, 5'000'000, RATE);
  settle();
  EXPECT_TRUE(somr.all<som::Fill>().empty());
  EXPECT_EQ(om->book().find_oid(67)->state, State::Working);
  // It matches again (8.5.2).
  cx.pending(clid, 22, 5'000'000, RATE);
  cx.pending_trade(clid, 22, 5'000'000, RATE, "PT67", 0);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(67)->state == State::Filled; }));
}

// LeavesAmt 0 while another pending fill is unsettled does not end the order.
TEST_F(CxOMTest, NothingLeftButAPendingFillIsNotACancel)
{
  start();
  auto clid = place(order(68, 5));
  cx.ack(clid);
  cx.pending(clid, 21, 2'000'000, RATE);
  cx.pending(clid, 22, 3'000'000, RATE);
  cx.pending_trade(clid, 21, 2'000'000, RATE, "PT68A", 0);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  settle();
  EXPECT_TRUE(somr.all<som::CancAck>().empty());
  cx.pending_trade(clid, 22, 3'000'000, RATE, "PT68B", 0);
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(68)->state == State::Filled; }));
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

// ---- cancels ------------------------------------------------------------------------------

// The cancel takes a ClOrderID of its own and names the order by its
// ClOrderID (UST OUCH 10.11).
TEST_F(CxOMTest, CancelRoundTrip)
{
  start();
  auto clid = place(order(70));
  cx.ack(clid);
  auto cancel_id = cancel(70);
  auto q = cx.cancels().back();
  EXPECT_GT(cancel_id, clid);
  EXPECT_EQ(int32_t(q.PrevClOrderID), clid);
  EXPECT_EQ(int16_t(q.InstrumentIndex), 7);
  EXPECT_EQ(int32_t(q.RoutedUserKey), -1);
  EXPECT_EQ(om->book().find_oid(70)->state, State::PendingCancel);
  cx.cancelled(clid, 'C', 0);
  auto ca = somr.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 70u);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(70)->state == State::Canceled; }));
}

// Whichever id the Unsolicited Cancel carries, the order's or the cancel's,
// it resolves to the order.
TEST_F(CxOMTest, CancelAckCarryingTheCancelsIdResolves)
{
  start();
  auto clid = place(order(71));
  cx.ack(clid);
  auto cancel_id = cancel(71);
  cx.cancelled(cancel_id, 'C', 0);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.all<som::CancAck>()[0]->id, 71u);
}

TEST_F(CxOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto clid = place(order(72));
  cx.ack(clid);
  auto c1 = cancel(72);
  cx.cancel_reject(c1, clid, 0x0e);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->id, 72u);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(72)->state == State::Working; }));
  auto c2 = cancel(72); // a second cancel takes another id
  EXPECT_NE(c2, c1);
  cx.cancelled(clid);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(CxOMTest, CancelOfUnknownOrFilledOrder)
{
  start();
  cancel_only(999);
  auto r1 = somr.wait_for<som::CancReject>(0);
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->reason, som::CancReject::NOTFOUND);
  auto clid = place(order(73, 1));
  cx.ack(clid);
  cx.trade(clid, 1'000'000, RATE, "E73", 0);
  ASSERT_TRUE(wait_until([&] { return om->book().find_oid(73)->state == State::Filled; }));
  cancel_only(73);
  auto r2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(cx.cancels().empty());
}

// System cancel, residual below the minimum (8.6), expiry: each reaches the SOM.
TEST_F(CxOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  struct Case
  {
    char status;
    int16_t type;
  } cases[] = {{'C', 1}, {'C', 2}, {'E', 0}};
  int oid = 74;
  size_t n = 0;
  for (auto c : cases)
  {
    auto clid = place(order(oid));
    cx.ack(clid);
    cx.cancelled(clid, c.status, c.type);
    auto ca = somr.wait_for<som::CancAck>(n++);
    ASSERT_NE(ca, nullptr);
    EXPECT_EQ(ca->id, uint(oid));
    ++oid;
  }
}

// Residual below the minimum after partial fills: the partial fills count,
// the rest is cancelled.
TEST_F(CxOMTest, PartialFillThenResidualCancel)
{
  start();
  auto clid = place(order(78, 5));
  cx.ack(clid);
  cx.trade(clid, 4'999'000, RATE, "E78", 1'000);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  cx.cancelled(clid, 'C', 2);
  auto ca = somr.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->fillsz, 4); // whole units filled
}

TEST_F(CxOMTest, IocPartialFillThenCancel)
{
  start();
  auto o = order(79, 5);
  o.tif = Order::TimeInForce::IOC;
  auto clid = place(o);
  cx.ack(clid);
  cx.trade(clid, 2'000'000, RATE, "E79", 0); // nothing left open
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  cx.cancelled(clid, 'C', 1); // the cancel for the rest: already done
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  EXPECT_EQ(om->book().find_oid(79)->cum, 2);
}

TEST_F(CxOMTest, FillBeforeAck)
{
  start();
  auto clid = place(order(80, 5));
  cx.trade(clid, 2'000'000, RATE, "E80A", 3'000'000);
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 3);
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cx.trade(clid, 3'000'000, RATE, "E80B", 0);
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(80)->state, State::Filled);
}

TEST_F(CxOMTest, FillRacingCancel)
{
  start();
  auto clid = place(order(81, 5));
  cx.ack(clid);
  auto c = cancel(81);
  cx.trade(clid, 5'000'000, RATE, "E81", 0);
  cx.cancel_reject(c, clid, 0x19); // Cannot Cancel Already Filled
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
  EXPECT_EQ(om->book().find_oid(81)->state, State::Filled);
}

TEST_F(CxOMTest, PartialFillRacingCancel)
{
  start();
  auto clid = place(order(82, 5));
  cx.ack(clid);
  cancel(82);
  cx.trade(clid, 2'000'000, RATE, "E82", 3'000'000);
  cx.cancelled(clid);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  EXPECT_EQ(somr.all<som::CancAck>()[0]->fillsz, 2);
}

// A match just before a cancel can still fill (8.7): it counts.
TEST_F(CxOMTest, FillAfterCancelAckStillCounts)
{
  start();
  auto clid = place(order(83, 5));
  cx.ack(clid);
  cancel(83);
  cx.cancelled(clid);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  cx.trade(clid, 5'000'000, RATE, "E83", 0);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck", "Fill"}));
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(83)->cum == 5; }));
}

// OUCH has no bust or correction message; a replace this OM never asked for
// is logged, not applied.
TEST_F(CxOMTest, ReplaceAckNeverRequestedIsNotApplied)
{
  start();
  auto clid = place(order(84));
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cx.replace_ack(clid + 1000, clid, 'P');
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack"}));
  EXPECT_EQ(om->book().find_oid(84)->state, State::Working);
  EXPECT_EQ(om->book().find_oid(84)->qty, 5);
}

// ---- busy connection ----------------------------------------------------------------------

TEST_F(CxOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  cx.busy();
  ASSERT_TRUE(wait_until([&] { return om->busy(); }));
  auto rej = refused(order(90));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::THROTTLE);
  cx.available();
  ASSERT_TRUE(wait_until([&] { return !om->busy(); }));
  place(order(91));
  // A send that returns Busy refuses the order and marks the connection busy.
  cx.order_status = CallStatus::Busy;
  auto rej2 = refused(order(92), 1);
  ASSERT_NE(rej2, nullptr);
  EXPECT_EQ(rej2->reason, som::Reject::THROTTLE);
  EXPECT_TRUE(om->busy());
  EXPECT_EQ(om->book().find_oid(92)->state, State::Rejected);
}

TEST_F(CxOMTest, CancelOnABusyConnectionIsRefusedAndTheOrderStaysWorking)
{
  start();
  auto clid = place(order(93));
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cx.cancel_status = CallStatus::Busy;
  cancel_only(93);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::THROTTLE);
  EXPECT_EQ(om->book().find_oid(93)->state, State::Working);
}

// ---- sequence numbers, reconnect and replay -----------------------------------------------

// Heartbeats are numbered; a GapFill moves the expected number on.
TEST_F(CxOMTest, HeartbeatsAndGapFillsAreCounted)
{
  start();
  cx.heartbeat();
  cx.heartbeat();
  synced();
  auto last = cx.last_sequence();
  cx.gap_fill_now(last + 5, '2'); // NOT_AVAILABLE: logged as an error
  ASSERT_TRUE(wait_until([&] { return om->next_sequence() == last + 5; }));
}

// Persistence: a fill while disconnected is replayed once after the
// ResendRequest; Heartbeats and the Logon come back as GapFills.
TEST_F(CxOMTest, ReconnectWithPersistenceReplaysTheMissedFillOnce)
{
  start();
  auto clid = place(order(100, 5));
  cx.ack(clid);
  cx.trade(clid, 2'000'000, RATE, "E100A", 3'000'000);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  cx.heartbeat();
  synced();
  auto applied = cx.last_sequence();

  cx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  cx.heartbeat();                                   // while down: logged at Currenex
  cx.trade(clid, 3'000'000, RATE, "E100B", 0);      // while down: logged at Currenex
  cx.heartbeat();

  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->sz, 3);
  EXPECT_EQ(cx.resends(), (std::vector<uint32_t>{applied + 1}));
  synced();
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u);
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(100)->state, State::Filled);
}

// Nothing missed: no ResendRequest.
TEST_F(CxOMTest, ReconnectWithNothingMissedAsksForNoReplay)
{
  start();
  cx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  synced();
  EXPECT_TRUE(cx.resends().empty());
}

// If Currenex replays more than asked, what was applied is skipped by
// sequence number.
TEST_F(CxOMTest, ReplayedReportsAreNotAppliedTwice)
{
  start();
  auto clid = place(order(101, 5));
  cx.ack(clid);
  cx.trade(clid, 2'000'000, RATE, "E101", 3'000'000);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  synced();
  cx.resend_from_override = 1;
  cx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  cx.heartbeat(); // a gap, so a ResendRequest is sent
  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  synced();
  settle();
  EXPECT_EQ(cx.resends().size(), 1u);
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(101)->cum, 2);
}

// The same trade under another sequence number is recognised by its
// ExecutionID.
TEST_F(CxOMTest, SameExecutionIdIsAppliedOnce)
{
  start();
  auto clid = place(order(102, 5));
  cx.ack(clid);
  cx.trade(clid, 2'000'000, RATE, "E102", 3'000'000);
  cx.trade(clid, 2'000'000, RATE, "E102", 3'000'000);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  synced();
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(102)->cum, 2);
}

// Without persistence Currenex numbers from 1 again: what happened while
// disconnected is unknown, and live orders were cancelled on the
// disconnect (8.7).
TEST_F(CxOMTest, ReconnectWithoutPersistenceCancelsLiveOrders)
{
  cx.persistence = false;
  auto c = cfg();
  c.persistence = false;
  start(true, c);
  auto clid = place(order(103, 5));
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  synced();
  cx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  auto ca = somr.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 103u);
  EXPECT_TRUE(cx.resends().empty());
  synced();
}

// Instrument indexes are valid for one session (UST OUCH 6): after a
// reconnect, orders and cancels use the new session's.
TEST_F(CxOMTest, InstrumentIndexIsPerSession)
{
  start();
  auto clid = place(order(104));
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cx.instruments = {{"91282CJY8", 9}, {"91282CKA8", 3}};
  cx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  synced();
  EXPECT_EQ(om->instrument_index("91282CJY8"), 9);
  cancel(104);
  EXPECT_EQ(int16_t(cx.cancels().back().InstrumentIndex), 9);
  place(order(105));
  EXPECT_EQ(int16_t(cx.orders().back().InstrumentIndex), 9);
}

// The first logon of a process can ask for the week's replay.
TEST_F(CxOMTest, FirstResendFromIsRequestedOnTheFirstLogon)
{
  cx.heartbeat(); // numbered before this process logs on
  cx.heartbeat();
  auto c = cfg();
  c.first_resend_from = 1;
  start(true, c);
  EXPECT_EQ(cx.resends(), (std::vector<uint32_t>{1}));
}

// Between the drop and the reconnect there is no session: DISCONNECTED.
// During the new logon: NOTLOGGEDIN. Nothing is queued.
TEST_F(CxOMTest, OrdersAndCancelsWhileDisconnectedAreRefused)
{
  auto c = cfg();
  c.reconnect_ms = 600;
  start(true, c);
  auto clid = place(order(106));
  cx.ack(clid);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  cx.auto_login = false;
  cx.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto rej = refused(order(107));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
  cancel_only(106);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(106)->state, State::Working);

  ASSERT_TRUE(wait_until([&] { return om->sessions_created() == 2; }));
  auto rej2 = refused(order(108), 1);
  ASSERT_NE(rej2, nullptr);
  EXPECT_EQ(rej2->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_EQ(cx.orders().size(), 1u);
}

// ---- robustness ----------------------------------------------------------------------------

// Reports for orders this process never entered, spread trades it never
// asked for, and a message type the table does not list: logged, nothing
// sent to the SOM, all numbered, and the OM keeps working.
TEST_F(CxOMTest, UnknownReportsAreLoggedAndIgnored)
{
  start();
  auto before = somr.count();
  cx.ack(424242);
  cx.trade(424242, 1'000'000, RATE, "X1", 0);
  cx.cancelled(424242);
  cx.reject(424243, 0x01);
  cx.cancel_reject(424244, 424242, 0x0e);
  cx.pending(424242, 1, 1'000'000, RATE);
  cx.spread_trade(424242, 10);
  cx.replace_ack(424245, 424242, 'P');
  cx.raw(std::string("Z\x01\x02", 3)); // no such type
  synced();
  settle();
  EXPECT_EQ(somr.count(), before);
  auto clid = place(order(110));
  cx.ack(clid);
  EXPECT_NE(somr.wait_for<som::Ack>(), nullptr);
}

// ClOrderIDs are strictly increasing, and a restarted OM, later in the
// week, never reuses one. Client ids are unique across the restart too.
TEST_F(CxOMTest, IdsAreUniqueAndIncreasingAcrossRestart)
{
  start(true, cfg(), frame::som::ClientIdGen::from_clock());
  std::vector<int32_t> ids;
  for (int i = 0; i < 3; ++i)
    ids.push_back(place(order(120 + i)));
  EXPECT_LT(ids[0], ids[1]);
  EXPECT_LT(ids[1], ids[2]);
  auto cid1 = om->book().find_oid(122)->cid;
  stop();

  start(true, cfg(), frame::som::ClientIdGen::from_clock());
  auto id = place(order(120)); // the SOM's oids restart too
  EXPECT_GT(id, ids[2]);
  EXPECT_GT(om->book().find_oid(120)->cid, cid1);
}

// Reports keep arriving while the OM shuts down: shutdown completes.
TEST_F(CxOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto clid = place(order(130, 1000));
  cx.ack(clid);
  std::atomic<bool> go{true};
  std::thread venue([&] {
    int n = 0;
    while (go)
    {
      cx.trade(clid, 1'000, RATE, "S" + std::to_string(n++), 1);
      std::this_thread::sleep_for(100us);
    }
  });
  std::this_thread::sleep_for(20ms);
  mgr->end();
  go = false;
  venue.join();
  EXPECT_FALSE(cx.connected());
  mgr.reset();
  delete om;
  om = nullptr;
}
