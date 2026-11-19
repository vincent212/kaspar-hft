/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenOM on SimKraken: the --sim path end to end, including a dropped link
// and the snapshot replay after it.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "kraken_oe/SimKraken.hpp"

#include "TestUtil.hpp"

using namespace kraken::oe;
using namespace kraken::oe::test;
namespace som = frame::som::msg;

namespace
{
  struct SimRun
  {
    Recorder somr{"SOM"};
    std::shared_ptr<SimKraken::Venue> venue;
    std::unique_ptr<actors::Manager> mgr;
    KrakenOM *om = nullptr;
    explicit SimRun(SimKraken::Config cfg)
    {
      om = new KrakenOM(om_config(), &somr, SimKraken::make_factory(cfg, venue), en::x::KRAKEN,
                        frame::som::ClientIdGen(9000));
      mgr = std::make_unique<actors::Manager>("sim");
      mgr->manage_and_start(om);
    }
    ~SimRun()
    {
      mgr->end();
      mgr.reset();
      delete om;
    }
    som::Order order(int oid, int units = 10)
    {
      som::Order o("KR", en::x::KRAKEN, uint(assets().spot), units, 631234, en::bs::BUY, 4);
      o.oid = oid;
      o.tif = som::Order::TimeInForce::GTC;
      return o;
    }
  };
}

TEST(SimKraken, AckThenFillsInParts)
{
  SimKraken::Config c;
  c.fill_ms = 20;
  c.fill_parts = 2;
  SimRun r(c);
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.om->send(new som::Order(r.order(1)), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::Fill>(1), nullptr);
  EXPECT_EQ(r.somr.replies(), (std::vector<std::string>{"Ack", "Fill", "Fill"}));
  auto f = r.somr.all<som::Fill>();
  EXPECT_EQ(f[0]->sz + f[1]->sz, 10);
  EXPECT_EQ(f[1]->still_to_be_filled, 0);
}

TEST(SimKraken, RestingOrderCancelsAndRejectedSymbol)
{
  SimKraken::Config c;
  c.no_fill = {"BTC/USD"};
  SimRun r(c);
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.om->send(new som::Order(r.order(1)), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::Ack>(0), nullptr);
  r.om->send(new som::Cancel(1), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::CancAck>(0), nullptr);
  r.om->send(new som::Cancel(1), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::CancReject>(0), nullptr);

  SimKraken::Config c2;
  c2.reject_symbols = {"BTC/USD"};
  SimRun r2(c2);
  ASSERT_TRUE(wait_until([&] { return r2.om->ready(); }));
  r2.om->send(new som::Order(r2.order(1)), &r2.somr);
  ASSERT_NE(r2.somr.wait_for<som::Reject>(0), nullptr);
}

TEST(SimKraken, DroppedLinkFillArrivesOnceAfterReconnect)
{
  SimKraken::Config c;
  c.fill_ms = 300;
  SimRun r(c);
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.om->send(new som::Order(r.order(1)), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::Ack>(0), nullptr);
  SimKraken::drop_connection(*r.venue); // the fill happens while the OM is away
  ASSERT_NE(r.somr.wait_for<som::Fill>(0, std::chrono::seconds(5)), nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(r.somr.all<som::Fill>().size(), 1u);
  EXPECT_EQ(r.om->sessions_created(), 2);
  EXPECT_EQ(SimKraken::executions(*r.venue), 1u);
}
