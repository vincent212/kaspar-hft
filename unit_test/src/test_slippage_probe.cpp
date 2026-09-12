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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
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

// Columns of the emitted CSV, in the order emit_header() writes them. The CSV
// is the probe's real product -- a caller reads these numbers, never the leg
// state -- so the arithmetic is asserted where it actually lands.
// Mirrors SlippageProbe::emit_header(). Kept exact on purpose: the CSV is the
// experiment's output, and a column silently moving would corrupt every
// downstream number without failing anything.
enum Col {
  kFireTs = 0, kSym, kParentSz, kMidFire, kBuyVwap, kBuyFilled, kBuyFills,
  kBuyNs, kMidSell, kSelVwap, kSelFilled, kSelFills, kSelNs,
  kSlipBuy, kSlipSel, kSlipPaired,
  kBuyMktVol, kSelMktVol, kBuyPart, kSelPart, kOutcome, kNumCols
};

class SlippageProbeTest : public ::testing::Test {
protected:
  MockOB    mock_ob{"MockOB"};
  MockTimer mock_timer;
  MockLight mock_buy{"MockBuy"};
  MockLight mock_sel{"MockSel"};

  std::unique_ptr<sim::SlippageProbe> probe;
  int sym = 0;
  uint64_t next_ex_order_id = 5000;
  std::string csv_path;            // set before build() to capture the CSV

  void TearDown() override {
    probe.reset();                 // closes the CSV
    if (!csv_path.empty()) std::remove(csv_path.c_str());
  }

  // A CSV path unique to the running test, so a parameterised suite does not
  // have its three instances writing over one another. Parameterised test names
  // contain '/', so the name part is flattened into a legal filename.
  void want_csv() {
    const auto *ti = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string leaf = std::string(ti->test_suite_name()) + "_" + ti->name();
    for (char &c : leaf) if (c == '/') c = '_';
    csv_path = ::testing::TempDir() + "slip_probe_" + leaf + ".csv";
    std::remove(csv_path.c_str());
  }

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
    cfg.out_path  = csv_path;        // empty unless the test asked for a CSV
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

  // Rows of the emitted CSV, header included as row 0. The probe fflushes after
  // every row, so this is readable while the actor is still alive.
  std::vector<std::vector<std::string>> csv_rows() {
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(csv_path);
    std::string line;
    while (std::getline(in, line)) {
      std::vector<std::string> cells;
      std::stringstream ss(line);
      std::string cell;
      while (std::getline(ss, cell, ',')) cells.push_back(cell);
      rows.push_back(std::move(cells));
    }
    return rows;
  }

  // How a parent of this size actually arrives: as several child clips, never
  // as one fill. A parent of 1 is the degenerate case and is what every other
  // test in this file used to assume.
  static std::vector<int> clips_for(int parent) {
    if (parent <= 1) return std::vector<int>{parent};
    if (parent < 10) return std::vector<int>{parent - 1, 1};
    if (parent == 10) return std::vector<int>{3, 3, 4};   // uneven on purpose
    std::vector<int> v(9, parent / 10);                   // 100 -> ten of 10
    v.push_back(parent - 9 * (parent / 10));
    return v;
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

  // The same, but with both legs arriving as several clips -- the shape any
  // parent bigger than one child order actually has.
  void run_clipped_round_trip(uint64_t now, int parent) {
    quote(kBid, kAsk);
    tick(now);
    uint64_t t = now;
    for (int c : clips_for(parent)) { fill(en::bs::BUY, kAsk, c); tick(t += kSec); }
    for (int c : clips_for(parent)) { fill(en::bs::SEL, kBid, c); tick(t += kSec); }
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

// ---------------------------------------------------------------------------
// Parent sizes other than one.
//
// Every test above fills a leg with a single Fill, which makes "a fill arrived"
// and "the leg is complete" indistinguishable. That is only true for a parent
// small enough to be one child order. At 10 or 100 the parent comes back as a
// stream of partials, and the probe has to add them up: declare the buy leg
// done on the first clip and the fire measures a fraction of the order it
// claims to have measured, while the sell leg unwinds a position that is not
// there. These run the whole state machine at 1, 10 and 100.
// ---------------------------------------------------------------------------

class SlippageProbeSizeTest : public SlippageProbeTest,
                              public ::testing::WithParamInterface<int> {};

INSTANTIATE_TEST_SUITE_P(ParentSizes, SlippageProbeSizeTest,
                         ::testing::Values(1, 10, 100),
                         [](const ::testing::TestParamInfo<int> &i) {
                           return "sz" + std::to_string(i.param);
                         });

// The core partial-fill property: the buy leg is complete when the clips SUM to
// the parent, not when the first one lands. A probe that flipped on the first
// fill would sell back one clip and report the slippage of a 3-lot as the
// slippage of a 100-lot.
TEST_P(SlippageProbeSizeTest, BuyLegCompletesOnlyWhenTheClipsSumToTheParent) {
  const int parent = GetParam();
  build(full_schedule(), parent);
  quote(kBid, kAsk);
  tick(kT0);
  ASSERT_EQ(targets(mock_buy), std::vector<int>{parent});

  const auto clips = clips_for(parent);
  int so_far = 0;
  for (size_t i = 0; i + 1 < clips.size(); i++) {
    fill(en::bs::BUY, kAsk, clips[i]);
    so_far += clips[i];
    tick(kT0 + uint64_t(i + 1) * kSec);
    EXPECT_EQ(targets(mock_buy), std::vector<int>{parent})
        << "buy leg ended at " << so_far << "/" << parent
        << ": a partial fill must not flip the pair to selling";
  }

  fill(en::bs::BUY, kAsk, clips.back());
  tick(kT0 + uint64_t(clips.size()) * kSec);
  EXPECT_EQ(targets(mock_buy), (std::vector<int>{parent, 0}))
      << "the last clip completes the parent and must start the sell leg";
}

// The mirror property, and the one the outcome column depends on: the fire is
// only over when the whole parent is back off the books. Ending early would
// leave inventory that makes the next fire unmeasurable -- and it would still
// be reported "ok".
TEST_P(SlippageProbeSizeTest, SellLegFinishesOnlyWhenTheWholeParentIsSoldBack) {
  const int parent = GetParam();
  want_csv();
  build(full_schedule(), parent);

  quote(kBid, kAsk);
  tick(kT0);
  uint64_t t = kT0;
  for (int c : clips_for(parent)) { fill(en::bs::BUY, kAsk, c); tick(t += kSec); }
  ASSERT_EQ(targets(mock_buy), (std::vector<int>{parent, 0}));

  const auto clips = clips_for(parent);
  int sold = 0;
  for (size_t i = 0; i + 1 < clips.size(); i++) {
    fill(en::bs::SEL, kBid, clips[i]);
    sold += clips[i];
    tick(t += kSec);
    EXPECT_EQ(csv_rows().size(), 1u)
        << "fire closed with " << (parent - sold) << " still held";
  }

  fill(en::bs::SEL, kBid, clips.back());
  tick(t += kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u) << "a completed round trip must emit exactly one row";
  ASSERT_EQ(rows[1].size(), size_t(kNumCols));
  EXPECT_EQ(rows[1][kOutcome], "ok");
  EXPECT_EQ(rows[1][kParentSz], std::to_string(parent));
  EXPECT_EQ(rows[1][kBuyFilled], std::to_string(parent));
  EXPECT_EQ(rows[1][kSelFilled], std::to_string(parent));
  EXPECT_EQ(rows[1][kBuyFills], std::to_string(clips.size()))
      << "every clip must be counted, not just the one that completed the leg";
}

// The cadence test again, but with the legs clipped. Twelve fires is the
// property the whole experiment rests on, and it must not quietly depend on
// each leg being one fill: a leg that never completes stalls the schedule and
// every later slot is skipped for "not flat".
TEST_P(SlippageProbeSizeTest, FiresTwelveTimesWhateverTheParentSize) {
  const int parent = GetParam();
  build(full_schedule(), parent);

  for (int slot = 0; slot < 12; slot++) {
    run_clipped_round_trip(kT0 + uint64_t(slot) * kStep, parent);
    EXPECT_EQ(count_fires(mock_buy), slot + 1)
        << "slot " << slot << " at parent_sz=" << parent;
  }

  for (int i = 1; i <= 4; i++) {
    quote(kBid, kAsk);
    tick(kT0 + 12 * kStep + uint64_t(i) * kStep);
  }
  EXPECT_EQ(count_fires(mock_buy), 12);
}

// With partials at different prices the leg price is a weighted mean, not the
// mean of the fill prices. 6 @ 100 and 4 @ 105 is 102.0; averaging the two
// prices gives 102.5, half a tick of slippage invented out of nothing. The two
// only coincide when every clip is the same size, which is exactly what a
// single-fill test cannot distinguish.
TEST_F(SlippageProbeTest, LegVwapIsSizeWeightedAcrossPartialFills) {
  want_csv();
  build(full_schedule(), 10 /*parent*/);

  quote(100, 104);                       // mid 102
  tick(kT0);
  fill(en::bs::BUY, 100, 6);
  tick(kT0 + kSec);
  fill(en::bs::BUY, 105, 4);             // -> 1020/10 = 102.0
  tick(kT0 + 2 * kSec);
  fill(en::bs::SEL, 110, 5);
  fill(en::bs::SEL, 100, 5);             // -> 1050/10 = 105.0
  tick(kT0 + 3 * kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_NEAR(std::stod(rows[1][kMidFire]), 102.0, 1e-6);
  EXPECT_NEAR(std::stod(rows[1][kBuyVwap]), 102.0, 1e-6)
      << "un-weighted, the two buy prices average to 102.5";
  EXPECT_NEAR(std::stod(rows[1][kSelVwap]), 105.0, 1e-6);

  // And the slippage columns derived from them.
  EXPECT_NEAR(std::stod(rows[1][kSlipBuy]), 0.0, 1e-6);      // 102.0 - 102
  EXPECT_NEAR(std::stod(rows[1][kSlipSel]), -3.0, 1e-6);     // 102 - 105.0
  EXPECT_NEAR(std::stod(rows[1][kSlipPaired]), -1.5, 1e-6);  // (102.0-105.0)/2
}

// A clip can come back larger than the residual -- several child orders crossing
// in the same match event are reported as they fill, not capped at what the
// parent still wanted. The leg must end (it is past the target, not short of
// it) and the unwind must clear the position that actually exists rather than
// parent_sz, or the probe is left permanently long and skips every later slot.
TEST_F(SlippageProbeTest, AnOverFillCompletesAndLeavesTheProbeUsable) {
  want_csv();
  build(full_schedule(), 10 /*parent*/);

  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, kAsk, 6);
  tick(kT0 + kSec);
  fill(en::bs::BUY, kAsk, 6);            // 12 against a parent of 10
  tick(kT0 + 2 * kSec);
  ASSERT_EQ(targets(mock_buy), (std::vector<int>{10, 0}))
      << "overshooting the target still completes the buy leg";

  fill(en::bs::SEL, kBid, 10);           // parent_sz back, but 2 still held
  tick(kT0 + 3 * kSec);
  EXPECT_EQ(csv_rows().size(), 1u)
      << "selling parent_sz is not flat when the buy leg over-filled";

  fill(en::bs::SEL, kBid, 2);
  tick(kT0 + 4 * kSec);
  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kBuyFilled], "12");
  EXPECT_EQ(rows[1][kSelFilled], "12");
  EXPECT_EQ(rows[1][kOutcome], "ok");

  // The next slot must still fire: an over-fill that left a phantom position
  // behind would silently cost every remaining fire in the session.
  run_clipped_round_trip(kT0 + kStep, 10);
  EXPECT_EQ(count_fires(mock_buy), 2);
}

// A parent of one either fills or does not. A parent of ten can time out
// half-done, and then the unwind has to sell the four contracts that exist --
// not the ten that were asked for, which would go short. The row must say
// buy_short so the number is not read as a clean measurement, and the session
// must carry on.
TEST_F(SlippageProbeTest, ABuyLegThatTimesOutPartiallyFilledUnwindsOnlyWhatItGot) {
  want_csv();
  build(full_schedule(), 10 /*parent*/);

  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, kAsk, 4);            // and then the book goes quiet
  tick(kT0 + 601 * kSec);                // past the 10-minute leg timeout
  ASSERT_EQ(targets(mock_buy), (std::vector<int>{10, 0}))
      << "a timed-out buy leg must still be unwound";

  fill(en::bs::SEL, kBid, 4);
  tick(kT0 + 602 * kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kBuyFilled], "4");
  EXPECT_EQ(rows[1][kSelFilled], "4")
      << "unwinding parent_sz instead of the position held would go short";
  EXPECT_EQ(rows[1][kOutcome], "buy_short");

  run_clipped_round_trip(kT0 + kStep, 10);
  EXPECT_EQ(count_fires(mock_buy), 2) << "a short fire must not stall the schedule";
}

// Only a multi-clip parent can do this: when the target flips to zero the buy
// light may still have children resting at the exchange, and one of them can
// print after the sell leg has started. The unwind is sized off the position,
// not off parent_sz, precisely so that late contract gets sold too -- ending
// the fire at ten sold would leave five long and poison the next fire.
TEST_F(SlippageProbeTest, ABuyFillArrivingDuringTheSellLegIsStillUnwound) {
  want_csv();
  build(full_schedule(), 10 /*parent*/);

  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, kAsk, 10);
  tick(kT0 + kSec);                      // buy leg done, sell leg starts
  fill(en::bs::BUY, kAsk, 5);            // a stale child prints late

  fill(en::bs::SEL, kBid, 10);
  tick(kT0 + 2 * kSec);
  EXPECT_EQ(csv_rows().size(), 1u)
      << "ten sold against fifteen bought is not flat";

  fill(en::bs::SEL, kBid, 5);
  tick(kT0 + 3 * kSec);
  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kBuyFilled], "15");
  EXPECT_EQ(rows[1][kSelFilled], "15");
}

}  // namespace
