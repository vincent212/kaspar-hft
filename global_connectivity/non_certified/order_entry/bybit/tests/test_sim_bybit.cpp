/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitOM against SimBybit: whole order lifecycles through the simulator's
// own thread, as a --sim run would see them.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "bybit_oe/SimBybit.hpp"
#include "bybit_oe/act/BybitOM.hpp"

namespace som = frame::som::msg;
using namespace bybit::oe;
using namespace bybit::oe::test;

namespace
{
  struct SimRun
  {
    SimBybit sim;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    BybitOM *om = nullptr;
    const frame::ref::Asset *a = test_asset("BTCUSDT_SIM");

    explicit SimRun(SimBybit::Config c) : sim(c)
    {
      BybitOMConfig oc;
      oc.sim = true;
      oc.instruments[a->id] = {"linear", "BTCUSDT", 0.1, 0.001};
      om = new BybitOM(oc, &somr, sim.factory(), en::x::BYBITF, frame::som::ClientIdGen(1), 20261118);
      mgr = std::make_unique<actors::Manager>("simtest");
      mgr->manage_and_start(om);
      wait_until([&] { return om->ready(); });
    }
    ~SimRun()
    {
      mgr->end();
      delete om;
    }
    void order(int oid, int sz, int px, som::Order::TimeInForce tif = som::Order::TimeInForce::GTC)
    {
      som::Order o("CR", en::x::BYBITF, a->id, sz, px, en::bs::BUY, 4);
      o.oid = oid;
      o.tif = tif;
      om->send(new som::Order(o), &somr);
    }
  };
}

TEST(SimBybit, FillsInPartsAtTheLimitPrice)
{
  SimBybit::Config c;
  c.fill_parts = 3;
  c.fill_delay_ms = 5;
  SimRun r(c);
  ASSERT_TRUE(r.om->ready());
  r.order(1, 6, 630000);
  ASSERT_NE(r.somr.wait_for<som::Fill>(2), nullptr);
  auto f = r.somr.all<som::Fill>();
  EXPECT_EQ(f[0]->sz + f[1]->sz + f[2]->sz, 6);
  EXPECT_EQ(f[2]->still_to_be_filled, 0);
  EXPECT_EQ(f[0]->pxi, 630000);
  EXPECT_EQ(r.somr.replies().front(), "Ack");
  EXPECT_EQ(r.sim.executions(), 3u);
}

TEST(SimBybit, RestingOrderCancels)
{
  SimBybit::Config c;
  c.fill = false;
  SimRun r(c);
  r.order(1, 2, 630000);
  ASSERT_NE(r.somr.wait_for<som::Ack>(), nullptr);
  r.om->send(new som::Cancel(1), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::CancAck>(), nullptr);
  r.om->send(new som::Cancel(1), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::CancReject>(), nullptr);
}

TEST(SimBybit, IocWithoutFillIsCanceled)
{
  SimBybit::Config c;
  c.fill = false;
  SimRun r(c);
  r.order(1, 2, 630000, som::Order::TimeInForce::IOC);
  ASSERT_NE(r.somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(r.somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST(SimBybit, RejectedSymbol)
{
  SimBybit::Config c;
  c.reject_symbols = {"BTCUSDT"};
  SimRun r(c);
  r.order(1, 2, 630000);
  ASSERT_NE(r.somr.wait_for<som::Reject>(), nullptr);
}
