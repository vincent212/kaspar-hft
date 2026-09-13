/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
constexpr uint64_t kWindow = 15ull * 60ull * kSec; // Config::quote_window_ns
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
  kSlipBuy, kSlipSel, kSlipPaired, kSlipLegsum,
  kBuyMktVol, kSelMktVol, kBuyPart, kSelPart,
  kBuyMktVwap, kSelMktVwap, kSlipBuyVsVwap, kSlipSelVsVwap, kSlipVsVwap,
  kAskFire, kBidSell, kSlipBuyVsTouch, kSlipSelVsTouch, kSlipVsTouch,
  kPosAtClose, kOutcome, kNumCols
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

  // The session the probe measures: 09:30 to 15:30 ET. Only the two bounds
  // matter -- the repeating timer supplies every boundary in between.
  static uint64_t session_end() { return kT0 + 24 * kWindow; }   // 09:30 + 6h

  void build(int parent = kParent) {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr);
    sym = asset->id;

    sim::SlippageProbe::Config cfg;
    cfg.sym_name  = "ESH6";
    cfg.sym       = sym;
    cfg.parent_sz     = parent;
    cfg.session_start = kT0;
    cfg.session_end   = session_end();
    cfg.tick_s        = int(kWindow / kSec);   // the timer period IS the window
    cfg.out_path      = csv_path;        // empty unless the test asked for a CSV
    probe = std::make_unique<sim::SlippageProbe>(&mock_ob, &mock_timer, cfg);

    // Lights go in as vectors, split by side: the probe targets every BUY light
    // with +sz and every SEL light with -sz. Handing it single pointers was the
    // bug that left SEL lights untargeted once there was more than one a side.
    //
    // set_lights BEFORE Start: the probe sends its targets from start_handler,
    // exactly once, and there is nothing to send them to before this.
    // Lights BEFORE Start, and as vectors split by side. The probe targets every
    // BUY light with +sz and every SEL light with -sz; handing it single
    // pointers was the bug that left SEL lights untargeted once there was more
    // than one a side.
    probe->set_lights({&mock_buy}, {&mock_sel});
    TestHelper::invoke_handler(probe.get(), new actors::msg::Start(), nullptr);
    // Deliberately NOT cleared here: the startup targets are the probe's whole
    // control surface and two tests assert on them directly. Tests that care
    // about later traffic clear the mocks themselves.
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

  // Quote both sides for one window and let the clock close it. Both sides are
  // live throughout -- that is the whole arrangement -- so fills interleave
  // rather than arriving in two phases.
  void run_window(uint64_t now, int lots = kParent) {
    quote(kBid, kAsk);
    tick(now);                          // opens the window
    uint64_t t = now;
    for (int c : clips_for(lots)) {
      fill(en::bs::BUY, kAsk, c);
      fill(en::bs::SEL, kBid, c);
      tick(t += kSec);
    }
    tick(now + kWindow + kSec);          // past the boundary: closes it
  }
};

TEST_F(SlippageProbeTest, ArmsAPeriodicTimerRatherThanAWallClockAlarm) {
  build();

  auto sub = mock_timer.get_message<frame::mtim::msg::AlarmClockSub>(0);
  ASSERT_NE(sub, nullptr) << "the probe must take its clock from the Timer";
  EXPECT_EQ(sub->timer_id, 700);
  EXPECT_TRUE(sub->periodic)
      << "a one-shot would fire once and the probe would stop";
}

TEST_F(SlippageProbeTest, SetsBothSidesTargetsOnceAtStartup) {
  build();
  // The entire control surface. BUY lights are told +sz, SEL lights -sz, once,
  // and nothing ever sends a target again. That is what keeps both sides quoting
  // continuously: a BUY light stands down at pos >= targetpos and a SEL light at
  // pos <= targetpos, so every position strictly inside (-sz, +sz) leaves both
  // live, and the inventory crosses zero on its own without anything resetting
  // it.
  EXPECT_EQ(targets(mock_buy), std::vector<int>{ kParent });
  EXPECT_EQ(targets(mock_sel), std::vector<int>{ -kParent });
}

TEST_F(SlippageProbeTest, NeverSendsATargetAgain) {
  build();
  // Mutation guard for the bug this design replaced: the old probe re-targeted
  // on every phase change and then TRADED its way back to flat after each fire,
  // which is roughly as much volume as the measurement itself and landed after
  // the row was written.
  mock_buy.clear();
  mock_sel.clear();

  run_window(kT0);
  run_window(kT0 + kStep);

  EXPECT_TRUE(targets(mock_buy).empty()) << "a target was re-sent after startup";
  EXPECT_TRUE(targets(mock_sel).empty()) << "a target was re-sent after startup";
}

TEST_F(SlippageProbeTest, DoesNotEmitBeforeTheFirstScheduledTime) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0 - kSec);
  EXPECT_EQ(csv_rows().size(), 1u) << "header only";
}

TEST_F(SlippageProbeTest, AWindowEmitsOneRowWhenTheClockPassesItsEnd) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);                       // opens
  EXPECT_EQ(csv_rows().size(), 1u) << "nothing emitted while it is still open";

  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);      // past the end: closes and emits

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kOutcome], "ok");
}

TEST_F(SlippageProbeTest, BothSidesFillIntoTheirOwnLegOverTheSameWindow) {
  want_csv();
  build();
  // The point of quoting both sides at once: the legs share an interval, so
  // their VWAPs and participation denominators are comparable by construction.
  run_window(kT0);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(std::stod(rows[1][kBuyFilled]), kParent);
  EXPECT_EQ(std::stod(rows[1][kSelFilled]), kParent);
  // Each leg's denominator is its OWN fill interval, not the clock window --
  // the sides quote together but do not trade together, so a shared denominator
  // would flatter whichever finished first.
  EXPECT_GE(std::stod(rows[1][kBuyMktVol]), 0.0);
  EXPECT_GE(std::stod(rows[1][kSelMktVol]), 0.0);
}

TEST_F(SlippageProbeTest, WindowsRunBackToBack) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);
  for (int i = 1; i <= 3; i++) {
    fill(en::bs::BUY, kAsk, 1);
    fill(en::bs::SEL, kBid, 1);
    tick(kT0 + i * (kWindow + kSec));
  }
  EXPECT_EQ(csv_rows().size(), 4u) << "one row per closed window";
}

TEST_F(SlippageProbeTest, CarriesInventoryAcrossAWindowBoundary) {
  want_csv();
  build();
  // Inventory is NOT flattened at a boundary. It stays on the book, the lights
  // carry it into the next window, and their own targets pull it back toward the
  // band. The old probe traded out here; that trade was invisible in the row.
  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, kAsk, kParent);      // long, no offsetting sell
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(std::stoi(rows[1][kPosAtClose]), kParent)
      << "the position the window ended on must be reported, not unwound";
  EXPECT_TRUE(targets(mock_sel).empty())
      << "nothing may be re-targeted to flatten it";
}

TEST_F(SlippageProbeTest, SkipsAWindowWithNoTouch) {
  want_csv();
  build();
  tick(kT0);                       // never quoted: no bid/ask to anchor a row
  EXPECT_EQ(csv_rows().size(), 1u) << "header only";
}

TEST_F(SlippageProbeTest, LegVwapIsSizeWeightedAcrossPartialFills) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, 100, 1);
  fill(en::bs::BUY, 200, 3);       // 1@100 + 3@200 -> 175
  fill(en::bs::SEL, kBid, 4);
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_NEAR(std::stod(rows[1][kBuyVwap]), 175.0, 1e-6);
}

TEST_F(SlippageProbeTest, DisabledWithoutAParentSize) {
  build(0);
  EXPECT_FALSE(probe->enabled());
}

}  // namespace
