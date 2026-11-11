/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Optional order instructions on som::msg::Order: order type, time in force,
 * short-sale marking, stop price, route, broker strategy and locate.
 *
 * They exist for order managers that need more than a limit order for the day
 * (equities brokers). Code that never sets them must see exactly the old
 * behaviour, so the tests pin the defaults, check that every copy keeps them
 * (the SOM stores and forwards orders by copy), that JSON round-trips them,
 * that JSON written before they existed still reads, and that a live SOM hands
 * them to the venue's order manager unchanged.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

#include "frame/ref/RefData.hpp"
#include "frame/som/act/SOM.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;
using Order = som::msg::Order;

namespace
{
  Order equity_order()
  {
    Order o("EQ", en::x::SIM, 1, 300, 12345, en::bs::SEL, 7);
    o.ord_type = Order::OrdType::STOP_LIMIT;
    o.tif = Order::TimeInForce::IOC;
    o.short_sale = Order::ShortSale::SHORT;
    o.stop_px = 12340;
    o.route = "XNAS";
    o.strategy = "VWAP";
    o.locate_id = "LOC-42";
    return o;
  }

  void expect_equity_fields(const Order &o)
  {
    EXPECT_EQ(o.ord_type, Order::OrdType::STOP_LIMIT);
    EXPECT_EQ(o.tif, Order::TimeInForce::IOC);
    EXPECT_EQ(o.short_sale, Order::ShortSale::SHORT);
    EXPECT_EQ(o.stop_px, 12340);
    EXPECT_EQ(o.route, "XNAS");
    EXPECT_EQ(o.strategy, "VWAP");
    EXPECT_EQ(o.locate_id, "LOC-42");
  }
} // namespace

// Unset means a long limit order for the day with no routing instructions:
// what every order manager assumed before the fields existed.
TEST(OrderFields, DefaultsAreTheOldBehaviour)
{
  Order d;
  EXPECT_EQ(d.ord_type, Order::OrdType::LIMIT);
  EXPECT_EQ(d.tif, Order::TimeInForce::DAY);
  EXPECT_EQ(d.short_sale, Order::ShortSale::NONE);
  EXPECT_EQ(d.stop_px, 0);
  EXPECT_TRUE(d.route.empty());
  EXPECT_TRUE(d.strategy.empty());
  EXPECT_TRUE(d.locate_id.empty());

  Order c("P", en::x::SIM, 1, 5, 100, en::bs::BUY, 0);
  EXPECT_EQ(c.ord_type, Order::OrdType::LIMIT);
  EXPECT_EQ(c.tif, Order::TimeInForce::DAY);
  EXPECT_EQ(c.short_sale, Order::ShortSale::NONE);
}

// The SOM keeps orders in maps by value and forwards a heap copy, so copy
// construction and assignment must carry the fields.
TEST(OrderFields, CopiesKeepThem)
{
  const Order o = equity_order();
  Order copy(o);
  expect_equity_fields(copy);
  Order assigned;
  assigned = o;
  expect_equity_fields(assigned);
  auto heap = std::make_unique<Order>(o);
  expect_equity_fields(*heap);
}

TEST(OrderFields, JsonRoundTrip)
{
  const Order o = equity_order();
  Order back = Order::from_json(o.to_json());
  expect_equity_fields(back);
  EXPECT_EQ(back.px, o.px);
  EXPECT_EQ(back.side, o.side);
}

// Every enumerator survives the int encoding used in JSON.
TEST(OrderFields, JsonKeepsEveryEnumValue)
{
  for (auto t : {Order::OrdType::LIMIT, Order::OrdType::MARKET, Order::OrdType::STOP, Order::OrdType::STOP_LIMIT})
  {
    Order o;
    o.venue = en::x::SIM;
    o.ord_type = t;
    EXPECT_EQ(Order::from_json(o.to_json()).ord_type, t);
  }
  for (auto t : {Order::TimeInForce::DAY, Order::TimeInForce::IOC, Order::TimeInForce::FOK,
                 Order::TimeInForce::GTC, Order::TimeInForce::AT_OPEN, Order::TimeInForce::AT_CLOSE,
                 Order::TimeInForce::EXTENDED_DAY})
  {
    Order o;
    o.venue = en::x::SIM;
    o.tif = t;
    EXPECT_EQ(Order::from_json(o.to_json()).tif, t);
  }
  for (auto s : {Order::ShortSale::NONE, Order::ShortSale::SHORT, Order::ShortSale::SHORT_EXEMPT})
  {
    Order o;
    o.venue = en::x::SIM;
    o.short_sale = s;
    EXPECT_EQ(Order::from_json(o.to_json()).short_sale, s);
  }
}

// JSON from before these fields existed has none of their keys.
TEST(OrderFields, OldJsonReadsAsDefaults)
{
  const std::string old_json =
      R"({"venue":"0","sym":"3","sz":"5","px":"100","oid":"9","side":"0","cusip":""})";
  Order o = Order::from_json(old_json);
  EXPECT_EQ(o.oid, 9);
  EXPECT_EQ(o.ord_type, Order::OrdType::LIMIT);
  EXPECT_EQ(o.tif, Order::TimeInForce::DAY);
  EXPECT_EQ(o.short_sale, Order::ShortSale::NONE);
  EXPECT_EQ(o.stop_px, 0);
  EXPECT_TRUE(o.route.empty());
  EXPECT_TRUE(o.strategy.empty());
  EXPECT_TRUE(o.locate_id.empty());
}

// ---------------------------------------------------------------------------
// Through a live SOM: the order manager registered for the venue receives the
// instructions exactly as the strategy set them.
// ---------------------------------------------------------------------------

namespace
{
  struct TestableSOM : public som::act::SOM
  {
    using som::act::SOM::SOM;
    using som::act::SOM::reply_to;
  };

  class LiveSOMForwardTest : public ::testing::Test
  {
  protected:
    MockActor om{"VenueOM"};
    MockActor client{"Client"};
    std::vector<std::vector<actor_ptr>> order_books;
    boost::property_tree::ptree pt;
    std::unique_ptr<TestableSOM> som;
    int sym = 0;

    static void SetUpTestSuite()
    {
      const char *proj = std::getenv("KSPRPROJ");
      ref::RefData::set_universe(proj ? std::string(proj) + "/unit_test/config/universe.csv"
                                      : "../config/universe.csv",
                                 "");
    }

    void SetUp() override
    {
      auto asset = ref::RefData::get_asset("ESH6");
      ASSERT_NE(asset, nullptr) << "fixture universe must contain ESH6";
      sym = asset->id;
      pt.put("som.pos_limit.ES", 1000);
      pt.put("som.size_limit.ES", 100);
      order_books.resize(en::x_num_syms());
      for (auto &row : order_books)
        row.resize(sym + 1, nullptr);
      som = std::make_unique<TestableSOM>(en::x::SIM, nullptr, pt, order_books,
                                          false /*sim_mode: live*/, false /*spin*/);
      som::msg::SetExchManager reg(en::x::SIM, &om);
      process(&reg, nullptr);
    }

    void TearDown() override
    {
      om.clear();
      client.clear();
    }

    void process(const actors::Message *m, actors::Actor *sender)
    {
      som->reply_to = sender;
      TestHelper::invoke_handler(som.get(), m, sender);
      som->reply_to = nullptr;
    }
  };
} // namespace

TEST_F(LiveSOMForwardTest, OrderManagerReceivesTheInstructions)
{
  Order o = equity_order();
  o.sym = sym;
  o.sz = 1;
  o.px = 24000;
  o.oid = -1;
  o.owner = en::trader::SIMULATOR;
  process(&o, &client);

  auto fwd = om.get_message<Order>(0);
  ASSERT_NE(fwd, nullptr) << "live SOM did not forward the order to the venue's order manager";
  expect_equity_fields(*fwd);
}

TEST_F(LiveSOMForwardTest, PlainOrderArrivesWithDefaults)
{
  Order o("P", en::x::SIM, sym, 1, 24000, en::bs::BUY, en::trader::SIMULATOR);
  o.oid = -1;
  process(&o, &client);

  auto fwd = om.get_message<Order>(0);
  ASSERT_NE(fwd, nullptr);
  EXPECT_EQ(fwd->ord_type, Order::OrdType::LIMIT);
  EXPECT_EQ(fwd->tif, Order::TimeInForce::DAY);
  EXPECT_EQ(fwd->short_sale, Order::ShortSale::NONE);
  EXPECT_TRUE(fwd->route.empty());
}
