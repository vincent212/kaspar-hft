/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * OB two-queue G/D/1 upgrade (paper §4.1 eqs (3)-(4), §5.2).
 *
 * These tests only compile under -DOB_TAIL_DELAY. Under the flag both release
 * rules follow the Lindley recursion
 *
 *     d_i = max(a_i, d_{i-1}) + s
 *
 * with a single scalar service time s per side and no other latency
 * composition. Observable at the same channels test_ob_delay_queue.cpp uses:
 * pub_q_size() and the "qat" get for the inbound side; our own resting size
 * for the outbound side.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

#ifdef OB_TAIL_DELAY

using namespace frame;
using namespace unit_test;

namespace {

// See test_ob_delay_queue.cpp for the reasoning behind this shim; OB's
// Subscribe handler is typeid-dispatched and reply_to is protected.
struct TestableOB : public ob::act::OB {
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;
  using actors::Actor::process_message;
};

constexpr uint64_t kT0    = 1736960000000000000ULL;
constexpr int      kBidPx = 23990;
constexpr int      kAskPx = 24010;
constexpr int      kOurPx = 24000;

class OBTailDelayTest : public ::testing::Test {
protected:
  MockActor probe{"probe"};
  MockActor feed{"feed"};
  boost::property_tree::ptree pt;
  std::unique_ptr<TestableOB> ob;
  int sym = 0;
  uint64_t next_ex_order_id = 1000;
  int tick_n = 0;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  void SetUp() override {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr);
    sym = asset->id;

    ob = std::make_unique<TestableOB>(nullptr, true, nullptr, sym, pt);
    probe.clear();
    feed.clear();

    // Register the feed subscriber so publish_delayed has somewhere to fan
    // out to. Without it pub_q would stay empty and the inbound assertions
    // would pass vacuously.
    auto sub = new mda::msg::Subscribe(mda::msg::Subscribe::HI);
    sub->sender = &feed;
    ob->reply_to = &feed;
    ob->process_message(sub);
    ob->reply_to = nullptr;
    feed.clear();
  }

  void TearDown() override {
    // The base class calls feed.clear() explicitly; here feed is a member of
    // this fixture and OB holds it in hiprio_datasubs and pub_q. Reset OB
    // first so pub_q is torn down while feed is still alive.
    ob.reset();
    probe.clear();
    feed.clear();
  }

  void dispatch(const actors::Message* m) {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), m, &probe);
    ob->reply_to = nullptr;
  }

  void market_add(en::bs side, int px, uint32_t qty, uint64_t txtim) {
    const uint64_t exid = next_ex_order_id++;
    bfile::l3_mbo_v2_t mbo{};
    mbo.typ               = en::l3::MBO_V2;
    mbo.venue             = char(en::x::CMEMD);
    mbo.transactTime      = txtim;
    mbo.sendingTime       = txtim;
    mbo.orderUpdateAction = 0;
    mbo.orderID           = exid;
    mbo.priority          = exid;
    mbo.pxd               = double(px);
    mbo.displayQty        = qty;
    mbo.side              = (side == en::bs::BUY) ? '0' : '1';
    mbo.endOfEvent        = true;

    auto d = std::make_unique<mda::msg::Data>();
    d->israw  = true;
    d->l3     = mbo;
    d->payload = mda::msg::data_pay_load::make_payload(
        txtim, side, exid, mda::OrderID::longid(en::x::CMEMD, exid), sym,
        en::md::ADD, en::mt::NONE, qty, qty, double(px), txtim, txtim,
        en::x::CMEMD, true, false);
    dispatch(d.get());
  }

  void our_order(en::bs side, int px, uint32_t qty, uint64_t ts0, uint oid,
                 en::md mev, en::mt action) {
    auto d = std::make_unique<mda::msg::Data>();
    d->israw = false;
    d->l3    = bfile::l3_sim_t();

    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(
        new mda::msg::data_pay_load());
    payload->mkt        = en::x::SIM;
    payload->sym        = sym;
    payload->px         = ref::Price(px, sym);
    payload->sz         = qty;
    payload->disp_sz    = (action == en::mt::CANCD) ? 0 : qty;
    payload->side       = side;
    payload->order_ref  = mda::OrderID::longid(en::x::SIM, oid);
    payload->action     = action;
    payload->ot         = en::ot::LIMIT;
    payload->mev        = mev;
    payload->owner      = en::trader::SIMULATOR;
    payload->ts0        = ts0;
    d->payload = payload;
    dispatch(d.get());
  }

  void place_ours(int px, uint64_t ts0, uint oid = 1, uint32_t qty = 1) {
    our_order(en::bs::BUY, px, qty, ts0, oid, en::md::ADD, en::mt::NONE);
  }

  int our_size_at(int px, en::bs side = en::bs::BUY) {
    probe.clear();
    cons::msg::Get g("qat", {{"side", side == en::bs::BUY ? "B" : "S"},
                             {"px", std::to_string(px)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    if (!page) return -1;
    int sz = -1, cnt = -1, simsz = -1;
    std::sscanf(page->val.c_str(), "QAT: %d %d %d", &sz, &cnt, &simsz);
    return simsz;
  }

  void seed_book(uint64_t txtim) {
    market_add(en::bs::BUY, kBidPx, 10, txtim);
    market_add(en::bs::SEL, kAskPx, 10, txtim + 1);
  }

  void tick(uint64_t txtim) {
    // A harmless SEL record deep enough not to disturb the touch. Each tick
    // drains pub_q entries whose stamp is <= txtim and then queues its own
    // EndOfBurst behind the new head-of-queue delay.
    market_add(en::bs::SEL, kAskPx + 100 + tick_n++, 1, txtim);
  }
};

// -----------------------------------------------------------------------------
// Inbound queue: Lindley recursion drives pub_q release times.
//
// Called directly via publish_delayed so the assertion is on the recursion
// itself, not on the record-by-record choreography of when EndOfBurst is
// emitted. The reference computes d_i = max(a_i, d_{i-1}) + s off the same
// arrival stream and compares to get_last_release_inbound_ns() after each call.
// -----------------------------------------------------------------------------

// The identity: publish_delayed must produce the Lindley d_i for the sequence
// {a_i} passed to it under service_us_inbound = 7. Two regimes exercised:
// - a_i < d_{i-1} (busy server): w_i > s, arrivals stack.
// - a_i >= d_{i-1} (idle server): w_i = s.
TEST_F(OBTailDelayTest, InboundPublishDelayedMatchesLindleyRecursion) {
  constexpr int s_in_us = 7;
  constexpr uint64_t s_in_ns = uint64_t(s_in_us) * 1000;
  ob->set_service_us_inbound(s_in_us);

  // Arrival stream chosen to hit both regimes. a_2 and a_3 land inside
  // {d_1, d_2}'s service intervals; a_4 lands well past d_3.
  const std::vector<uint64_t> arrivals = {
      kT0,
      kT0 + s_in_ns / 3,       // busy: d_1 = a_1 + s, so w_2 > s
      kT0 + 2 * s_in_ns / 3,   // still busy
      kT0 + 100 * s_in_ns,     // idle: w_4 = s
  };

  uint64_t d_prev = 0;
  for (size_t i = 0; i < arrivals.size(); ++i) {
    const uint64_t a = arrivals[i];
    // A payload-less publish is legitimate for the recursion test -- OB never
    // inspects the message here; publish_delayed only stamps and queues it.
    auto* m = new frame::ob::msg::EndOfBurst();
    ob->publish_delayed(&feed, m, a);

    const uint64_t d_expected = std::max(a, d_prev) + s_in_ns;
    EXPECT_EQ(ob->get_last_release_inbound_ns(), d_expected)
        << "recursion mismatch at i=" << i
        << " a=" << a << " d_prev=" << d_prev;
    d_prev = d_expected;
  }

  // pub_q holds one entry per publish (nothing was drained), and every stamp
  // must be monotone.
  EXPECT_EQ(ob->pub_q_size(), arrivals.size())
      << "publish_delayed must queue, not send inline";
}

// A message with no market timestamp (now == 0) bypasses the recursion and
// sends inline. This mirrors the constant-delay contract: an untimed message
// is applied at once rather than parked with a fabricated stamp.
TEST_F(OBTailDelayTest, InboundUntimedMessageBypassesRecursion) {
  ob->set_service_us_inbound(7);

  const uint64_t before = ob->get_last_release_inbound_ns();
  auto* m = new frame::ob::msg::EndOfBurst();
  ob->publish_delayed(&feed, m, 0);

  EXPECT_EQ(ob->get_last_release_inbound_ns(), before)
      << "an untimed publish must not advance the recursion state";
  EXPECT_EQ(ob->pub_q_size(), 0u) << "and must not queue";
  EXPECT_EQ(feed.count_messages_of_type<frame::ob::msg::EndOfBurst>(), 1u)
      << "it must go through inline instead";
}

// -----------------------------------------------------------------------------
// Outbound queue: burst of orders inside one service-time interval must stack.
// -----------------------------------------------------------------------------

// The claim of §5.2: k orders arriving inside one s_out interval on the
// outbound queue release at a_1 + s, a_1 + 2s, ..., a_1 + k*s. The constant
// delay would release all three at a_i + s, so this cleanly separates the two.
TEST_F(OBTailDelayTest, OutboundLindleyStacksBurstOrders) {
  const int s_out_us = 500;
  const uint64_t s_out_ns = uint64_t(s_out_us) * 1000;
  // Service time inbound must be non-zero too, otherwise our_size_at reads may
  // stall unexpectedly; but this test observes the outbound side only.
  ob->set_service_us_inbound(1);
  ob->set_service_us_outbound(s_out_us);

  seed_book(kT0);

  // Three orders at three different prices, all sent inside one s_out window.
  const uint64_t a1 = kT0 + 10;
  place_ours(kOurPx - 2, a1,               1);
  place_ours(kOurPx - 3, a1 + s_out_ns/4,  2);
  place_ours(kOurPx - 4, a1 + s_out_ns/2,  3);

  // A market record whose txtim exceeds a_1 + s but not a_1 + 2s: exactly the
  // first order should have been released.
  market_add(en::bs::SEL, kAskPx + 1, 1, a1 + s_out_ns + s_out_ns/8);
  EXPECT_EQ(our_size_at(kOurPx - 2), 1)
      << "order 1 must release at d_1 = a_1 + s";
  EXPECT_EQ(our_size_at(kOurPx - 3), 0)
      << "order 2's release is d_2 = d_1 + s, not yet reached";
  EXPECT_EQ(our_size_at(kOurPx - 4), 0)
      << "order 3's release is d_3 = d_1 + 2s, not yet reached";

  // Past d_2 = a_1 + 2s but not d_3.
  market_add(en::bs::SEL, kAskPx + 2, 1, a1 + 2 * s_out_ns + s_out_ns/8);
  EXPECT_EQ(our_size_at(kOurPx - 3), 1)
      << "order 2 must release at d_1 + s";
  EXPECT_EQ(our_size_at(kOurPx - 4), 0)
      << "order 3's release is d_1 + 2s, not yet reached";

  // Past d_3.
  market_add(en::bs::SEL, kAskPx + 3, 1, a1 + 3 * s_out_ns + s_out_ns/8);
  EXPECT_EQ(our_size_at(kOurPx - 4), 1)
      << "order 3 must release at d_2 + s = a_1 + 3s";
}

// A second, isolated order after the first has released. w_2 = s alone, not
// 2s. This asserts that state is advanced only on release, not on every
// recomputation -- an implementation that updated d_{i-1} at each process_q
// pass over the same order would produce a runaway.
TEST_F(OBTailDelayTest, OutboundIdleServerGivesWEqualsS) {
  const int s_out_us = 500;
  const uint64_t s_out_ns = uint64_t(s_out_us) * 1000;
  ob->set_service_us_inbound(1);
  ob->set_service_us_outbound(s_out_us);

  seed_book(kT0);

  const uint64_t a1 = kT0 + 10;
  place_ours(kOurPx - 2, a1, 1);
  market_add(en::bs::SEL, kAskPx + 1, 1, a1 + s_out_ns + s_out_ns/8);
  ASSERT_EQ(our_size_at(kOurPx - 2), 1) << "order 1 released as expected";

  // Second, isolated order well after order 1's release: idle server.
  const uint64_t a2 = a1 + 100 * s_out_ns;
  place_ours(kOurPx - 3, a2, 2);

  // Just under a_2 + s: the release stamp of order 2 is d_2 = a_2 + s alone.
  market_add(en::bs::SEL, kAskPx + 2, 1, a2 + s_out_ns - 1);
  EXPECT_EQ(our_size_at(kOurPx - 3), 0);

  market_add(en::bs::SEL, kAskPx + 3, 1, a2 + s_out_ns + 1);
  EXPECT_EQ(our_size_at(kOurPx - 3), 1)
      << "the second order's release is a_2 + s, not a_2 + 2s";
}

}  // namespace

#else   // OB_TAIL_DELAY undefined

// Register at least one no-op test so the file has something to compile down to
// under the constant-delay build. GoogleTest treats an empty translation unit
// with no TEST_F as a link-time no-op, which is fine, but the explicit
// placeholder makes the intent visible in the run summary.
TEST(OBTailDelay, CompiledOutUnderConstantDelayBuild) {
  GTEST_SKIP() << "OB_TAIL_DELAY undefined; see paper §5.2";
}

#endif  // OB_TAIL_DELAY
