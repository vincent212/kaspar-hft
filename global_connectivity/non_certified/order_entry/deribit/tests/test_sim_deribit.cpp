/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitOM against SimDeribit: the --sim path end to end.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "deribit_oe/SimDeribit.hpp"
#include "deribit_oe/act/DeribitOM.hpp"

namespace som = frame::som::msg;
using namespace deribit::oe;
using namespace deribit::oe::test;

namespace
{
  struct Rig
  {
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    DeribitOM *om = nullptr;
    explicit Rig(SimDeribit::Config sc)
    {
      auto c = test_config();
      c.sim = true;
      om = new DeribitOM(c, &somr, SimDeribit::make_factory(sc), frame::som::ClientIdGen(7000));
      mgr = std::make_unique<actors::Manager>("sim");
      mgr->manage_and_start(om);
    }
    ~Rig()
    {
      mgr->end();
      delete om;
    }
    void order(int oid, int units)
    {
      auto o = new som::Order("CR", en::x::DERIBIT, uint(assets().btc), units, 126000, en::bs::BUY, 4);
      o->oid = oid;
      om->send(o, &somr);
    }
  };
}

TEST(SimDeribit, OrderIsAckedThenFilledAtItsPrice)
{
  Rig r({5, 30, 0});
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(1, 10);
  auto f = r.somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 10);
  EXPECT_EQ(f->pxi, 126000);
  EXPECT_EQ(r.somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
}

TEST(SimDeribit, CancelOfARestingOrder)
{
  Rig r({5, 0, 0});
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(2, 10);
  ASSERT_NE(r.somr.wait_for<som::Ack>(), nullptr);
  r.om->send(new som::Cancel(2), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::CancAck>(), nullptr);
  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(r.somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST(SimDeribit, CancelAfterFillIsRefusedAndTheOrderEndsFilled)
{
  Rig r({5, 10, 0});
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(3, 10);
  ASSERT_NE(r.somr.wait_for<som::Fill>(), nullptr);
  r.om->send(new som::Cancel(3), &r.somr);
  auto cr = r.somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->reason, som::CancReject::FILLEDALREADY);
}

TEST(SimDeribit, OversizeIsRejectedWithNotEnoughFunds)
{
  Rig r({5, 0, 100});   // amount above 100 USD
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(4, 20);       // 200 USD
  auto j = r.somr.wait_for<som::Reject>();
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->reason, som::Reject::EXCHLIMIT);
}
