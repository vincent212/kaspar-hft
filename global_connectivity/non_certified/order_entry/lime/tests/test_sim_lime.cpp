/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LimeOM against SimLime, the in-process simulated Lime used for --sim runs.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "lime_om/act/LimeOM.hpp"

namespace som = frame::som::msg;
using namespace lime;
using namespace lime::test;
using frame::som::State;

namespace
{
  class SimLimeTest : public ::testing::Test
  {
  protected:
    Recorder somr{"SOM"};
    std::unique_ptr<SimLime> sim;
    std::unique_ptr<actors::Manager> mgr;
    LimeOM *om = nullptr;
    const frame::ref::Asset *msft = nullptr;

    void start(SimLime::Config sc = {})
    {
      msft = test_stock("MSFT");
      sc.fill_delay_ms = 5;
      sim = std::make_unique<SimLime>(sc);
      LimeOMConfig c;
      c.account = "SIM";
      c.user = "sim";
      c.default_route = "INET";
      c.reconnect_ms = 20;
      om = new LimeOM(c, &somr, sim->factory());
      mgr = std::make_unique<actors::Manager>("sim");
      mgr->manage_and_start(om);
      ASSERT_TRUE(wait_until([&] { return om->logged_in(); }));
    }
    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      sim.reset();
    }
    void send(int oid, int sz, som::Order::TimeInForce tif = som::Order::TimeInForce::DAY, std::string route = "")
    {
      som::Order o("EQ", en::x::LIME, msft->id, sz, 4012, en::bs::BUY, 4);
      o.oid = oid;
      o.tif = tif;
      o.route = route;
      om->send(new som::Order(o), &somr);
    }
  };
}

TEST_F(SimLimeTest, OrderIsAcceptedAndFilledInParts)
{
  SimLime::Config c;
  c.fill_parts = 3;
  start(c);
  send(1, 300);
  ASSERT_TRUE(wait_until([&] { return om->book().find_oid(1) && om->book().find_oid(1)->state == State::Filled; }));
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill", "Fill"}));
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->pxi, 4012);
  EXPECT_DOUBLE_EQ(f[2]->still_to_be_filled, 0);
}

TEST_F(SimLimeTest, RestingOrderIsCancelled)
{
  SimLime::Config c;
  c.fill = false;
  start(c);
  send(2, 100);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  om->send(new som::Cancel(2), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(om->book().find_oid(2)->state, State::Canceled);
}

TEST_F(SimLimeTest, UnfilledIocIsCancelledAndBadRouteRejected)
{
  SimLime::Config c;
  c.fill = false;
  start(c);
  send(3, 100, som::Order::TimeInForce::IOC);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  send(4, 100, som::Order::TimeInForce::DAY, "REJECT");
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
  EXPECT_EQ(om->book().find_oid(4)->state, State::Rejected);
}

// SimLime replays from the eventId a session is built with.
TEST(SimLimeAlone, ReplayFromEventId)
{
  struct Ev : SessionEvents
  {
    std::mutex mu;
    std::vector<uint64_t> ids;
    std::atomic<bool> in{false};
    void login_accepted(uint64_t) override { in = true; }
    void login_failed(const std::string &) override {}
    void connection_failed(const std::string &) override {}
    void busy() override {}
    void available() override {}
    void note(uint64_t e)
    {
      std::lock_guard<std::mutex> l(mu);
      ids.push_back(e);
    }
    void accepted(OrderId, OrderId, uint64_t e) override { note(e); }
    void filled(OrderId, const FillInfo &, uint64_t e) override { note(e); }
    void canceled(OrderId, const std::string &, uint64_t e) override { note(e); }
    void partial_canceled(OrderId, int, uint64_t e) override { note(e); }
    void replaced(OrderId, OrderId, OrderId, uint64_t e) override { note(e); }
    void rejected(OrderId, const std::string &, uint64_t e) override { note(e); }
    void cancel_rejected(OrderId, const std::string &, uint64_t e) override { note(e); }
    void replace_rejected(OrderId, OrderId, const std::string &, uint64_t e) override { note(e); }
    void manual_order(OrderId, const std::string &, const std::string &, int, ScaledPrice, Side, uint64_t) override {}
    void manual_replace(OrderId, OrderId, int, ScaledPrice, uint64_t) override {}
    size_t n()
    {
      std::lock_guard<std::mutex> l(mu);
      return ids.size();
    }
  };
  SimLime::Config c;
  c.fill_delay_ms = 1;
  SimLime sim(c);
  LoginParams p;
  p.account = "A";
  p.user = "u";
  p.cancel_all_on_disconnect = false;
  Ev e1;
  auto s1 = sim.factory()(p, e1);
  ASSERT_TRUE(wait_until([&] { return e1.in.load(); }));
  NewOrder o;
  o.id = 1;
  o.route = "INET";
  o.symbol = "MSFT";
  o.quantity = 100;
  o.price = 401200;
  ASSERT_EQ(s1->place(o), CallStatus::Success);
  ASSERT_TRUE(wait_until([&] { return e1.n() == 2; })); // accepted, filled
  s1.reset();
  EXPECT_EQ(sim.last_event_id(), 2u);
  Ev e2;
  p.event_id = 2;
  auto s2 = sim.factory()(p, e2);
  ASSERT_TRUE(wait_until([&] { return e2.n() == 1; }));
  EXPECT_EQ(e2.ids, (std::vector<uint64_t>{2}));
}
