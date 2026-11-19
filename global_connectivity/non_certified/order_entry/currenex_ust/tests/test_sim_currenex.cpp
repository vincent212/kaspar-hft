/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CurrenexOM against SimCurrenex, the in-process Currenex used for --sim
// runs: what a sim run sees, end to end, with the simulator on its own
// TimedRunner thread.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "currenex_ust/SimCurrenex.hpp"
#include "currenex_ust/act/CurrenexOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace currenex::ouch;
using namespace currenex::ouch::test;
using frame::som::State;

namespace
{
  constexpr int PX = 25345; // 99-00+

  class SimCurrenexTest : public ::testing::Test
  {
  protected:
    std::unique_ptr<SimCurrenex> sim;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    CurrenexOM *om = nullptr;
    const frame::ref::Asset *note = nullptr;

    void SetUp() override { note = test_bond("91282CJY8"); } // index 3 in the simulator's default list

    void start(SimCurrenex::Config sc, bool persistence = true)
    {
      sim = std::make_unique<SimCurrenex>(sc);
      CurrenexOMConfig c;
      c.sim = true;
      c.reconnect_ms = 50;
      c.persistence = persistence;
      om = new CurrenexOM(c, &somr, sim->factory(), en::x::SIM);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      ASSERT_TRUE(wait_until([&] { return om->ready() && om->next_sequence() == sim->last_sequence() + 1 &&
                                          sim->last_sequence() >= 4; }));
    }

    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      sim.reset();
    }

    Order order(int oid, int sz = 2)
    {
      Order o("UST", en::x::SIM, note->id, sz, PX, en::bs::BUY, 4);
      o.oid = oid;
      return o;
    }
  };
}

TEST_F(SimCurrenexTest, AckThenFill)
{
  SimCurrenex::Config sc;
  sc.fill_delay_ms = 20;
  sc.heartbeat_ms = 0;
  start(sc);
  om->send(new Order(order(1)), &somr);
  auto f = somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 2);
  EXPECT_EQ(f->pxi, PX);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(1)->state == State::Filled; }));
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
}

TEST_F(SimCurrenexTest, FilledInPartsOfLessThanAUnit)
{
  SimCurrenex::Config sc;
  sc.fill_delay_ms = 5;
  sc.fill_parts = 4;
  sc.heartbeat_ms = 0;
  start(sc);
  om->send(new Order(order(2, 1)), &somr);
  ASSERT_NE(somr.wait_for<som::Fill>(3), nullptr);
  EXPECT_DOUBLE_EQ(somr.all<som::Fill>()[0]->sz, 0.25);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(2)->state == State::Filled; }));
}

TEST_F(SimCurrenexTest, PendingFillWorkflow)
{
  SimCurrenex::Config sc;
  sc.fill_delay_ms = 5;
  sc.pending_fill = true;
  sc.heartbeat_ms = 0;
  start(sc);
  om->send(new Order(order(3)), &somr);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(3)->state == State::Filled; }));
  EXPECT_EQ(somr.all<som::Fill>().size(), 1u);
}

TEST_F(SimCurrenexTest, RestingOrderCancelled)
{
  SimCurrenex::Config sc;
  sc.fill = false;
  sc.heartbeat_ms = 0;
  start(sc);
  om->send(new Order(order(4)), &somr);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  om->send(new som::Cancel(4), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(4)->state == State::Canceled; }));
}

TEST_F(SimCurrenexTest, IocWithoutAMatchIsCancelled)
{
  SimCurrenex::Config sc;
  sc.fill = false;
  sc.heartbeat_ms = 0;
  start(sc);
  auto o = order(5);
  o.tif = Order::TimeInForce::IOC;
  om->send(new Order(o), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

// Heartbeats are numbered and counted.
TEST_F(SimCurrenexTest, HeartbeatsAreCounted)
{
  SimCurrenex::Config sc;
  sc.heartbeat_ms = 20;
  start(sc);
  auto before = om->next_sequence();
  ASSERT_TRUE(wait_until([&] { return om->next_sequence() >= before + 3; }));
}
