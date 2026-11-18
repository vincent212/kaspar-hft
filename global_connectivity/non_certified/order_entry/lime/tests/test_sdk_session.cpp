/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SdkSession built against sdk_doc/LimeTradingApiDoc.hh (the public
// overview's declarations), with tests/SdkStub.cpp standing in for the
// vendor library. Checks the calls SdkSession makes and the callbacks it
// forwards; then LimeOM over SdkSession end to end.

#include <gtest/gtest.h>

#include <mutex>
#include <vector>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "SdkStub.hpp"
#include "TestUtil.hpp"
#include "lime_om/act/LimeOM.hpp"

namespace som = frame::som::msg;
namespace sdk = LimeBrokerage;
using namespace lime;
using namespace lime::test;

namespace
{
  // Records SessionEvents as strings.
  struct Events : SessionEvents
  {
    std::mutex mu;
    std::vector<std::string> got;
    FillInfo last_fill;
    void add(std::string s)
    {
      std::lock_guard<std::mutex> l(mu);
      got.push_back(std::move(s));
    }
    void login_accepted(uint64_t e) override { add("login " + std::to_string(e)); }
    void login_failed(const std::string &r) override { add("login_failed " + r); }
    void connection_failed(const std::string &r) override { add("connection_failed " + r); }
    void busy() override { add("busy"); }
    void available() override { add("available"); }
    void accepted(OrderId id, OrderId lid, uint64_t e) override
    {
      add("accepted " + std::to_string(id) + " " + std::to_string(lid) + " " + std::to_string(e));
    }
    void filled(OrderId id, const FillInfo &f, uint64_t e) override
    {
      {
        std::lock_guard<std::mutex> l(mu);
        last_fill = f;
      }
      add("filled " + std::to_string(id) + " " + to_string(f.exec_type) + " " + std::to_string(e));
    }
    void canceled(OrderId id, const std::string &r, uint64_t e) override
    {
      add("canceled " + std::to_string(id) + " '" + r + "' " + std::to_string(e));
    }
    void partial_canceled(OrderId id, int left, uint64_t e) override
    {
      add("partial " + std::to_string(id) + " " + std::to_string(left) + " " + std::to_string(e));
    }
    void replaced(OrderId id, OrderId rid, OrderId, uint64_t e) override
    {
      add("replaced " + std::to_string(id) + " " + std::to_string(rid) + " " + std::to_string(e));
    }
    void rejected(OrderId id, const std::string &r, uint64_t e) override
    {
      add("rejected " + std::to_string(id) + " " + r + " " + std::to_string(e));
    }
    void cancel_rejected(OrderId id, const std::string &r, uint64_t e) override
    {
      add("cancel_rejected " + std::to_string(id) + " " + r + " " + std::to_string(e));
    }
    void replace_rejected(OrderId id, OrderId rid, const std::string &, uint64_t e) override
    {
      add("replace_rejected " + std::to_string(id) + " " + std::to_string(rid) + " " + std::to_string(e));
    }
    void manual_order(OrderId id, const std::string &s, const std::string &r, int q, ScaledPrice p, Side side,
                      uint64_t e) override
    {
      add("manual " + std::to_string(id) + " " + s + " " + r + " " + std::to_string(q) + " " + std::to_string(p) +
          " " + to_string(side) + " " + std::to_string(e));
    }
    void manual_replace(OrderId id, OrderId rid, int q, ScaledPrice p, uint64_t e) override
    {
      add("manual_replace " + std::to_string(id) + " " + std::to_string(rid) + " " + std::to_string(q) + " " +
          std::to_string(p) + " " + std::to_string(e));
    }
  };

  sdk::Listener *listener()
  {
    std::lock_guard<std::mutex> l(sdkstub::state().mu);
    return sdkstub::state().listener;
  }
  std::vector<sdkstub::Call> calls()
  {
    std::lock_guard<std::mutex> l(sdkstub::state().mu);
    return sdkstub::state().calls;
  }

  class SdkSessionTest : public ::testing::Test
  {
  protected:
    void SetUp() override { sdkstub::reset(); }
    LoginParams login()
    {
      LoginParams p;
      p.account = "ACCT1";
      p.user = "trader";
      p.password = "secret";
      p.event_id = 42;
      p.cancel_all_on_disconnect = true;
      p.host = "lime-gw";
      return p;
    }
  };
}

TEST_F(SdkSessionTest, ConstructorArguments)
{
  ASSERT_TRUE(sdk_available());
  Events ev;
  auto s = sdk_session_factory()(login(), ev);
  auto &c = sdkstub::state().ctors;
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].account, "ACCT1");
  EXPECT_EQ(c[0].user, "trader");
  EXPECT_EQ(c[0].password, "secret");
  EXPECT_EQ(c[0].event_id, 42u);
  EXPECT_TRUE(c[0].cancel_all);
  EXPECT_EQ(c[0].host, "lime-gw");
  EXPECT_EQ(c[0].transport, sdk::transportTypeTcp);
  s.reset();
  EXPECT_EQ(sdkstub::state().dtors.size(), 1u);
}

TEST_F(SdkSessionTest, ConstructorExceptionBecomesStdException)
{
  sdkstub::state().throw_in_ctor = true;
  Events ev;
  EXPECT_THROW(sdk_session_factory()(login(), ev), std::runtime_error);
}

TEST_F(SdkSessionTest, PlaceOrderArguments)
{
  Events ev;
  auto s = sdk_session_factory()(login(), ev);
  NewOrder o;
  o.id = 77;
  o.route = "INET";
  o.symbol = "MSFT";
  o.side = Side::SellShortExempt;
  o.quantity = 300;
  o.price = 401200;
  o.tif = Tif::Ioc;
  o.sale_affirm = true;
  EXPECT_EQ(s->place(o), CallStatus::Success);
  o.id = 78;
  o.price = MARKET_PRICE;
  o.tif = Tif::AtTheClose;
  o.side = Side::SellShort;
  o.sale_affirm = false;
  o.regular_session_only = true;
  s->place(o);
  o.id = 79;
  o.strategy = "VWAP";
  o.route = "LSS";
  o.tif = Tif::AtTheClose;
  s->place(o);
  EXPECT_EQ(s->cancel(77), CallStatus::Success);
  auto c = calls();
  ASSERT_EQ(c.size(), 4u);
  EXPECT_EQ(c[0].what, "placeOrder");
  EXPECT_EQ(c[0].id, 77u);
  EXPECT_EQ(c[0].route, "INET");
  EXPECT_EQ(c[0].symbol, "MSFT");
  EXPECT_EQ(c[0].side, sdk::sideSellShortExempt);
  EXPECT_EQ(c[0].quantity, 300);
  EXPECT_EQ(c[0].price, 401200);
  EXPECT_EQ(c[0].tif, sdk::OrderProperties::timeInForceIoc);
  EXPECT_TRUE(c[0].sale_affirm);
  EXPECT_EQ(c[1].price, sdk::marketPrice);
  EXPECT_EQ(c[1].tif, sdk::OrderProperties::timeInForceAtTheClose);
  EXPECT_EQ(c[1].side, sdk::sideSellShort);
  EXPECT_TRUE(c[1].regular_session_only);
  EXPECT_EQ(c[2].what, "placeAlgoOrder");
  EXPECT_EQ(c[2].strategy, "VWAP");
  EXPECT_EQ(c[2].route, "LSS");
  EXPECT_EQ(c[2].tif, sdk::AlgoOrderProperties::timeinForceAtTheClose);
  EXPECT_EQ(c[3].what, "cancelOrder");
  EXPECT_EQ(c[3].id, 77u);
}

TEST_F(SdkSessionTest, CallStatusAndExceptions)
{
  Events ev;
  auto s = sdk_session_factory()(login(), ev);
  NewOrder o;
  o.id = 1;
  sdkstub::state().status = sdk::statusConnectionBusy;
  EXPECT_EQ(s->place(o), CallStatus::ConnectionBusy);
  sdkstub::state().status = sdk::statusConnectionError;
  EXPECT_EQ(s->cancel(1), CallStatus::ConnectionError);
  sdkstub::state().status = sdk::statusSuccess;
  sdkstub::state().throw_in_call = true;
  EXPECT_THROW(s->place(o), std::runtime_error);
}

TEST_F(SdkSessionTest, CallbacksAreForwarded)
{
  Events ev;
  auto s = sdk_session_factory()(login(), ev);
  auto l = listener();
  ASSERT_NE(l, nullptr);
  l->onLoginAccepted(5);
  l->onLoginAccepted();
  sdk::OrderAckAttributes attr;
  l->onOrderAccept(10, 9000, attr, 6);
  sdk::FillInfo f;
  f.symbol = "MSFT";
  f.side = sdk::sideSell;
  f.lastShares = 100;
  f.lastPrice = 401100;
  f.leftQty = 200;
  f.transactTime = 1700000000123ULL;
  f.transactTimeNanos = 1700000000123456789ULL;
  f.execType = sdk::execTypeFill;
  f.execId = "X1";
  f.lastMarket = "XNAS";
  l->onOrderFill(10, f, 7);
  f.execType = sdk::execTypeBust;
  l->onOrderFill(10, f, 8);
  f.execType = sdk::execTypeCorrection;
  l->onOrderFill(10, f, 9);
  l->onOrderCancel(10, 10);
  l->onOrderCancel(10, "user", 11);
  l->onOrderPartialCancel(10, 50, 12);
  l->onOrderReplace(10, 11, 9001, attr, 13);
  l->onOrderReject(12, "no buying power", 14);
  l->onOrderCancelReject(10, "too late", 15);
  l->onOrderCancelReplaceReject(10, 11, "x", 16);
  sdk::ManualOrderInfo mi;
  mi.symbol = "IBM";
  mi.route = "ARCP";
  mi.quantity = 10;
  mi.price = 1000000;
  mi.side = sdk::sideBuy;
  l->onManualOrder(20, mi, 17);
  l->onManualOrderReplace(20, 21, 5, 1000100, 18);
  l->onLoginFailed("bad");
  l->onConnectionFailed("reset");
  l->onConnectionBusy();
  l->onConnectionAvailable();
  std::vector<std::string> want = {"login 5",
                                   "login 0",
                                   "accepted 10 9000 6",
                                   "filled 10 FILL 7",
                                   "filled 10 BUST 8",
                                   "filled 10 CORRECTION 9",
                                   "canceled 10 '' 10",
                                   "canceled 10 'user' 11",
                                   "partial 10 50 12",
                                   "replaced 10 11 13",
                                   "rejected 12 no buying power 14",
                                   "cancel_rejected 10 too late 15",
                                   "replace_rejected 10 11 16",
                                   "manual 20 IBM ARCP 10 1000000 BUY 17",
                                   "manual_replace 20 21 5 1000100 18",
                                   "login_failed bad",
                                   "connection_failed reset",
                                   "busy",
                                   "available"};
  EXPECT_EQ(ev.got, want);
  EXPECT_EQ(ev.last_fill.side, Side::Sell);
  EXPECT_EQ(ev.last_fill.last_shares, 100);
  EXPECT_EQ(ev.last_fill.last_price, 401100);
  EXPECT_EQ(ev.last_fill.left_qty, 200);
  EXPECT_EQ(ev.last_fill.transact_time_ns, 1700000000123456789ULL);
  EXPECT_EQ(ev.last_fill.exec_id, "X1");
}

// LimeOM over SdkSession: the session is built and destroyed on the actor's
// thread; an order goes out as placeOrder, the accept comes back as an Ack.
TEST_F(SdkSessionTest, LimeOMOverTheSdkSession)
{
  auto msft = test_stock("MSFT");
  Recorder somr{"SOM"};
  LimeOMConfig c;
  c.account = "ACCT1";
  c.user = "trader";
  c.password = "secret";
  c.default_route = "INET";
  auto om = create_LimeOM(c, &somr);
  ASSERT_NE(om, nullptr);
  auto mgr = std::make_unique<actors::Manager>("sdk");
  mgr->manage_and_start(om);
  ASSERT_TRUE(wait_until([] { return listener() != nullptr; }));
  listener()->onLoginAccepted(1);
  auto lom = static_cast<LimeOM *>(om);
  ASSERT_TRUE(wait_until([&] { return lom->logged_in(); }));
  som::Order o("EQ", en::x::LIME, msft->id, 100, 4012, en::bs::BUY, 4);
  o.oid = 1;
  om->send(new som::Order(o), &somr);
  ASSERT_TRUE(wait_until([] { return calls().size() == 1; }));
  auto call = calls()[0];
  EXPECT_EQ(call.what, "placeOrder");
  EXPECT_EQ(call.price, 401200);
  sdk::OrderAckAttributes attr;
  listener()->onOrderAccept(call.id, 31337, attr, 2);
  auto a = somr.wait_for<som::Ack>();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->xordid, 31337u);
  auto ctor_thread = sdkstub::state().ctors[0].thread;
  mgr->end();
  mgr.reset();
  delete om;
  ASSERT_EQ(sdkstub::state().dtors.size(), 1u);
  EXPECT_EQ(sdkstub::state().dtors[0], ctor_thread);
  EXPECT_NE(ctor_thread, std::this_thread::get_id());
}
