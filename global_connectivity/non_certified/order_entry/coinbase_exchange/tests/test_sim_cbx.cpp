/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * CbxOM against SimCbx, the in-process Coinbase Exchange of --sim runs.
 */

#include <gtest/gtest.h>

#include "chutil/Assert.hpp"
#include "actors/act/Manager.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Reject.hpp"

#include "TestUtil.hpp"
#include "coinbase_exchange_oe/SimCbx.hpp"
#include "coinbase_exchange_oe/act/CbxOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace coinbase_exchange;
using namespace coinbase_exchange::oe;
using namespace coinbase_exchange::oe::test;

namespace
{
  class SimCbxTest : public ::testing::Test
  {
  protected:
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    std::unique_ptr<SimCbx> sim;
    CbxOM *om = nullptr;
    const frame::ref::Asset *btc = nullptr;

    void SetUp() override { btc = test_product("BTC-USD"); }
    void start(SimCbx::Config sc)
    {
      sim = std::make_unique<SimCbx>(sc);
      CbxOMConfig c;
      OEProduct p;
      std::string err;
      ASSERT_TRUE(OEProduct::make("BTC-USD", "0.01", "0.00000001", "", p, err));
      c.products["BTC-USD"] = p;
      om = new CbxOM(c, &somr, sim->factory(), en::x::COINBASEX, frame::som::ClientIdGen(7000), 7);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      ASSERT_TRUE(wait_until([&] { return om->ready(); }));
    }
    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      sim.reset();
    }
    Order order(int oid, Order::TimeInForce tif = Order::TimeInForce::GTC, int qty = 250000, int px = 6400000)
    {
      Order o("CB", en::x::COINBASEX, btc->id, qty, px, en::bs::BUY, 4);
      o.oid = oid;
      o.tif = tif;
      return o;
    }
  };
}

TEST_F(SimCbxTest, AckThenFillsInParts)
{
  SimCbx::Config c;
  c.fill_parts = 2;
  c.fill_delay_ms = 10;
  start(c);
  om->send(new Order(order(1)), &somr);
  ASSERT_NE(somr.wait_for<som::Fill>(1), nullptr);
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->sz + f[1]->sz, 250000);
  EXPECT_EQ(f[1]->still_to_be_filled, 0);
  EXPECT_EQ(f[0]->pxi, 6400000);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
  EXPECT_NE(somr.all<som::Ack>()[0]->xordid, 0u);
}

TEST_F(SimCbxTest, RestingOrderCancels_IocExpires_UnknownCancelIsRejected)
{
  SimCbx::Config c;
  c.fill = false;
  start(c);
  om->send(new Order(order(1)), &somr);
  ASSERT_NE(somr.wait_for<som::Ack>(), nullptr);
  om->send(new som::Cancel(1), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  om->send(new Order(order(2, Order::TimeInForce::IOC)), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(1), nullptr) << "IOC with nothing to fill expires";
  om->send(new som::Cancel(2), &somr);
  auto r = somr.wait_for<som::CancReject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::CancReject::CANCELLEDALREADY);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "CancAck", "Ack", "CancAck", "CancReject"}));
}

TEST_F(SimCbxTest, RejectedSymbol)
{
  SimCbx::Config c;
  c.reject_symbols = {"BTC-USD"};
  start(c);
  om->send(new Order(order(1)), &somr);
  ASSERT_NE(somr.wait_for<som::Reject>(), nullptr);
}
