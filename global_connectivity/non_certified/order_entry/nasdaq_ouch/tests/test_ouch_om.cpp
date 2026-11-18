/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OuchOM against FakeOuch, with OuchOM on its own actor thread under a
// kaspar Manager as in production. "SOM" is a recorder: orders come from it,
// replies go back to it. The test thread plays the session's thread.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "FakeOuch.hpp"
#include "TestUtil.hpp"
#include "nasdaq_ouch/act/OuchOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace nasdaq::ouch;
using namespace nasdaq::ouch::test;
using frame::som::State;

namespace
{
  constexpr uint64_t PX_41_25 = 41'2500; // $41.25 in OUCH units

  class OuchOMTest : public ::testing::Test
  {
  protected:
    FakeOuchServer nasdaq;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    OuchOM *om = nullptr;
    const frame::ref::Asset *msft = nullptr;

    void SetUp() override { msft = test_stock("MSFT"); }

    static OuchOMConfig cfg()
    {
      OuchOMConfig c;
      c.session.host = "127.0.0.1"; // never used: the fake has no socket
      c.session.port = "1";
      c.username = "TEST01";
      c.password = "secret";
      c.firm = "M2TT";
      c.reconnect_ms = 50;
      return c;
    }

    void start(bool wait_ready = true, OuchOMConfig c = cfg(),
               frame::som::ClientIdGen ids = frame::som::ClientIdGen(5000))
    {
      om = new OuchOM(c, &somr, nasdaq.factory(), en::x::NASDAQ, ids);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      if (wait_ready)
      {
        ASSERT_TRUE(wait_until([&] { return om->ready(); })) << "never ready";
      }
    }

    void stop()
    {
      if (mgr)
        mgr->end(); // Shutdown + join; the Manager does not delete the actor
      mgr.reset();
      delete om;
      om = nullptr;
    }

    void TearDown() override { stop(); }

    Order order(int oid, int qty = 100, int px = 4125, en::bs side = en::bs::BUY)
    {
      Order o("EQ", en::x::NASDAQ, msft->id, qty, px, side, 4);
      o.oid = oid;
      return o;
    }

    // Send as the SOM does; wait until it reaches Nasdaq; return its UserRefNum.
    uint32_t place(const Order &o)
    {
      auto before = nasdaq.entered().size();
      om->send(new Order(o), &somr);
      EXPECT_TRUE(wait_until([&] { return nasdaq.entered().size() > before; })) << "order never reached Nasdaq";
      auto e = nasdaq.entered();
      return e.empty() ? 0 : uint32_t(e.back().msg.UserRefNum);
    }

    const som::Reject *refused(const Order &o, size_t nth = 0)
    {
      om->send(new Order(o), &somr);
      return somr.wait_for<som::Reject>(nth);
    }

    void cancel(int oid) { om->send(new som::Cancel(oid), &somr); }

    // Let in-flight reports drain before asserting that nothing more came.
    void settle() { std::this_thread::sleep_for(150ms); }
  };
} // namespace

// ---- logon and registration ---------------------------------------------------------------

TEST_F(OuchOMTest, LogsInRegistersAndAsksForTheNextUserRefNum)
{
  start();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::NASDAQ);
  EXPECT_EQ(reg->manager, om);
  auto s = nasdaq.sessions();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].logon.username, "TEST01");
  EXPECT_EQ(s[0].logon.password, "secret");
  EXPECT_EQ(s[0].logon.session, "");  // the currently active session
  EXPECT_EQ(s[0].logon.sequence, 0u); // first login: no replay
  EXPECT_EQ(nasdaq.account_queries(), 1);
  EXPECT_EQ(om->next_user_ref_num(), 1u);
}

TEST_F(OuchOMTest, BadConfigGivesNullNotCrash)
{
  auto c = cfg();
  c.session.host.clear();
  EXPECT_EQ(create_OuchOM(c, &somr, nasdaq.factory()), nullptr);
  c = cfg();
  c.username = "TOOLONGNAME";
  EXPECT_EQ(create_OuchOM(c, &somr, nasdaq.factory()), nullptr);
  c = cfg();
  c.capacity = 'X';
  EXPECT_EQ(create_OuchOM(c, &somr, nasdaq.factory()), nullptr);
  c = cfg();
  c.firm = "TOOLONG";
  EXPECT_EQ(create_OuchOM(c, &somr, nasdaq.factory()), nullptr);
  EXPECT_EQ(create_OuchOM(std::string("/nonexistent/ouch.ini"), &somr), nullptr);

  auto ok = create_OuchOM(cfg(), &somr, nasdaq.factory());
  EXPECT_NE(ok, nullptr);
  delete ok;
}

TEST_F(OuchOMTest, ConfigFileWithCredentialsFileIsRead)
{
  auto dir = std::filesystem::temp_directory_path();
  auto cred = (dir / "ouch_test_cred.ini").string();
  auto ini = (dir / "ouch_test.ini").string();
  std::ofstream(cred) << "username = ABC123\npassword = pw\n";
  std::ofstream(ini) << "host = 10.0.0.1\nport = 9000\ncredentials = " << cred
                     << "\nfirm = M2TT\ncapacity = P\nmax_order_qty = 500\n";
  OuchOMConfig c;
  std::string err;
  ASSERT_TRUE(OuchOMConfig::from_file(ini, c, err)) << err;
  EXPECT_EQ(c.username, "ABC123");
  EXPECT_EQ(c.password, "pw");
  EXPECT_EQ(c.session.port, "9000");
  EXPECT_EQ(c.capacity, 'P');
  EXPECT_EQ(c.max_order_qty, 500);
  std::ofstream(ini) << "host = 10.0.0.1\nport = 9000\ncredentials = " << cred << "\ncapacity = Q\n";
  EXPECT_FALSE(OuchOMConfig::from_file(ini, c, err));
}

TEST_F(OuchOMTest, OrderBeforeLoginIsRejected)
{
  nasdaq.auto_login = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return nasdaq.sessions().size() == 1; }));
  auto rej = refused(order(1));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_TRUE(nasdaq.entered().empty());
}

// Logged in, but Nasdaq has not yet said which UserRefNum is next: an order
// could reuse one, so none is sent.
TEST_F(OuchOMTest, OrderBeforeAccountQueryAnswerIsRejected)
{
  nasdaq.answer_account_query = false;
  start(false);
  ASSERT_TRUE(wait_until([&] { return nasdaq.account_queries() == 1; }));
  auto rej = refused(order(1));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_FALSE(om->ready());
}

// Bad credentials: retrying will not help, so no reconnect loop.
TEST_F(OuchOMTest, LoginRejectedStaysDown)
{
  nasdaq.login_reject = 'A';
  start(false);
  std::this_thread::sleep_for(300ms);
  EXPECT_FALSE(om->ready());
  EXPECT_EQ(nasdaq.sessions().size(), 1u);
  auto rej = refused(order(1));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
}

TEST_F(OuchOMTest, SessionIsCreatedOnTheActorThread)
{
  start();
  auto s = nasdaq.sessions();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_NE(s[0].created_on, std::this_thread::get_id());
}

// ---- what goes to Nasdaq ------------------------------------------------------------------

TEST_F(OuchOMTest, EnterOrderFields)
{
  start();
  auto urn = place(order(10, 300, 4125));
  auto e = nasdaq.entered().back();
  const auto &m = e.msg;
  EXPECT_EQ(urn, 1u);
  EXPECT_EQ(om->user_ref_num(10), 1u);
  EXPECT_EQ(m.Side, 'B');
  EXPECT_EQ(uint32_t(m.Quantity), 300u);
  EXPECT_EQ(m.get_Symbol(), "MSFT");
  EXPECT_EQ(std::string(m.Symbol, 8), "MSFT    "); // left-justified, space padded
  EXPECT_EQ(uint64_t(m.Price), PX_41_25);
  EXPECT_EQ(m.TimeInForce, '0');
  EXPECT_EQ(m.Display, 'Y');
  EXPECT_EQ(m.Capacity, 'A');
  EXPECT_EQ(m.InterMarketSweepEligibility, 'N');
  EXPECT_EQ(m.CrossType, 'N');
  EXPECT_EQ(m.get_ClOrdID(), "10");
  auto firm = find_option(e.appendage, TAG_FIRM);
  ASSERT_TRUE(firm.has_value());
  EXPECT_EQ(*firm, "M2TT");
  EXPECT_EQ(e.appendage.size(), 6u); // one TagValue: length 5, tag 2, 4 bytes
}

TEST_F(OuchOMTest, TimeInForceCrossTypeAndMarketOrders)
{
  start();
  struct Case
  {
    Order::TimeInForce tif;
    char ouch_tif, cross;
  } cases[] = {{Order::TimeInForce::IOC, '3', 'N'},
               {Order::TimeInForce::EXTENDED_DAY, '5', 'N'},
               {Order::TimeInForce::AT_OPEN, '0', 'O'},
               {Order::TimeInForce::AT_CLOSE, '0', 'C'}};
  int oid = 11;
  for (auto c : cases)
  {
    auto o = order(oid++);
    o.tif = c.tif;
    place(o);
    EXPECT_EQ(nasdaq.entered().back().msg.TimeInForce, c.ouch_tif);
    EXPECT_EQ(nasdaq.entered().back().msg.CrossType, c.cross);
  }
  auto m = order(20);
  m.ord_type = Order::OrdType::MARKET;
  m.tif = Order::TimeInForce::AT_CLOSE;
  place(m);
  EXPECT_EQ(uint64_t(nasdaq.entered().back().msg.Price), MARKET_PRICE);
  EXPECT_EQ(nasdaq.entered().back().msg.CrossType, 'C');

  auto r = order(21);
  r.route = "NASDAQ";
  place(r);
  EXPECT_EQ(nasdaq.entered().size(), 6u);
}

TEST_F(OuchOMTest, SidesAndShortSaleLocate)
{
  start();
  place(order(30, 100, 4125, en::bs::SEL));
  EXPECT_EQ(nasdaq.entered().back().msg.Side, 'S');
  EXPECT_FALSE(find_option(nasdaq.entered().back().appendage, TAG_SHARES_LOCATED).has_value());

  auto s = order(31, 100, 4125, en::bs::SEL);
  s.short_sale = Order::ShortSale::SHORT;
  s.locate_id = "GSCO";
  place(s);
  auto e = nasdaq.entered().back();
  EXPECT_EQ(e.msg.Side, 'T');
  EXPECT_EQ(find_option(e.appendage, TAG_SHARES_LOCATED).value_or(""), "Y");
  EXPECT_EQ(find_option(e.appendage, TAG_LOCATE_BROKER).value_or(""), "GSCO");

  auto x = order(32, 100, 4125, en::bs::SEL);
  x.short_sale = Order::ShortSale::SHORT_EXEMPT;
  place(x);
  EXPECT_EQ(nasdaq.entered().back().msg.Side, 'E');

  // A buy is never short, whatever the flag says.
  auto b = order(33);
  b.short_sale = Order::ShortSale::SHORT;
  place(b);
  EXPECT_EQ(nasdaq.entered().back().msg.Side, 'B');
}

// What OUCH cannot express, or the OM does not support, is refused before it
// leaves.
TEST_F(OuchOMTest, UnsupportedOrdersAreRefusedLocally)
{
  start();
  std::vector<Order> bad;
  auto add = [&](auto f) {
    auto o = order(40 + int(bad.size()));
    f(o);
    bad.push_back(o);
  };
  add([](Order &o) { o.ord_type = Order::OrdType::STOP_LIMIT; });
  add([](Order &o) { o.ord_type = Order::OrdType::STOP; });
  add([](Order &o) { o.tif = Order::TimeInForce::GTC; });
  add([](Order &o) { o.tif = Order::TimeInForce::FOK; });
  add([](Order &o) { o.strategy = "VWAP"; });
  add([](Order &o) { o.route = "ARCA"; });
  add([](Order &o) { o.oid_to_cancel = 7; });                                  // cancel/replace
  add([](Order &o) { o.sz = 1'000'000; });                                     // OUCH maximum is 999,999
  add([](Order &o) { o.sz = 10.5; });                                          // whole shares only
  add([](Order &o) { o.px = 20'000'000; });                                    // $200,000.00
  add([](Order &o) { o.px = 0; });
  add([](Order &o) { o.ord_type = Order::OrdType::MARKET; o.tif = Order::TimeInForce::EXTENDED_DAY; });
  add([&](Order &o) { o.sym = test_stock("TOOLONGSYM")->id; });              // symbol over 8 characters
  add([](Order &o) { o.side = en::bs::SEL; o.short_sale = Order::ShortSale::SHORT; o.locate_id = "TOOLONG"; });
  size_t n = 0;
  for (auto &o : bad)
  {
    auto rej = refused(o, n++);
    ASSERT_NE(rej, nullptr) << "order " << o.oid;
    EXPECT_EQ(rej->id, uint(o.oid));
  }
  settle();
  EXPECT_TRUE(nasdaq.entered().empty());
  EXPECT_EQ(om->book().size(), 0u);
}

TEST_F(OuchOMTest, LocalRiskLimits)
{
  auto c = cfg();
  c.max_order_qty = 500;
  c.max_order_notional = 20'000; // dollars
  c.max_orders_per_sec = 2;
  start(true, c);
  auto r1 = refused(order(50, 600));
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->reason, som::Reject::OVERLIMIT);
  auto r2 = refused(order(51, 500, 4125), 1); // 500 x $41.25 = $20,625
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::OVERLIMIT);
  place(order(52, 100));
  place(order(53, 100));
  auto r3 = refused(order(54, 100), 2); // third order in the same second
  ASSERT_NE(r3, nullptr);
  EXPECT_EQ(r3->reason, som::Reject::THROTTLE);
  EXPECT_EQ(nasdaq.entered().size(), 2u);
}

// ---- reports back to the SOM --------------------------------------------------------------

TEST_F(OuchOMTest, AckCarriesOrderReferenceNumber)
{
  start();
  auto urn = place(order(60));
  nasdaq.accept(urn, 9876543210ULL);
  auto ack = somr.wait_for<som::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->id, 60u);
  EXPECT_EQ(ack->xordid, 9876543210ULL);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(60)->state == State::Working; }));
}

TEST_F(OuchOMTest, PartialThenFullFill)
{
  start();
  auto urn = place(order(61, 100));
  nasdaq.accept(urn);
  nasdaq.fill(urn, 30, 41'2600, 1001); // $41.26
  auto f1 = somr.wait_for<som::Fill>(0);
  ASSERT_NE(f1, nullptr);
  EXPECT_EQ(f1->id, 61u);
  EXPECT_DOUBLE_EQ(f1->sz, 30);
  EXPECT_DOUBLE_EQ(f1->still_to_be_filled, 70);
  EXPECT_EQ(f1->pxi, 4126);
  EXPECT_EQ(f1->venue, en::x::NASDAQ);
  EXPECT_EQ(f1->side, en::bs::BUY);
  // Timestamp is ns since (Eastern) midnight; 9:31 is 34,260 s after it.
  EXPECT_EQ((f1->tim - eastern_midnight_epoch_ns(f1->tim)), 34'260'000'000'000ULL);
  nasdaq.fill(urn, 70, PX_41_25, 1002);
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 4125);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(61)->state == State::Filled; }));
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
}

TEST_F(OuchOMTest, RejectGivesRejectWithMappedReason)
{
  start();
  auto u1 = place(order(62));
  nasdaq.reject(u1, 0x0017); // Invalid Symbol
  auto r1 = somr.wait_for<som::Reject>(0);
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->id, 62u);
  EXPECT_EQ(r1->reason, som::Reject::UNKNOWN);
  auto u2 = place(order(63));
  nasdaq.reject(u2, 0x0029); // Risk: Port Message Rate Restriction
  auto r2 = somr.wait_for<som::Reject>(1);
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(r2->reason, som::Reject::THROTTLE);
  auto u3 = place(order(64));
  nasdaq.reject(u3, 0x0030); // Risk: Single Order Notional Exceeded
  auto r3 = somr.wait_for<som::Reject>(2);
  ASSERT_NE(r3, nullptr);
  EXPECT_EQ(r3->reason, som::Reject::EXCHLIMIT);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(64)->state == State::Rejected; }));
}

// Order State D: accepted and canceled at once; Nasdaq sends nothing more.
TEST_F(OuchOMTest, AcceptedDeadIsAckThenCancAck)
{
  start();
  auto urn = place(order(65));
  nasdaq.accept(urn, 1, 'D');
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
  EXPECT_EQ(om->book().find_oid(65)->state, State::Canceled);
}

// Broken trades change positions after the fact: logged loudly, not turned
// into fills or negative fills.
TEST_F(OuchOMTest, BrokenTradeIsNotAFill)
{
  start();
  auto urn = place(order(66));
  nasdaq.accept(urn);
  nasdaq.fill(urn, 100, PX_41_25, 2001);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  nasdaq.broken(urn, 2001);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(66)->cum, 100);
}

// ---- cancels ------------------------------------------------------------------------------

TEST_F(OuchOMTest, CancelRoundTripUsesTheOrdersUserRefNum)
{
  start();
  auto urn = place(order(70));
  nasdaq.accept(urn);
  cancel(70);
  ASSERT_TRUE(wait_until([&] { return !nasdaq.cancels().empty(); }));
  auto c = nasdaq.cancels().back();
  EXPECT_EQ(uint32_t(c.UserRefNum), urn);
  EXPECT_EQ(uint32_t(c.Quantity), 0u); // whole order
  EXPECT_EQ(nasdaq.entered().size(), 1u);     // a cancel takes no new UserRefNum
  EXPECT_EQ(om->next_user_ref_num(), urn + 1);
  nasdaq.cancel(urn, 100, 'U');
  auto ca = somr.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 70u);
  EXPECT_EQ(om->book().find_oid(70)->state, State::Canceled);
}

// Cancel Reject (only for partial cancels of cross orders in the late
// period, OUCH 5.0 3.10): the order is still working.
TEST_F(OuchOMTest, CancelRejectLeavesTheOrderWorking)
{
  start();
  auto urn = place(order(71));
  nasdaq.accept(urn);
  cancel(71);
  ASSERT_TRUE(wait_until([&] { return nasdaq.cancels().size() == 1; }));
  nasdaq.cancel_reject(urn);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->id, 71u);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(71)->state == State::Working; }));
  cancel(71); // and can be canceled again
  EXPECT_TRUE(wait_until([&] { return nasdaq.cancels().size() == 2; }));
}

// Cancel Pending: the cross order is canceled after the cross; nothing goes
// to the SOM until then.
TEST_F(OuchOMTest, CancelPendingThenCanceled)
{
  start();
  auto o = order(72);
  o.tif = Order::TimeInForce::AT_CLOSE;
  auto urn = place(o);
  nasdaq.accept(urn);
  cancel(72);
  ASSERT_TRUE(wait_until([&] { return nasdaq.cancels().size() == 1; }));
  nasdaq.cancel_pending(urn);
  settle();
  EXPECT_TRUE(somr.all<som::CancAck>().empty());
  EXPECT_TRUE(somr.all<som::CancReject>().empty());
  EXPECT_EQ(om->book().find_oid(72)->state, State::PendingCancel);
  nasdaq.cancel(urn, 100, 'U');
  EXPECT_NE(somr.wait_for<som::CancAck>(), nullptr);
}

TEST_F(OuchOMTest, CancelOfUnknownOrFilledOrder)
{
  start();
  cancel(999);
  auto c1 = somr.wait_for<som::CancReject>(0);
  ASSERT_NE(c1, nullptr);
  EXPECT_EQ(c1->reason, som::CancReject::NOTFOUND);

  auto urn = place(order(73, 10));
  nasdaq.accept(urn);
  nasdaq.fill(urn, 10, PX_41_25, 3001);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  cancel(73);
  auto c2 = somr.wait_for<som::CancReject>(1);
  ASSERT_NE(c2, nullptr);
  EXPECT_EQ(c2->reason, som::CancReject::FILLEDALREADY);
  EXPECT_TRUE(nasdaq.cancels().empty());
}

// Supervisory, timeout, end of day: cancels nobody asked for reach the SOM.
TEST_F(OuchOMTest, UnsolicitedCancelReachesTheSom)
{
  start();
  auto urn = place(order(74));
  nasdaq.accept(urn);
  nasdaq.cancel(urn, 100, 'S');
  auto ca = somr.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 74u);
  EXPECT_TRUE(nasdaq.cancels().empty());
}

// A Canceled message can take part of an order off (OUCH 5.0 3.4); the SOM
// has no partial cancel, so it hears of it only when the order ends.
TEST_F(OuchOMTest, PartialReductionByNasdaqThenFillEndsWithCancAck)
{
  start();
  auto urn = place(order(75, 100));
  nasdaq.accept(urn);
  nasdaq.aiq_cancel(urn, 30); // self-match prevention takes 30
  settle();
  EXPECT_TRUE(somr.all<som::CancAck>().empty());
  EXPECT_EQ(om->book().find_oid(75)->state, State::Working);
  nasdaq.fill(urn, 70, PX_41_25, 4001);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  auto e = om->book().find_oid(75);
  EXPECT_EQ(e->cum, 70);
  EXPECT_EQ(e->state, State::Canceled);
}

// An IOC that partly fills: Executed, then Canceled for the rest (reason I).
TEST_F(OuchOMTest, IocPartialFillThenCancel)
{
  start();
  auto o = order(76, 100);
  o.tif = Order::TimeInForce::IOC;
  auto urn = place(o);
  nasdaq.accept(urn);
  nasdaq.fill(urn, 40, PX_41_25, 4101);
  nasdaq.cancel(urn, 60, 'I');
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
}

// ---- races --------------------------------------------------------------------------------

TEST_F(OuchOMTest, FillBeforeAck)
{
  start();
  auto urn = place(order(80, 100));
  nasdaq.fill(urn, 40, PX_41_25, 5001);
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 60);
  nasdaq.accept(urn);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  nasdaq.fill(urn, 60, PX_41_25, 5002);
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(80)->state, State::Filled);
}

// The order fills completely while the cancel is in flight: Nasdaq ignores
// the cancel silently (OUCH 5.0 2.3); the order ends with its last Fill.
TEST_F(OuchOMTest, FillRacingCancel)
{
  start();
  auto urn = place(order(81, 100));
  nasdaq.accept(urn);
  cancel(81);
  ASSERT_TRUE(wait_until([&] { return nasdaq.cancels().size() == 1; }));
  nasdaq.fill(urn, 100, PX_41_25, 5101);
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->still_to_be_filled, 0);
  settle();
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
  EXPECT_EQ(om->book().find_oid(81)->state, State::Filled);
}

TEST_F(OuchOMTest, PartialFillRacingCancel)
{
  start();
  auto urn = place(order(82, 100));
  nasdaq.accept(urn);
  cancel(82);
  ASSERT_TRUE(wait_until([&] { return nasdaq.cancels().size() == 1; }));
  nasdaq.fill(urn, 40, PX_41_25, 5201);
  nasdaq.cancel(urn, 60, 'U');
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  EXPECT_EQ(om->book().find_oid(82)->cum, 40);
}

// A fill reported after the cancel ack still happened, so it counts.
TEST_F(OuchOMTest, FillAfterCancelAckStillCounts)
{
  start();
  auto urn = place(order(83, 100));
  nasdaq.accept(urn);
  cancel(83);
  ASSERT_TRUE(wait_until([&] { return nasdaq.cancels().size() == 1; }));
  nasdaq.cancel(urn, 100, 'U');
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  nasdaq.fill(urn, 40, PX_41_25, 5301);
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 40);
  EXPECT_EQ(om->book().find_oid(83)->cum, 40);
  EXPECT_EQ(somr.all<som::CancAck>().size(), 1u);
}

// ---- busy and throttled --------------------------------------------------------------------

// The session's queue is full: Reject(THROTTLE), and nothing more until the
// session says it has drained.
TEST_F(OuchOMTest, BusyConnectionThrottlesUntilAvailable)
{
  start();
  nasdaq.enter_status = CallStatus::Busy;
  auto rej = refused(order(90));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::THROTTLE);
  EXPECT_TRUE(wait_until([&] { return om->busy(); }));
  EXPECT_EQ(om->book().find_oid(90)->state, State::Rejected);

  nasdaq.enter_status = CallStatus::Success;
  auto rej2 = refused(order(91), 1);
  ASSERT_NE(rej2, nullptr);
  EXPECT_EQ(rej2->reason, som::Reject::THROTTLE); // still busy until the session says otherwise

  nasdaq.available();
  ASSERT_TRUE(wait_until([&] { return !om->busy(); }));
  EXPECT_NE(place(order(92)), 0u);
}

TEST_F(OuchOMTest, CancelOnABusyConnectionIsRefusedAndTheOrderStaysWorking)
{
  start();
  auto urn = place(order(93));
  nasdaq.accept(urn);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  nasdaq.cancel_status = CallStatus::Busy;
  cancel(93);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::THROTTLE);
  EXPECT_EQ(om->book().find_oid(93)->state, State::Working);
}

// ---- reconnect and replay ------------------------------------------------------------------

// The connection drops with an order working; a fill happens while it is
// down. The new login resumes the same SoupBinTCP session at the next
// sequence number, so the missed fill arrives exactly once and nothing
// earlier repeats.
TEST_F(OuchOMTest, ReconnectResumesAndDeliversMissedFillOnce)
{
  start();
  auto urn = place(order(100, 100));
  nasdaq.accept(urn);
  nasdaq.fill(urn, 40, PX_41_25, 6001);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  auto last = nasdaq.last_sequence();
  ASSERT_TRUE(wait_until([&] { return om->next_sequence() == last + 1; }));

  nasdaq.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  nasdaq.fill(urn, 60, PX_41_25, 6002); // while down: logged at Nasdaq, not delivered

  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  auto s = nasdaq.sessions();
  ASSERT_EQ(s.size(), 2u);
  EXPECT_EQ(s[1].logon.session, "FAKESESS01");
  EXPECT_EQ(s[1].logon.sequence, last + 1);

  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->sz, 60);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 2u);
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(100)->cum, 100);
  EXPECT_EQ(om->book().find_oid(100)->state, State::Filled);
}

// If Nasdaq replays more than asked, reports already applied are skipped by
// sequence number.
TEST_F(OuchOMTest, ReplayedReportsAreNotAppliedTwice)
{
  start();
  auto urn = place(order(101, 100));
  nasdaq.accept(urn);
  nasdaq.fill(urn, 50, PX_41_25, 6101);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  ASSERT_TRUE(wait_until([&] { return om->next_sequence() == nasdaq.last_sequence() + 1; }));

  nasdaq.replay_from_override = 1;
  nasdaq.fail_connection();
  ASSERT_TRUE(wait_until([&] { return om->ready() && om->sessions_created() == 2; }));
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(somr.all<som::Ack>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(101)->cum, 50);
}

// The same execution under a new sequence number (as after a fail-over to a
// mirror port, OUCH 5.0 1.3) is recognised by its Match Number.
TEST_F(OuchOMTest, SameMatchNumberIsAppliedOnce)
{
  start();
  auto urn = place(order(102, 100));
  nasdaq.accept(urn);
  nasdaq.fill(urn, 50, PX_41_25, 6201);
  nasdaq.fill(urn, 50, PX_41_25, 6201);
  ASSERT_NE(somr.wait_for<som::Fill>(0), nullptr);
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(om->book().find_oid(102)->cum, 50);
}

// Between the drop and the reconnect there is no session: DISCONNECTED.
// During the new login: NOTLOGGEDIN. Nothing is queued.
TEST_F(OuchOMTest, OrdersAndCancelsWhileDisconnectedAreRefused)
{
  auto c = cfg();
  c.reconnect_ms = 600;
  start(true, c);
  auto urn = place(order(103));
  nasdaq.accept(urn);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  nasdaq.auto_login = false; // the reconnect will not be answered
  nasdaq.fail_connection();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  auto rej = refused(order(104));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
  cancel(103);
  auto cr = somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::DISCONNECTED);
  EXPECT_EQ(om->book().find_oid(103)->state, State::Working);

  ASSERT_TRUE(wait_until([&] { return om->sessions_created() == 2; }));
  auto rej2 = refused(order(105), 1);
  ASSERT_NE(rej2, nullptr);
  EXPECT_EQ(rej2->reason, som::Reject::NOTLOGGEDIN);
  EXPECT_EQ(nasdaq.entered().size(), 1u);
}

// End of Session: the day is over; no reconnect.
TEST_F(OuchOMTest, EndOfSessionStopsReconnecting)
{
  start();
  nasdaq.end_of_session();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(nasdaq.sessions().size(), 1u);
  auto rej = refused(order(106));
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, som::Reject::DISCONNECTED);
}

// ---- robustness ----------------------------------------------------------------------------

// Reports for orders this process never entered, reports of a kind the OM
// never asks for, and a message type OUCH 5.0 does not define: logged,
// nothing sent to the SOM, and the OM keeps working.
TEST_F(OuchOMTest, UnknownReportsAreLoggedAndIgnored)
{
  start();
  auto before = somr.count();
  nasdaq.accept(424242);
  nasdaq.fill(424242, 10, PX_41_25, 7001);
  nasdaq.cancel(424242, 10);
  nasdaq.reject(424243, 0x0F);
  nasdaq.replaced(424242, 424244);
  nasdaq.system_event('S');
  nasdaq.raw(std::string("Y\x01\x02", 3)); // no such type
  nasdaq.raw(std::string("E\x00", 2));     // Executed, too short
  settle();
  EXPECT_EQ(somr.count(), before);
  EXPECT_EQ(om->next_sequence(), nasdaq.last_sequence() + 1); // all were counted
  auto urn = place(order(110));
  nasdaq.accept(urn);
  EXPECT_NE(somr.wait_for<som::Ack>(), nullptr);
}

// An Executed without its Appendage Length (which OUCH 5.0 does not mark
// optional there) is still applied; one cut short anywhere else is not.
TEST_F(OuchOMTest, ReportWithoutAppendageLengthIsStillApplied)
{
  start();
  auto urn = place(order(111, 100));
  nasdaq.accept(urn);
  from::OrderExecuted e{};
  e.UserRefNum = urn;
  e.Quantity = 100;
  e.Price = PX_41_25;
  e.MatchNumber = 7101;
  auto b = to_bytes(e);
  nasdaq.raw(b.substr(0, 34)); // Appendage Length missing
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 100);
  nasdaq.raw(b.substr(0, 30)); // truncated inside Match Number: not decoded
  settle();
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
}

// UserRefNums are strictly increasing, and a restarted OM starts after the
// highest one Nasdaq has seen today (Account Query), never reusing one.
// Client ids are unique across the restart too.
TEST_F(OuchOMTest, IdsAreUniqueAndIncreasingAcrossRestart)
{
  start(true, cfg(), frame::som::ClientIdGen::from_clock());
  std::vector<uint32_t> urns;
  for (int i = 0; i < 3; ++i)
    urns.push_back(place(order(120 + i)));
  EXPECT_EQ(urns, (std::vector<uint32_t>{1, 2, 3}));
  auto cid1 = om->book().find_oid(122)->cid;
  stop();

  start(true, cfg(), frame::som::ClientIdGen::from_clock());
  EXPECT_EQ(om->next_user_ref_num(), 4u);
  auto urn = place(order(120)); // the SOM's oids restart too
  EXPECT_EQ(urn, 4u);
  EXPECT_GT(om->book().find_oid(120)->cid, cid1);
}

// Reports keep arriving while the OM shuts down: shutdown completes, and
// reports after it are dropped.
TEST_F(OuchOMTest, ShutdownWithReportsInFlightCompletes)
{
  start();
  auto urn = place(order(130, 1000));
  nasdaq.accept(urn);
  std::atomic<bool> go{true};
  std::thread venue([&] {
    uint64_t match = 8000;
    while (go)
    {
      nasdaq.fill(urn, 1, PX_41_25, match++);
      std::this_thread::sleep_for(100us);
    }
  });
  std::this_thread::sleep_for(20ms);
  mgr->end();
  go = false;
  venue.join();
  EXPECT_FALSE(nasdaq.connected());
  mgr.reset();
  delete om;
  om = nullptr;
}
