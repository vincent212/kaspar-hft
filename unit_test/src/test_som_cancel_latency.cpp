/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * SOM -> OB timestamping, in sim mode.
 *
 * OB does not apply our orders to the book when it receives them. It queues
 * them on del_q and releases each one only once
 *
 *     payload->ts0 + max(40, delay) * 1000 ns
 *
 * has passed in MARKET time (the transactTime of the next real record). ts0 is
 * therefore the whole latency model: it is the moment we decided to act, and
 * everything that happens in the market between then and the release gets in
 * front of us.
 *
 * The order path always set it from the incoming message. The cancel path set
 * it from the ORIGINAL ORDER's ts, whose deadline the order itself had already
 * crossed on release — so every cancel arrived with no latency at all. Our
 * cancels always beat the flow and we never wore the adverse fill a real one
 * would have cost.
 *
 * These tests pin both paths to the timestamp of the message that caused them.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>

#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/act/SOM.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

// SOM replies (an Ack) to whoever sent the Order, and Actor::reply() asserts on
// a null return address. The real run loop sets reply_to = m->sender before
// dispatch; TestHelper does not, so expose it. Same shape as TestablePM in
// test_position_manager.cpp.
struct TestableSOM : public som::act::SOM {
  using som::act::SOM::SOM;
  using som::act::SOM::reply_to;
};

// Market times far enough apart that a mix-up cannot pass by coincidence:
// the cancel is a full second after the order.
constexpr uint64_t kOrderTs  = 1736960000000000000ULL;   // epoch ns
constexpr uint64_t kCancelTs = kOrderTs + 1000000000ULL; // +1 s
constexpr uint64_t kBboTs    = kOrderTs +  500000000ULL; // +0.5 s, between them

class SOMCancelLatencyTest : public ::testing::Test {
protected:
  MockOB mock_book{"MockBook"};
  std::vector<std::vector<actor_ptr>> order_books;
  boost::property_tree::ptree pt;
  std::unique_ptr<TestableSOM> som;
  int sym = 0;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  void SetUp() override {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr) << "fixture universe must contain ESH6";
    sym = asset->id;

    // SOM's ctor requires these two subtrees (it uses get_child, which throws
    // when they are absent). Keyed by mnemonic, as som.ini is.
    pt.put("som.pos_limit.ES", 1000);
    pt.put("som.size_limit.ES", 100);

    order_books.resize(en::x_num_syms());
    for (auto& row : order_books) row.resize(sym + 1, nullptr);
    order_books[en::x::SIM][sym] = &mock_book;

    som = std::make_unique<TestableSOM>(en::x::SIM, nullptr, pt, order_books,
                                       true /*sim_mode*/, false /*spin*/);
    mock_book.clear();
  }

  void TearDown() override { mock_book.clear(); }

  // Mimics the run loop's reply_to bookkeeping around the dispatch.
  void process(const actors::Message* m, actors::Actor* sender = nullptr) {
    som->reply_to = sender;
    TestHelper::invoke_handler(som.get(), m, sender);
    som->reply_to = nullptr;
  }

  // The book payload SOM produced for the idx'th message it forwarded.
  const mda::msg::data_pay_load* forwarded(size_t idx) {
    auto d = mock_book.get_message<mda::msg::Data>(idx);
    return d ? d->payload.get() : nullptr;
  }

  // oid is left negative on purpose: SOM only registers an order in its own
  // `orders` map when it has to assign the id itself (SOM.cpp:1184). Give it
  // one and the order is never registered, so the later cancel is rejected as
  // "order was not found" and never reaches the book.
  som::msg::Order make_order(uint64_t ts) {
    som::msg::Order o;
    o.venue = en::x::SIM;
    o.sym   = sym;
    o.sz    = 1;
    o.px    = 24000;
    o.side  = en::bs::BUY;
    o.oid   = -1;
    o.ts    = ts;
    o.owner = en::trader::SIMULATOR;
    return o;
  }

  // Send an order and return the id SOM assigned it.
  uint place(actors::Actor* client, uint64_t ts) {
    auto ord = make_order(ts);
    process(&ord, client);
    EXPECT_GE(ord.oid, 0) << "SOM should have assigned an order id";
    return static_cast<uint>(ord.oid);
  }
};

// A new order's ts0 is the time the LIGHT sent it. This path was already
// right; the test is here so the two are pinned together and a future change
// cannot fix one by breaking the other.
TEST_F(SOMCancelLatencyTest, OrderCarriesItsOwnSendTime) {
  MockLight client("client");
  place(&client, kOrderTs);

  auto pl = forwarded(0);
  ASSERT_NE(pl, nullptr) << "SOM should forward the order to the book";
  EXPECT_EQ(pl->mev, en::md::ADD);
  EXPECT_EQ(pl->ts0, kOrderTs);
}

// The regression this branch exists for. Before the fix ts0 came from the
// original order, so this read kOrderTs and OB released the cancel on the very
// next record — zero modelled latency.
TEST_F(SOMCancelLatencyTest, CancelCarriesItsOwnDecisionTime) {
  MockLight client("client");
  const uint oid = place(&client, kOrderTs);
  mock_book.clear();

  som::msg::Cancel canc(oid, kCancelTs);
  process(&canc, &client);

  auto pl = forwarded(0);
  ASSERT_NE(pl, nullptr) << "SOM should forward the cancel to the book";
  EXPECT_EQ(pl->action, en::mt::CANCD);
  EXPECT_EQ(pl->ts0, kCancelTs)
      << "cancel must be stamped with when the CANCEL was decided, not when "
         "the order was sent; anything else gives cancels zero wire latency";
  EXPECT_NE(pl->ts0, kOrderTs) << "this is the original-order timestamp";
}

// A cancel with no time of its own -- the SOM's own unwind cancels, a console
// cancel -- keeps ts0 == 0. That is not a defect to paper over with a
// substitute timestamp: OB reads 0 as "no clock, apply without delay", which is
// honest about the fact that there is no latency to model for a message that
// did not come from the market.
TEST_F(SOMCancelLatencyTest, AnUntimedCancelStaysUntimed) {
  MockLight client("client");
  const uint oid = place(&client, kOrderTs);
  mock_book.clear();

  som::msg::Cancel canc(oid);   // no ts
  EXPECT_EQ(canc.ts, 0u);
  process(&canc, &client);

  auto pl = forwarded(0);
  ASSERT_NE(pl, nullptr);
  EXPECT_EQ(pl->action, en::mt::CANCD);
  EXPECT_EQ(pl->ts0, 0u)
      << "an untimed cancel must not be given a substitute timestamp";
  EXPECT_NE(pl->ts0, kOrderTs)
      << "least of all the original order's, whose deadline is already past";
}

// A BBBOChg in between must not change that: the SOM's own clock is not a
// stand-in for a timestamp the canceller never supplied.
TEST_F(SOMCancelLatencyTest, AMarketUpdateDoesNotSupplyAMissingCancelTime) {
  MockLight client("client");
  const uint oid = place(&client, kOrderTs);

  ob::msg::BBBOChg bbo(kBboTs, en::x::SIM, en::bs::BUY, sym, 23999, 24001);
  process(&bbo, &mock_book);
  mock_book.clear();

  som::msg::Cancel canc(oid);
  process(&canc, &client);

  auto pl = forwarded(0);
  ASSERT_NE(pl, nullptr);
  EXPECT_EQ(pl->ts0, 0u);
}

}  // namespace
