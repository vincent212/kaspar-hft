/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// ClearStreetOM against SimClearStreet (the --sim broker), both on their own
// threads: the end-to-end flows a sim run produces.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "clearstreet_om/SimClearStreet.hpp"
#include "clearstreet_om/act/ClearStreetOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace clearstreet::om;
using namespace clearstreet::om::test;
using frame::som::State;

namespace
{
  class SimTest : public ::testing::Test
  {
  protected:
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    ClearStreetOM *om = nullptr;
    SimClearStreet *sim = nullptr;
    const frame::ref::Asset *msft = nullptr;

    void SetUp() override { msft = equity("MSFT_CS"); }
    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
    }
    void start(SimConfig sc, const std::string &account = "SIM1")
    {
      ClearStreetOMConfig c;
      c.account = account;
      c.instruments[msft->id] = CsInstrument{"MSFT", 0.01};
      auto f = [this, sc](SessionEvents &ev) -> std::unique_ptr<CsSession> {
        auto s = std::make_unique<SimClearStreet>(sc, ev);
        sim = s.get();
        return s;
      };
      om = new ClearStreetOM(c, &somr, f, en::x::CLEARSTREET, frame::som::ClientIdGen(7000), "20261118");
      mgr = std::make_unique<actors::Manager>("simtest");
      mgr->manage_and_start(om);
      ASSERT_TRUE(wait_until([&] { return om->ready(); }));
    }
    Order order(int oid, int qty = 100, int px = 41010)
    {
      Order o("CS", en::x::CLEARSTREET, msft->id, qty, px, en::bs::BUY, 4);
      o.oid = oid;
      return o;
    }
    bool replies_are(const std::vector<std::string> &want)
    {
      return wait_until([&] { return somr.replies() == want; });
    }
  };
}

TEST_F(SimTest, OrderIsAckedAndFilledInTwoExecutions)
{
  start(SimConfig{});
  om->send(new Order(order(1)), &somr);
  ASSERT_TRUE(replies_are({"Ack 1", "Fill 1 50", "Fill 1 50"})) << ::testing::PrintToString(somr.replies());
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->pxi, 41010);
  EXPECT_EQ(om->book().find_oid(1)->state, State::Filled);
}

TEST_F(SimTest, QueuedOrderAckedOnPendingNew)
{
  SimConfig sc;
  sc.queue_orders = true;
  sc.fill_ms = -1;
  start(sc);
  om->send(new Order(order(1)), &somr);
  ASSERT_TRUE(replies_are({"Ack 1"}));
}

TEST_F(SimTest, CancelAndReplaceRoundTrips)
{
  SimConfig sc;
  sc.fill_ms = -1;
  start(sc);
  om->send(new Order(order(1)), &somr);
  ASSERT_TRUE(replies_are({"Ack 1"}));
  auto n = order(2, 60, 40990);
  n.oid_to_cancel = 1;
  om->send(new Order(n), &somr);
  ASSERT_TRUE(replies_are({"Ack 1", "CancAck 1", "Ack 2"}));
  om->send(new som::Cancel(2), &somr);
  ASSERT_TRUE(replies_are({"Ack 1", "CancAck 1", "Ack 2", "CancAck 2"}));
  om->send(new som::Cancel(2), &somr); // already cancelled: refused locally
  ASSERT_TRUE(replies_are({"Ack 1", "CancAck 1", "Ack 2", "CancAck 2", "CancReject 2"}));
}

TEST_F(SimTest, UnauthorizedAccountIsASessionRejectThatRejectsTheOrder)
{
  SimConfig sc;
  sc.accounts = {"OTHER"};
  start(sc, "SIM1");
  EXPECT_EQ(om->counters().account_not_listed, 1u);
  om->send(new Order(order(1)), &somr);
  ASSERT_TRUE(replies_are({"Reject 1"}));
  EXPECT_EQ(om->counters().session_rejects, 1u);
}

TEST_F(SimTest, UnsolicitedCancelAndReconnect)
{
  SimConfig sc;
  sc.fill_ms = -1;
  start(sc);
  om->send(new Order(order(1)), &somr);
  ASSERT_TRUE(replies_are({"Ack 1"}));
  sim->drop();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }));
  om->send(new Order(order(2)), &somr);
  ASSERT_TRUE(replies_are({"Ack 1", "Reject 2"}));
  sim->restore();
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  sim->cancel_all("risk");
  ASSERT_TRUE(replies_are({"Ack 1", "Reject 2", "CancAck 1"}));
}
