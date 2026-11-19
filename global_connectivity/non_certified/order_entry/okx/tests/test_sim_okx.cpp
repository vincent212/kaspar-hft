/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxOM against SimOkx: the --sim path end to end.

#include <gtest/gtest.h>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "okx_oe/SimOkx.hpp"
#include "okx_oe/if/OkxOM.hpp"

namespace som = frame::som::msg;
using namespace okx::oe;
using namespace okx::oe::test;

namespace
{
  struct Rig
  {
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    OkxOM *om = nullptr;
    explicit Rig(SimOkx::Config sc)
    {
      auto c = demo_config();
      c.sim = true;
      om = new OkxOM(c, &somr, SimOkx::make_factory(sc), frame::som::ClientIdGen(7000));
      mgr = std::make_unique<actors::Manager>("sim");
      mgr->manage_and_start(om);
    }
    ~Rig()
    {
      mgr->end();
      delete om;
    }
    void order(int oid, int units, bool swap = false)
    {
      auto a = assets();
      auto o = new som::Order("CR", swap ? en::x::OKXF : en::x::OKX, uint(swap ? a.swap : a.spot), units, 631234,
                              en::bs::BUY, 4);
      o->oid = oid;
      om->send(o, &somr);
    }
  };
}

TEST(SimOkx, OrderIsAckedThenFilledAtItsPrice)
{
  Rig r({5, 30, 0});
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(1, 10);
  auto f = r.somr.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_DOUBLE_EQ(f->sz, 10);
  EXPECT_EQ(f->pxi, 631234);
  EXPECT_EQ(r.somr.replies(), (std::vector<std::string>{"Ack", "Fill"}));
}

TEST(SimOkx, CancelOfARestingOrder)
{
  Rig r({5, 0, 0});
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(2, 10, true);
  ASSERT_NE(r.somr.wait_for<som::Ack>(), nullptr);
  r.om->send(new som::Cancel(2), &r.somr);
  ASSERT_NE(r.somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(r.somr.replies(), (std::vector<std::string>{"Ack", "CancAck"}));
}

TEST(SimOkx, CancelAfterFillIsRefusedByTheVenueAndTheOrderEndsFilled)
{
  Rig r({5, 10, 0});
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(3, 10);
  ASSERT_NE(r.somr.wait_for<som::Fill>(), nullptr);
  r.om->send(new som::Cancel(3), &r.somr);
  auto cr = r.somr.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr); // refused locally: the book knows it is filled
  EXPECT_EQ(cr->reason, som::CancReject::FILLEDALREADY);
}

TEST(SimOkx, OversizeIsRejectedWithSCode51008)
{
  Rig r({5, 0, 1}); // reject sz above 1 OKX unit
  ASSERT_TRUE(wait_until([&] { return r.om->ready(); }));
  r.order(4, 200, true); // 2.00 contracts
  auto j = r.somr.wait_for<som::Reject>();
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->reason, som::Reject::EXCHLIMIT);
}
