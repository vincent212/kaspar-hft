/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OuchOM against SimOuch, the in-process Nasdaq used for --sim runs: what a
// sim run sees, end to end, with the simulator on its own TimedRunner
// thread.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"

#include "TestUtil.hpp"
#include "nasdaq_ouch/SimOuch.hpp"
#include "nasdaq_ouch/act/OuchOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace nasdaq::ouch;
using namespace nasdaq::ouch::test;
using frame::som::State;

namespace
{
  class SimOuchTest : public ::testing::Test
  {
  protected:
    std::unique_ptr<SimOuch> sim;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    OuchOM *om = nullptr;
    const frame::ref::Asset *msft = nullptr;
    const frame::ref::Asset *bad = nullptr;

    void SetUp() override
    {
      msft = test_stock("MSFT");
      bad = test_stock("BADSYM");
    }

    void start(SimOuch::Config sc)
    {
      sc.reject_symbols.insert("BADSYM");
      sim = std::make_unique<SimOuch>(sc);
      OuchOMConfig c;
      c.sim = true;
      c.reconnect_ms = 50;
      om = static_cast<OuchOM *>(create_OuchOM(c, &somr, sim->factory()));
      ASSERT_NE(om, nullptr);
      mgr = std::make_unique<actors::Manager>("simtest");
      mgr->manage_and_start(om);
      ASSERT_TRUE(wait_until([&] { return om->ready(); }));
    }

    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      sim.reset(); // after the OM: its session points into the simulator
    }

    Order order(int oid, int qty = 100, const frame::ref::Asset *a = nullptr)
    {
      Order o("EQ", en::x::NASDAQ, (a ? a : msft)->id, qty, 4125, en::bs::BUY, 4);
      o.oid = oid;
      return o;
    }
  };
}

TEST_F(SimOuchTest, AcceptsThenFillsAfterTheDelayInParts)
{
  SimOuch::Config sc;
  sc.fill_delay_ms = 30;
  sc.fill_parts = 2;
  start(sc);
  auto t0 = std::chrono::steady_clock::now();
  om->send(new Order(order(1, 100)), &somr);
  auto ack = somr.wait_for<som::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_NE(ack->xordid, 0u);
  auto f2 = somr.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_GE(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(60));
  auto fills = somr.all<som::Fill>();
  EXPECT_DOUBLE_EQ(fills[0]->sz, 50);
  EXPECT_DOUBLE_EQ(fills[0]->still_to_be_filled, 50);
  EXPECT_DOUBLE_EQ(fills[1]->still_to_be_filled, 0);
  EXPECT_EQ(fills[1]->pxi, 4125);
  EXPECT_TRUE(wait_until([&] { return om->book().find_oid(1)->state == State::Filled; }));
}

TEST_F(SimOuchTest, RestingOrderIsCanceledOnRequest)
{
  SimOuch::Config sc;
  sc.fill = false;
  start(sc);
  om->send(new Order(order(2)), &somr);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  om->send(new som::Cancel(2), &somr);
  auto ca = somr.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 2u);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST_F(SimOuchTest, RejectsAConfiguredSymbol)
{
  start({});
  om->send(new Order(order(3, 100, bad)), &somr);
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->id, 3u);
}

TEST_F(SimOuchTest, UnfilledIocIsCanceled)
{
  SimOuch::Config sc;
  sc.fill = false;
  start(sc);
  auto o = order(4);
  o.tif = Order::TimeInForce::IOC;
  om->send(new Order(o), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

// A second OM on the same simulated port (a restart) continues after the
// highest UserRefNum the simulator has seen.
TEST_F(SimOuchTest, RestartContinuesUserRefNums)
{
  SimOuch::Config sc;
  sc.fill = false;
  start(sc);
  om->send(new Order(order(5)), &somr);
  om->send(new Order(order(6)), &somr);
  ASSERT_NE(somr.wait_for<som::Ack>(1), nullptr);
  mgr->end();
  mgr.reset();
  delete om;

  OuchOMConfig c;
  c.sim = true;
  om = static_cast<OuchOM *>(create_OuchOM(c, &somr, sim->factory()));
  mgr = std::make_unique<actors::Manager>("simtest2");
  mgr->manage_and_start(om);
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  EXPECT_EQ(om->next_user_ref_num(), 3u);
}
