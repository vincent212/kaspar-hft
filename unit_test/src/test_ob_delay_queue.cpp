/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * OB's delay queue — the simulator's entire latency model.
 *
 * OB does not apply our own orders to the book when it receives them. It parks
 * them on del_q (OB.cpp:1507, keyed off `israw == false`) and releases each one
 * only once
 *
 *     payload->ts0 + max(40, delay) * 1000 ns
 *
 * has passed in MARKET time — the transactTime of the next real record. That
 * is what makes the fill model honest: everything the market does in that
 * window gets in front of us.
 *
 * Nothing tested it. `set_delay()` existed with no caller, so every run used
 * the hardcoded 1000 us, and the cancel path fed it a timestamp that had
 * already expired (see test_som_cancel_latency.cpp), which no test noticed
 * either.
 *
 * These tests drive a real OB with real MBO records and observe the book
 * through the one public channel it has — cons::msg::Get("bbbo") — so they
 * assert on what a consumer of the book actually sees, not on internals.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <map>
#include <string>
#include <utility>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

// OB::get_handler replies, and Actor::reply() asserts on a null return
// address. The run loop sets reply_to before dispatch; TestHelper does not.
// reply_to is protected in actors::Actor, so a using-declaration reaches it.
struct TestableOB : public ob::act::OB {
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;

  // Not every message OB understands arrives through the MESSAGE_HANDLER map.
  // Subscribe is dispatched by typeid inside OB::process_message (OB.cpp:1405),
  // which TestHelper::invoke_handler never reaches -- it only walks the public
  // handlers map, so a Subscribe sent that way is silently dropped and the
  // publish fan-out stays empty. OB's override is private; the base's
  // declaration is protected, and the call is virtual, so naming Actor's
  // reaches OB's.
  using actors::Actor::process_message;
};

constexpr uint64_t kT0       = 1736960000000000000ULL;  // epoch ns
constexpr int      kDelayUs  = 1000;                    // OB's own default
constexpr uint64_t kDelayNs  = uint64_t(kDelayUs) * 1000;

constexpr int kBidPx = 23990;
constexpr int kAskPx = 24010;
constexpr int kOurPx = 24000;   // inside the spread, so it moves the BBO

class OBDelayQueueTest : public ::testing::Test {
protected:
  MockActor probe{"probe"};
  boost::property_tree::ptree pt;
  std::unique_ptr<TestableOB> ob;
  int sym = 0;
  uint64_t next_ex_order_id = 1000;

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

    ob = std::make_unique<TestableOB>(nullptr, true /*do_cross_check*/,
                                      nullptr /*manager*/, sym, pt);
    probe.clear();
  }

  void TearDown() override { probe.clear(); }

  void dispatch(const actors::Message* m) {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), m, &probe);
    ob->reply_to = nullptr;
  }

  // A real exchange MBO record. `israw` is what tells OB this is market data
  // and not one of our own orders, so it bypasses del_q and is applied at once
  // -- and, on the way in, drains whatever on del_q has now come due.
  void market_add(en::bs side, int px, uint32_t qty, uint64_t txtim,
                  uint64_t* out_ex_order_id = nullptr) {
    const uint64_t exid = next_ex_order_id++;
    if (out_ex_order_id) *out_ex_order_id = exid;

    bfile::l3_mbo_v2_t mbo{};
    mbo.typ               = en::l3::MBO_V2;
    mbo.venue             = char(en::x::CMEMD);   // handler_if stores the en::x value
    mbo.transactTime      = txtim;
    mbo.sendingTime       = txtim;
    mbo.orderUpdateAction = 0;   // ADD
    mbo.orderID           = exid;
    mbo.priority          = exid;
    mbo.pxd               = double(px);
    mbo.displayQty        = qty;
    mbo.side              = (side == en::bs::BUY) ? '0' : '1';  // MDP3 coding
    mbo.endOfEvent        = true;

    auto d = std::make_unique<mda::msg::Data>();
    d->israw  = true;
    d->l3     = mbo;
    d->payload = mda::msg::data_pay_load::make_payload(
        txtim, side, exid, mda::OrderID::longid(en::x::CMEMD, exid), sym,
        en::md::ADD, en::mt::NONE, qty, qty, double(px), txtim, txtim,
        en::x::CMEMD, true /*eoe*/, false /*recovery*/);
    dispatch(d.get());
  }

  // One of OUR orders, shaped the way SOM shapes it: israw false, l3_sim_t,
  // venue SIM, and ts0 = the market time we decided to act.
  void our_order(en::bs side, int px, uint32_t qty, uint64_t ts0, uint oid,
                 en::md mev, en::mt action) {
    auto d = std::make_unique<mda::msg::Data>();
    d->israw = false;
    d->l3    = bfile::l3_sim_t();   // exactly as SOM builds it

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

  void cancel_ours(int px, uint64_t ts0, uint oid = 1, uint32_t qty = 1) {
    our_order(en::bs::BUY, px, qty, ts0, oid, en::md::MOD, en::mt::CANCD);
  }

  // The market BBO, which deliberately EXCLUDES our own orders.
  std::pair<int, int> bbo() {
    probe.clear();
    cons::msg::Get g("bbbo");
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    if (!page) return {-1, -1};
    int bid = -1, ask = -1;
    std::sscanf(page->val.c_str(), "BBBO: %d %d", &bid, &ask);
    return {bid, ask};
  }

  // Our own resting size at a price. This is the observable that matters:
  // "bbbo" cannot answer it, because sim orders are kept out of the BBO so a
  // strategy cannot react to its own quote.
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

  // Build a two-sided book with a 20-tick spread, wide enough that our order
  // can sit inside it without locking or crossing.
  void seed_book(uint64_t txtim) {
    market_add(en::bs::BUY, kBidPx, 10, txtim);
    market_add(en::bs::SEL, kAskPx, 10, txtim + 1);
  }
};

TEST_F(OBDelayQueueTest, SeededBookHasTheExpectedBbo) {
  seed_book(kT0);
  EXPECT_EQ(bbo(), std::make_pair(kBidPx, kAskPx));
  EXPECT_EQ(our_size_at(kOurPx), 0) << "we have not placed anything yet";
}

// Guards the observation channel itself: if our order ever started showing up
// in the BBO, every timing test below would be measuring the wrong thing.
TEST_F(OBDelayQueueTest, OurOwnOrderIsNotVisibleInTheMarketBbo) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);

  ASSERT_EQ(our_size_at(kOurPx), 1) << "precondition: our order is resting";
  EXPECT_EQ(bbo().first, kBidPx)
      << "a strategy must not see its own quote as the market bid";
}

// The core of the model: our order is NOT in the book the instant OB gets it.
TEST_F(OBDelayQueueTest, OurOrderIsWithheldUntilTheDelayHasElapsed) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "our order must not be in the book before any time has passed";

  // A record arriving mid-flight: still not ours.
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs / 2);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "released after only half the wire latency";

  // A record past the deadline drains it.
  market_add(en::bs::SEL, kAskPx + 6, 1, sent + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "our order should be in the book once the latency has elapsed";
}

// The regression test_som_cancel_latency.cpp pins at the SOM end, observed
// here at the book end: a cancel stamped with the ORIGINAL order's ts0 has a
// deadline that is already past, so it takes effect on the very next record.
TEST_F(OBDelayQueueTest, OurCancelIsWithheldForTheSameDelay) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1) << "precondition: our order is resting";

  const uint64_t cancelled_at = sent + kDelayNs + 100;
  cancel_ours(kOurPx, cancelled_at);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "cancel must not take effect on the message that carried it";

  market_add(en::bs::SEL, kAskPx + 6, 1, cancelled_at + kDelayNs / 2);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "cancel took effect after only half the wire latency -- this is what "
         "stamping it with the original order's ts0 used to do";

  market_add(en::bs::SEL, kAskPx + 7, 1, cancelled_at + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "cancel should take effect once its own latency has elapsed";
}

// The knob has to move the threshold, or it is decorative.
TEST_F(OBDelayQueueTest, SetDelayChangesHowLongTheOrderIsHeld) {
  constexpr int kSlowUs = 50000;              // 50 ms
  constexpr uint64_t kSlowNs = uint64_t(kSlowUs) * 1000;

  ob->set_delay(kSlowUs);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);

  // Past the DEFAULT deadline, nowhere near the configured one.
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "released at the default 1000us, so set_delay() did nothing";

  market_add(en::bs::SEL, kAskPx + 6, 1, sent + kSlowNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1);
}

// 40 us is a hard floor: max(40, delay). A zero or silly-small delay must not
// turn into "arrives instantly", which would quietly restore the old bug.
TEST_F(OBDelayQueueTest, DelayHasAFortyMicrosecondFloor) {
  ob->set_delay(0);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + 20000);   // +20 us
  EXPECT_EQ(our_size_at(kOurPx), 0) << "20us is inside the 40us floor";

  market_add(en::bs::SEL, kAskPx + 6, 1, sent + 41000);   // +41 us
  EXPECT_EQ(our_size_at(kOurPx), 1);
}

// The cancel latency defaults to -1 (= the order latency) and may only be made
// LONGER, which is the direction real venues go: the matching engine has to
// locate the resting order before it can pull it.
TEST_F(OBDelayQueueTest, CancelDelayCanBeMadeAsymmetric) {
  constexpr int kSlowCancelUs = 50000;
  constexpr uint64_t kSlowCancelNs = uint64_t(kSlowCancelUs) * 1000;

  ob->set_delay(kDelayUs, kSlowCancelUs);   // order at the default, cancel slower
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  // Placement still uses the ordinary delay.
  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1) << "cancel_delay must not slow placement down";

  const uint64_t cancelled_at = sent + kDelayNs + 100;
  cancel_ours(kOurPx, cancelled_at);

  market_add(en::bs::SEL, kAskPx + 6, 1, cancelled_at + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "cancel used the order delay, not the configured cancel delay";

  market_add(en::bs::SEL, kAskPx + 7, 1, cancelled_at + kSlowCancelNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0);
}



// A cancel may not be modelled as FASTER than a new order: the matching engine
// has to locate the resting order before it can pull it, so on a real venue
// the cancel path is the slower one. Getting this backwards would flatter the
// shadow -- our cancels would beat flow they should have worn.
// ---------------------------------------------------------------------------
// INBOUND FEED DELAY
//
// The mirror of everything above. del_q holds our ORDERS on the way out;
// pub_q holds MARKET DATA on the way in. Without the inbound leg the model is
// asymmetric in a way that invalidates any latency arm: the light saw the book
// instantly and acted at `delay`, so the gap between its place and its cancel
// was set by events it saw with no delay and survived the latency intact.
// ---------------------------------------------------------------------------

// A subscribed consumer of the feed. Every other test in this file asserts on
// BOOK state, so the publish fan-out has no target and nothing ever reaches
// pub_q -- a withholding test written without this passes vacuously.
class FeedDelayTest : public OBDelayQueueTest {
protected:
  // A LANDMINE, deliberately left armed and labelled rather than defused.
  //
  // `feed` is a member of this DERIVED fixture while `ob` is a member of the
  // base, so derived members are destroyed first: `feed` dies while `ob` is
  // still alive and still holds `&feed` in both hiprio_datasubs and pub_q. Most
  // cases below deliberately end with a non-empty pub_q.
  //
  // Harmless today only because ~OB never touches pub_q -- nothing is flushed
  // at destruction, which is issue-worthy in its own right and was reviewed as
  // such (deliberately not fixed: every sweep runs feed_delay 0, where pub_q is
  // never used). The moment anyone adds that flush to ~OB or OB::end(),
  // MarketDataIsWithheldThenReleased, EverythingQueuedIsEventuallyReleased,
  // ReleaseIsKeyedToMarketTimeNotWallClock and ACancelAckPaysTheFeedDelay all
  // send into a MockActor whose destructor has already run clear() and freed
  // its captured vector -- a heap-use-after-free in teardown that ASAN will
  // blame on the flush rather than on this line.
  //
  // If you are here because of that: move `feed` into the base fixture, or
  // reset `ob` in TearDown before `feed` dies.
  MockActor feed{"feed"};
  int tick_n = 0;

  void SetUp() override {
    OBDelayQueueTest::SetUp();
    // Subscribe has no MESSAGE_HANDLER; it is typeid-dispatched inside
    // OB::process_message (OB.cpp:1405), so it has to be delivered there
    // directly. Heap-allocated because OB's dispatch takes ownership.
    auto sub = new mda::msg::Subscribe(mda::msg::Subscribe::HI);
    sub->sender = &feed;
    ob->reply_to = &feed;
    ob->process_message(sub);
    ob->reply_to = nullptr;
    feed.clear();
  }

  void TearDown() override {
    feed.clear();
    OBDelayQueueTest::TearDown();
  }

  size_t bursts() const {
    return feed.count_messages_of_type<frame::ob::msg::EndOfBurst>();
  }

  // One harmless record, deep enough not to disturb the BBO. OB emits the
  // EndOfBurst for record N only when record N+1 arrives -- handle_eoburst
  // fires on `have_new_payload`, which the PREVIOUS record set -- so a burst
  // has to be ticked out before there is anything to withhold. Each tick also
  // drains whatever on pub_q its own timestamp has made due.
  void tick(uint64_t txtim) {
    market_add(en::bs::SEL, kAskPx + 100 + tick_n++, 1, txtim);
  }
};

// The subscription itself, asserted before anything depends on it. If this
// fails, every withholding test below is measuring an empty fan-out rather
// than a delay.
TEST_F(FeedDelayTest, TheProbeIsActuallySubscribed) {
  seed_book(kT0);
  tick(kT0 + 100);
  EXPECT_GT(bursts(), 0u)
      << "no EndOfBurst reached the subscriber, so the Subscribe never "
         "registered -- the withholding tests would pass vacuously";
  EXPECT_EQ(ob->pub_q_size(), 0u) << "feed_delay is 0 here, so nothing queues";
}

// The default path must stay the path that existed before the queue was added.
//
// This lived in OBDelayQueueTest, which registers no data subscriber -- so the
// publish fan-out had no target, nothing could ever reach pub_q, and
// `pub_q_size() == 0` held for ANY feed_delay. It passed with the fast path
// deleted. Here the probe IS subscribed, so the zero and non-zero cases
// genuinely differ, which is what the assertion claims to show.
TEST_F(FeedDelayTest, ZeroFeedDelayPublishesInlineAndNonZeroDoesNot) {
  ob->set_feed_delay(0);
  seed_book(kT0);
  tick(kT0 + 1000);
  EXPECT_EQ(ob->pub_q_size(), 0u)
      << "feed_delay 0 must publish inline, not through the queue";
  EXPECT_GT(bursts(), 0u)
      << "and the subscriber must actually have been handed it";

  // The same fixture, the same records, a non-zero delay: now it queues. Without
  // this half the test cannot tell an inline publish from an empty fan-out.
  const size_t before = bursts();
  ob->set_feed_delay(2000);
  tick(kT0 + 2000);
  tick(kT0 + 3000);
  EXPECT_GT(ob->pub_q_size(), 0u) << "a non-zero delay must queue";
  EXPECT_EQ(bursts(), before) << "and must not deliver on the producing record";
}

// The claim, stated directly: with a feed delay set, a book update does not
// reach the subscriber on the message that produced it. It sits on pub_q until
// MARKET time has advanced past the stamp, and then it is released.
TEST_F(FeedDelayTest, MarketDataIsWithheldThenReleased) {
  constexpr int kFeedUs = 2000;
  constexpr uint64_t kFeedNs = uint64_t(kFeedUs) * 1000;
  ob->set_feed_delay(kFeedUs);

  seed_book(kT0);
  tick(kT0 + 100);          // emits the burst for the seeded book
  EXPECT_EQ(bursts(), 0u)
      << "the subscriber saw the book on the very record that changed it";
  const size_t queued = ob->pub_q_size();
  EXPECT_GT(queued, 0u) << "withheld means queued, not dropped";

  // Market time advances, but not far enough.
  tick(kT0 + kFeedNs / 2);
  EXPECT_EQ(bursts(), 0u) << "released after only half the feed latency";
  EXPECT_GT(ob->pub_q_size(), queued) << "and the new update queued behind it";

  // Past the stamp on the first update: it comes out.
  tick(kT0 + kFeedNs + 1);
  EXPECT_GT(bursts(), 0u)
      << "still withheld after the feed latency has elapsed";
}

// Everything queued must eventually come out. A release rule that strands
// messages would starve the light rather than delay it -- and would look
// identical to "withheld" in the test above.
TEST_F(FeedDelayTest, EverythingQueuedIsEventuallyReleased) {
  ob->set_feed_delay(2000);

  seed_book(kT0);
  tick(kT0 + 1000);
  tick(kT0 + 2000);
  ASSERT_GT(ob->pub_q_size(), 0u);

  // Two records far past every stamp on the queue. Two, because each record
  // drains BEFORE it publishes, so the burst a record emits is always still in
  // flight when it returns.
  tick(kT0 + 10ULL * 1000 * 1000 * 1000);
  tick(kT0 + 30ULL * 1000 * 1000 * 1000);

  const size_t released = bursts();
  EXPECT_GE(released, 3u) << "nothing came out at all";
  ASSERT_EQ(ob->pub_q_size(), 1u)
      << "only the burst this last record just emitted may still be queued; "
         "anything more was stranded";

  // And that last one is not stranded either -- it comes out on the next
  // record whose timestamp is past its stamp.
  tick(kT0 + 60ULL * 1000 * 1000 * 1000);
  EXPECT_EQ(bursts(), released + 1)
      << "the burst left in flight never came out";
}

// The release must be keyed to the record's transactTime, not to the wall
// clock. This is the mistake the gunning-protection path made -- it compared
// against chutil::Time::epoch(), which is system_clock::now() -- and in a
// replay that runs a year of market data in an hour, a wall-clock rule
// releases everything instantly and the delay is decorative.
TEST_F(FeedDelayTest, ReleaseIsKeyedToMarketTimeNotWallClock) {
  constexpr int kHugeFeedUs = 10 * 1000 * 1000;            // 10 s
  constexpr uint64_t kHugeFeedNs = uint64_t(kHugeFeedUs) * 1000;
  ob->set_feed_delay(kHugeFeedUs);

  seed_book(kT0);
  tick(kT0 + 100);
  ASSERT_GT(ob->pub_q_size(), 0u);

  // Ten seconds of WALL clock would have passed by now in a real feed; here it
  // is microseconds. A rule that read the system clock would still be short,
  // so nudge market time forward only trivially and check nothing escapes.
  for (int i = 0; i < 20; i++)
    tick(kT0 + 200 + uint64_t(i) * 1000);
  EXPECT_EQ(bursts(), 0u)
      << "released without market time advancing -- the deadline is being "
         "compared against something other than transactTime";

  // Now advance MARKET time past the stamp.
  tick(kT0 + kHugeFeedNs + 1);
  EXPECT_GT(bursts(), 0u) << "market time passed the stamp and nothing came out";
}

// Feed delay holds data, it does not reorder it. A consumer that saw updates
// out of order would reconstruct a book the market never had.
TEST_F(FeedDelayTest, ReleaseOrderMatchesArrivalOrder) {
  ob->set_feed_delay(2000);
  seed_book(kT0);
  for (int i = 0; i < 5; i++)
    tick(kT0 + uint64_t(i + 1) * 100);

  tick(kT0 + 10ULL * 1000 * 1000 * 1000);

  uint64_t prev = 0;
  size_t seen = 0;
  for (size_t i = 0; i < feed.message_count(); i++) {
    auto eob = feed.get_message<frame::ob::msg::EndOfBurst>(i);
    if (!eob || !eob->payload) continue;
    EXPECT_GE(eob->payload->txtim_epoch, prev)
        << "EndOfBurst " << i << " went backwards in market time";
    prev = eob->payload->txtim_epoch;
    seen++;
  }
  EXPECT_GT(seen, 1u) << "need more than one release to check the ordering";
}

// A cancel-ack is an EXCHANGE event -- it says the matching engine pulled the
// order -- so it pays the inbound hop like a book update does. Without this a
// light learns its order is gone before it could possibly have seen it, which
// is the other half of the asymmetry the feed delay exists to remove.
TEST_F(FeedDelayTest, ACancelAckPaysTheFeedDelay) {
  constexpr int kFeedUs = 2000;
  constexpr uint64_t kFeedNs = uint64_t(kFeedUs) * 1000;
  ob->set_feed_delay(kFeedUs);
  ob->set_delay(kDelayUs);
  seed_book(kT0);

  const uint64_t sent = kT0 + 10;
  place_ours(kOurPx, sent);
  tick(sent + kDelayNs + kFeedNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1) << "precondition: our order is resting";

  const uint64_t cancelled_at = sent + kDelayNs + kFeedNs + 100;
  cancel_ours(kOurPx, cancelled_at);

  // The record that carries the cancel past its deadline: the book pulls the
  // order here, so the ack is GENERATED here.
  const uint64_t applied = cancelled_at + kDelayNs + kFeedNs + 1;
  tick(applied);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancAck>(), 0u)
      << "the ack came back on the very record that produced it";

  tick(applied + kFeedNs + 1);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancAck>(), 1u)
      << "the ack never arrived";
}

// THE ORDERING RULE, and the reason the whole feature aborted on real data.
//
// An order that crosses on arrival is filled and deleted -- it never rests --
// so a cancel for it finds nothing and OB answers CancReject(NOTFOUND). The
// fill and the reject are about the SAME order, and OB used to hold the fill
// on pub_q while sending the reject inline. The reject overtook the fill: the
// light cleared its slot on the reject, placed the next order, and then the
// fill for the previous one landed ("fill for wrong order id"), stranding the
// new order in QCoord until an add at that price tripped "already have this mm
// id" and killed the run. Every exchange event pays the same hop, or they
// reorder.
TEST_F(FeedDelayTest, AnExchangeRejectPaysTheSameDelayAsAFill) {
  constexpr int kFeedUs = 2000;
  constexpr uint64_t kFeedNs = uint64_t(kFeedUs) * 1000;
  ob->set_feed_delay(kFeedUs);
  ob->set_delay(kDelayUs);
  seed_book(kT0);

  // Priced AT the offer, so it crosses and is filled on arrival rather than
  // resting -- which is what leaves a cancel with nothing to find.
  const uint64_t sent = kT0 + 10;
  our_order(en::bs::BUY, kAskPx, 1, sent, 7, en::md::ADD, en::mt::NONE);
  tick(sent + kDelayNs + kFeedNs + 1);
  // Read the mailbox BEFORE our_size_at, whose first statement is
  // probe.clear() -- which deletes every captured message. Asserting after it
  // read a freshly emptied mailbox and was 0 whether the fill had been withheld
  // or sent inline, so the Fill half of this test detected nothing.
  const size_t fills_on_the_producing_record =
      probe.count_messages_of_type<frame::som::msg::Fill>();
  ASSERT_EQ(our_size_at(kAskPx), 0) << "precondition: it crossed, it is not resting";
  EXPECT_EQ(fills_on_the_producing_record, 0u)
      << "the fill came back on the record that produced it";

  // Cancel the order that no longer exists.
  const uint64_t cancelled_at = sent + kDelayNs + kFeedNs + 100;
  our_order(en::bs::BUY, kAskPx, 1, cancelled_at, 7, en::md::MOD, en::mt::CANCD);
  const uint64_t applied = cancelled_at + kDelayNs + kFeedNs + 1;
  tick(applied);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancReject>(), 0u)
      << "the reject went out inline while the fill waited -- it can now "
         "overtake the fill for the same order";

  tick(applied + kFeedNs + 1);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancReject>(), 1u)
      << "the reject never arrived";
}

// One OB per instrument is the normal case -- SimKaspr builds one per asset in
// the universe. The return path was a static hook at first, which meant the
// book constructed last published every book's fills and cancel-acks: wrong
// queue, wrong clock, and a dangling `this` once that book was destroyed (it
// segfaulted the suite). Standing up a second book must not touch the first's.
//
// The second book is given a DELIBERATELY HUGE delay, and that is the whole
// design of the test. An earlier version gave it feed_delay 0 and asserted
// `other->pub_q_size() == 0` and `ob->pub_q_size() > 0` -- both tautologies: a
// zero-delay book can never queue anything, and `ob` queues its own EndOfBursts
// whatever happens to the ack. Both passed under the static-hook regression
// they claimed to catch. With a 10 s second book the two worlds separate
// cleanly: the correct one releases the ack on ob's 2 ms, the regression would
// publish it through `other` and hold it for 10 s.
TEST_F(FeedDelayTest, ASecondBookDoesNotStealTheFirstsReturnPath) {
  constexpr int kFeedUs = 2000;
  constexpr uint64_t kFeedNs = uint64_t(kFeedUs) * 1000;
  constexpr uint64_t kOtherFeedNs = 10ULL * 1000 * 1000 * 1000;   // 10 s
  ob->set_feed_delay(kFeedUs);
  ob->set_delay(kDelayUs);
  seed_book(kT0);

  const uint64_t sent = kT0 + 10;
  place_ours(kOurPx, sent);
  tick(sent + kDelayNs + kFeedNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1);

  // A second book for the same instrument, an order of magnitude slower.
  boost::property_tree::ptree pt2;
  auto other = std::make_unique<TestableOB>(nullptr, true, nullptr, sym, pt2);
  other->set_feed_delay(10 * 1000 * 1000);

  const uint64_t cancelled_at = sent + kDelayNs + kFeedNs + 100;
  cancel_ours(kOurPx, cancelled_at);
  const uint64_t applied = cancelled_at + kDelayNs + kFeedNs + 1;
  tick(applied);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancAck>(), 0u)
      << "the ack went out on the record that produced it";

  // Past THIS book's delay. Under the static hook the ack would have been
  // stamped with the other book's 10 s and would still be in flight.
  tick(applied + kFeedNs + 1);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancAck>(), 1u)
      << "the ack did not arrive on this book's delay -- it is being published "
         "through the second book's publisher, with the second book's clock";
  EXPECT_EQ(other->pub_q_size(), 0u)
      << "the second book queued the first book's ack";

  // And it was not merely early: nothing further arrives once it has.
  tick(applied + kOtherFeedNs + 1);
  EXPECT_EQ(probe.count_messages_of_type<frame::som::msg::CancAck>(), 1u)
      << "a second copy of the ack arrived on the other book's clock";
}

TEST_F(OBDelayQueueTest, FeedDelayDefaultsToZero) {
  // The whole point of the default: every run that predates this change must
  // behave identically. A non-zero default would silently reinterpret them.
  seed_book(kT0);
  EXPECT_EQ(ob->get_feed_delay(), 0)
      << "a non-zero default would change every existing result";
}

TEST_F(OBDelayQueueTest, SetFeedDelayRejectsNegative) {
  EXPECT_DEATH(ob->set_feed_delay(-1), "feed latency");
}



TEST_F(OBDelayQueueTest, TheRoundTripIsFeedPlusOrder) {
  // The decisive one. ts0 is the market time the light SAW, already feed_delay
  // old, so an order must arrive at ts0 + feed + order. Leaving del_q at
  // ts0 + order makes every order one feed hop too early -- which is the
  // asymmetry this change exists to remove.
  const int kFeedUs = 2000;
  ob->set_feed_delay(kFeedUs);
  ob->set_delay(kDelayUs);
  seed_book(kT0);

  const uint64_t sent = kT0 + 10;
  place_ours(kOurPx, sent);

  // Past the ORDER delay alone, but not past feed + order: still not in.
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "arrived at ts0 + order, ignoring the feed hop the light already paid";

  // Past feed + order: in.
  market_add(en::bs::SEL, kAskPx + 6, 1,
             sent + kDelayNs + uint64_t(kFeedUs) * 1000 + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "should be in the book once feed + order has elapsed";
}

TEST_F(OBDelayQueueTest, ACancelFasterThanAnOrderIsRejected) {
  EXPECT_DEATH(ob->set_delay(1000, 500), "cancel latency");
}

TEST_F(OBDelayQueueTest, EqualLatenciesAreAccepted) {
  ob->set_delay(1000, 1000);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;
  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 1) << "equal is the floor, not a violation";
}

// -1 is "same as the order latency", the default.
TEST_F(OBDelayQueueTest, NegativeCancelLatencyMeansSameAsTheOrder) {
  ob->set_delay(1000, -1);
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1);

  const uint64_t cancelled_at = sent + kDelayNs + 100;
  cancel_ours(kOurPx, cancelled_at);
  market_add(en::bs::SEL, kAskPx + 6, 1, cancelled_at + kDelayNs / 2);
  EXPECT_EQ(our_size_at(kOurPx), 1) << "half the latency is not enough";
  market_add(en::bs::SEL, kAskPx + 7, 1, cancelled_at + kDelayNs + 1);
  EXPECT_EQ(our_size_at(kOurPx), 0) << "the order latency applies to the cancel";
}


// A message with no ts0 is applied immediately instead of being delayed. The
// senders are the SOM's own unwind cancels and console cancels -- not part of
// the measured experiment, and with no clock there is no latency to model. The
// alternative, inventing a timestamp, either releases the message instantly
// anyway or strands it on the queue forever.
TEST_F(OBDelayQueueTest, AMessageWithNoTimestampIsNotDelayed) {
  seed_book(kT0);
  const uint64_t sent = kT0 + 10;

  place_ours(kOurPx, sent);
  market_add(en::bs::SEL, kAskPx + 5, 1, sent + kDelayNs + 1);
  ASSERT_EQ(our_size_at(kOurPx), 1) << "precondition: our order is resting";

  // Cancel with ts0 == 0: takes effect on this message, no further market
  // data required.
  cancel_ours(kOurPx, 0);
  EXPECT_EQ(our_size_at(kOurPx), 0)
      << "an untimed cancel must be applied at once, not queued";
}

TEST_F(OBDelayQueueTest, AnUntimedOrderIsNotDelayedEither) {
  seed_book(kT0);
  place_ours(kOurPx, 0);
  EXPECT_EQ(our_size_at(kOurPx), 1)
      << "no timestamp means no modelled latency, in either direction";
}

}  // namespace
