/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * SlippageProbe — the actor that gives the simulator something to measure.
 *
 * The cadence is the thing worth pinning: a fire at 09:30 ET and every 30
 * minutes to 15:00, twelve in a session. A probe that fires eleven times, or
 * twice in one window, silently changes what every downstream number means.
 *
 * It is driven here exactly as the sim drives it -- a periodic Alarm from the
 * Timer for time, EndOfBurst from the book for the touch, Fills from the SOM --
 * with mocks on all three, so a full session's worth of schedule is exercised
 * in microseconds rather than the twenty minutes a replay takes.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Fill.hpp"
#include "light/msg/Set.hpp"
#include "sim/act/SlippageProbe.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

constexpr uint64_t kSec  = 1000000000ull;
constexpr uint64_t kT0   = 1736951400ull * kSec;   // 2025-01-15 09:30 ET
constexpr uint64_t kStep = 1800ull * kSec;         // 30 min
constexpr int kParent = 3;
constexpr int kBid = 23990;
constexpr int kAsk = 23994;

class SlippageProbeTest : public ::testing::Test {
protected:
  MockOB    mock_ob{"MockOB"};
  MockTimer mock_timer;
  MockLight mock_buy{"MockBuy"};
  MockLight mock_sel{"MockSel"};

  std::unique_ptr<sim::SlippageProbe> probe;
  int sym = 0;
  uint64_t next_ex_order_id = 5000;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  // 12 fires, 09:30 -> 15:00 ET, the real schedule.
  static std::vector<uint64_t> full_schedule() {
    std::vector<uint64_t> v;
    for (int i = 0; i < 12; i++) v.push_back(kT0 + uint64_t(i) * kStep);
    return v;
  }

  void build(std::vector<uint64_t> fires, int parent = kParent) {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr);
    sym = asset->id;

    sim::SlippageProbe::Config cfg;
    cfg.sym_name  = "ESH6";
    cfg.sym       = sym;
    cfg.parent_sz = parent;
    cfg.fire_ts   = std::move(fires);
    probe = std::make_unique<sim::SlippageProbe>(&mock_ob, &mock_timer, cfg);

    TestHelper::invoke_handler(probe.get(), new actors::msg::Start(), nullptr);
    probe->set_lights(&mock_buy, &mock_sel);
    mock_buy.clear();
    mock_sel.clear();
  }

  // --- stimulus, shaped the way the real senders shape it ---------------

  void tick(uint64_t now) {
    frame::mtim::msg::Alarm a(frame::mtim::msg::Alarm::ALARMCLOCK);
    a.timer_id = 700;                                   // PROBE_TICK
    a.currtim  = chutil::Time::from_epoch(now);
    TestHelper::invoke_handler(probe.get(), &a, &mock_timer);
  }

  void quote(int bid, int ask) {
    auto payload = boost::intrusive_ptr<mda::msg::data_pay_load>(
        new mda::msg::data_pay_load());
    payload->mkt  = en::x::CMEMD;
    payload->sym  = sym;
    payload->px   = ref::Price(bid, sym);
    payload->sz   = 1;
    payload->side = en::bs::BUY;
    payload->mev  = en::md::ADD;
    payload->ex_order_id = next_ex_order_id++;
    payload->point_.sym = sym;
    payload->point_.bid_px[0] = bid;
    payload->point_.ask_px[0] = ask;
    payload->point_.bid_sz[0] = 10;
    payload->point_.ask_sz[0] = 10;

    auto eob = new ob::msg::EndOfBurst(payload);
    eob->last = true;
    TestHelper::invoke_handler(probe.get(), eob, &mock_ob);
    delete eob;
  }

  void fill(en::bs side, int px, int sz) {
    som::msg::Fill f(1u, en::x::SIM, uint(sym), px, side, double(sz), 0.0,
                     en::trader::SIMULATOR, uint64_t(0));
    TestHelper::invoke_handler(probe.get(), &f, nullptr);
  }

  // --- observation ------------------------------------------------------

  // Every TARGET_POS the probe pushed at the BUY light, in order. The whole
  // protocol is visible here: a non-zero value opens a fire, a zero closes a
  // leg, so this sequence IS the probe's behaviour.
  std::vector<int> targets(const MockLight &l) {
    std::vector<int> v;
    for (size_t i = 0; i < l.message_count(); i++)
      if (auto s = l.get_message<light::msg::Set>(i))
        if (s->key == light::msg::Set::TARGET_POS)
          v.push_back(int(s->dval));
    return v;
  }

  int count_fires(const MockLight &l) {
    int n = 0;
    for (int t : targets(l)) if (t != 0) ++n;
    return n;
  }

  // One complete round trip at `now`: fire, fill the buy, fill the sell.
  void run_round_trip(uint64_t now) {
    quote(kBid, kAsk);
    tick(now);                       // opens the fire
    fill(en::bs::BUY, kAsk, kParent);
    tick(now + kSec);                // notices the buy leg is done
    fill(en::bs::SEL, kBid, kParent);
    tick(now + 2 * kSec);            // notices we are flat, closes the fire
  }
};

TEST_F(SlippageProbeTest, ArmsAPeriodicTimerRatherThanAWallClockAlarm) {
  build(full_schedule());

  auto sub = mock_timer.get_message<frame::mtim::msg::AlarmClockSub>(0);
  ASSERT_NE(sub, nullptr) << "the probe must take its clock from the Timer";
  EXPECT_EQ(sub->timer_id, 700);
  EXPECT_TRUE(sub->periodic)
      << "a one-shot would fire once and the probe would stop";
}

TEST_F(SlippageProbeTest, DoesNotFireBeforeTheFirstScheduledTime) {
  build(full_schedule());
  quote(kBid, kAsk);

  tick(kT0 - 3600 * kSec);
  tick(kT0 - 60 * kSec);
  tick(kT0 - 1);

  EXPECT_EQ(count_fires(mock_buy), 0);
}

TEST_F(SlippageProbeTest, FiresAtTheFirstScheduledTime) {
  build(full_schedule());
  quote(kBid, kAsk);
  tick(kT0);

  EXPECT_EQ(targets(mock_buy), std::vector<int>{kParent});
  EXPECT_EQ(targets(mock_sel), std::vector<int>{kParent})
      << "both lights get the target; it is what selects between them";
}

// The cadence. This is the property the whole experiment rests on: twelve
// fires, one per half hour, 09:30 to 15:00 inclusive -- and none in between.
TEST_F(SlippageProbeTest, FiresTwelveTimesEveryThirtyMinutes) {
  build(full_schedule());

  for (int slot = 0; slot < 12; slot++) {
    const uint64_t at = kT0 + uint64_t(slot) * kStep;

    // Midway through the previous gap: nothing should fire here.
    if (slot > 0) {
      const int before = count_fires(mock_buy);
      quote(kBid, kAsk);
      tick(at - kStep / 2);
      EXPECT_EQ(count_fires(mock_buy), before)
          << "fired between slots, at slot " << slot;
    }

    run_round_trip(at);
    EXPECT_EQ(count_fires(mock_buy), slot + 1)
        << "slot " << slot << " did not produce exactly one fire";
  }

  // Past the end of the schedule: no thirteenth.
  for (int i = 1; i <= 4; i++) {
    quote(kBid, kAsk);
    tick(kT0 + 12 * kStep + uint64_t(i) * kStep);
  }
  EXPECT_EQ(count_fires(mock_buy), 12)
      << "kept firing after the schedule was exhausted";
}

TEST_F(SlippageProbeTest, FiresExactlyOncePerScheduledSlot) {
  build(full_schedule());

  // Ticks far more often than the schedule: a slot must still open one fire.
  for (int slot = 0; slot < 3; slot++) {
    const uint64_t at = kT0 + uint64_t(slot) * kStep;
    for (int i = 0; i < 5; i++) { quote(kBid, kAsk); tick(at + uint64_t(i)); }
    fill(en::bs::BUY, kAsk, kParent);
    tick(at + 10);
    fill(en::bs::SEL, kBid, kParent);
    tick(at + 20);
  }

  EXPECT_EQ(count_fires(mock_buy), 3)
      << "repeated ticks inside one slot must not re-fire it";
}

TEST_F(SlippageProbeTest, BuyLegCompletionStartsTheSellLeg) {
  build(full_schedule());
  quote(kBid, kAsk);
  tick(kT0);
  ASSERT_EQ(targets(mock_buy), std::vector<int>{kParent});

  fill(en::bs::BUY, kAsk, kParent);
  tick(kT0 + kSec);

  EXPECT_EQ(targets(mock_buy), (std::vector<int>{kParent, 0}))
      << "reaching the target must flip the pair to selling back to flat";
}

TEST_F(SlippageProbeTest, ARoundTripLeavesTheLightsFlat) {
  build(full_schedule());
  run_round_trip(kT0);

  auto tg = targets(mock_buy);
  ASSERT_FALSE(tg.empty());
  EXPECT_EQ(tg.back(), 0)
      << "a fire that ends holding inventory would corrupt the next one";
}

// A slot that arrives while we are still unwinding the previous fire must be
// skipped, not started: measuring from a non-zero position is meaningless.
TEST_F(SlippageProbeTest, SkipsASlotWhenNotFlat) {
  build(full_schedule());

  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, kAsk, kParent);       // long, and never sold back

  // Walk past the next slot without ever returning to flat.
  for (uint64_t t = kT0 + kSec; t <= kT0 + kStep + 10 * kSec; t += 60 * kSec) {
    quote(kBid, kAsk);
    tick(t);
  }

  EXPECT_EQ(count_fires(mock_buy), 1)
      << "the second slot must be skipped while still holding position";
}

// No two-sided book means no mid, and a fire with no reference price produces
// a number that cannot be interpreted.
TEST_F(SlippageProbeTest, SkipsASlotWithNoTouch) {
  build(full_schedule());
  tick(kT0);                               // no quote() first
  EXPECT_EQ(count_fires(mock_buy), 0);
}

TEST_F(SlippageProbeTest, DisabledWithoutAParentSize) {
  build(full_schedule(), 0 /*parent*/);
  EXPECT_EQ(mock_timer.message_count(), 0u)
      << "a disabled probe must not even arm the timer";
  quote(kBid, kAsk);
  tick(kT0);
  EXPECT_EQ(count_fires(mock_buy), 0);
}

}  // namespace
