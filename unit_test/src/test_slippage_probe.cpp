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

#include <filesystem>
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
#include "unit_test/MockPCoord.hpp"
#include "unit_test/FakeMarketData.hpp"
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
  kBuyNs, kSelVwap, kSelFilled, kSelFills, kSelNs,
  kSlipBuy, kSlipSel, kSlipPaired,
  kBuyDrift, kSelDrift, kDrift,
  kBuyMktVol, kSelMktVol, kBuyPart, kSelPart,
  kBuyMktVwap, kSelMktVwap, kSlipBuyVsVwap, kSlipSelVsVwap, kSlipVsVwap,
  kBuyHitVol, kSelTakVol, kBuyHitVwap, kSelTakVwap,
  kSlipBuyVsHit, kSlipSelVsTak, kSlipVsAgg,
  kAskFire, kBidFire, kSlipBuyVsTouch, kSlipSelVsTouch, kSlipVsTouch,
  kPosAtClose, kPosMax, kPosMin, kPosMean, kPosAbsMean, kOutcome, kNumCols
};

class SlippageProbeTest : public ::testing::Test {
protected:
  MockOB    mock_ob{"MockOB"};
  MockTimer mock_timer;
  MockLight mock_buy{"MockBuy"};
  MockLight mock_sel{"MockSel"};

  // The two position books. These ARE the control surface: the probe sends no
  // targets, it hands each side work by moving its book away from flat, and the
  // lights (here, the test) work it back. One book per side, because with
  // targetpos 0 no single position leaves both sides live.
  unit_test::MockPCoord book_buy;
  unit_test::MockPCoord book_sel;

  std::unique_ptr<sim::SlippageProbe> probe;
  int sym = 0;
  uint64_t next_ex_order_id = 5000;
  std::string csv_path;            // set before build() to capture the CSV

  void TearDown() override {
    probe.reset();                 // closes the CSV
    if (!csv_path.empty()) std::remove(csv_path.c_str());
  }

  // Scratch directory for the CSVs these tests write.
  //
  // NOT ::testing::TempDir(). On generic Linux gtest returns the bare literal
  // "/tmp/" -- it special-cases only Windows, Windows Mobile and Android, and
  // consults no environment variable -- and / is the small local RAID1 that a
  // sweep's logs once filled to 100%, surfacing as a link error that looked
  // nothing like a full disk. TearDown() unlinks the file, so a clean run
  // leaves nothing behind; an ABORTED one does not, and several tests here
  // deliberately drive ASSERT paths. $HOME is on the big filer.
  static std::string scratch_dir() {
    if (const char *t = std::getenv("TEST_TMPDIR"); t && *t) {
      std::string d(t);
      if (d.back() != '/') d += '/';
      return d;
    }
    if (const char *h = std::getenv("HOME"); h && *h) {
      std::string d = std::string(h) + "/tmp/";
      std::filesystem::create_directories(d);
      return d;
    }
    return ::testing::TempDir();
  }

  // A CSV path unique to the running test, so a parameterised suite does not
  // have its three instances writing over one another. Parameterised test names
  // contain '/', so the name part is flattened into a legal filename.
  void want_csv() {
    const auto *ti = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string leaf = std::string(ti->test_suite_name()) + "_" + ti->name();
    for (char &c : leaf) if (c == '/') c = '_';
    csv_path = scratch_dir() + "slip_probe_" + leaf + ".csv";
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
    // The timer POLLS; it is not the window. A window closes only when its
    // minimum has elapsed AND both legs have filled, so the probe must look at
    // the clock far more often than the minimum.
    cfg.tick_s        = 1;
    cfg.min_window_ns = kWindow;
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
    probe->set_coords(&book_buy, &book_sel);
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

  void fill(en::bs side, int px, int sz) { fill_at(0, side, px, sz); }

  // A market trade. resting_side is the side of the order ALREADY IN THE BOOK,
  // which is what the feed reports and what is_hit()/is_tak() key on:
  //   resting BUY  -> a bid was hit    -> the aggressor SOLD
  //   resting SEL  -> an offer was taken -> the aggressor BOUGHT
  void trade(en::bs resting_side, int px, int sz) {
    auto payload = unit_test::make_trade_payload(en::x::CMEMD, sym, px, sz,
                                                 resting_side, kBid, kAsk);
    // make_payload deliberately leaves px unset -- it would need RefData, which
    // a bare fixture may not have registered. This one does (quote() sets a
    // Price the same way), and without it every trade contributes sz * 0
    // notional and any VWAP built from it is 0.
    payload->px = ref::Price(px, sym);
    auto tn = new ob::msg::TradeNotify(payload);
    TestHelper::invoke_handler(probe.get(), tn, &mock_ob);
    delete tn;
  }

  // A fill carrying its own market timestamp. The leg takes its end stamp from
  // the FILL, not from the tick that later notices, so a test about leg
  // durations has to be able to say when the fill happened.
  void fill_at(uint64_t tim, en::bs side, int px, int sz) {
    // The LIGHT moves its own book on a fill (light22_base.hpp:771), and the
    // probe is a separate subscriber to the same publish. Mirror that here, or
    // the books never come back to flat and the test bears no resemblance to
    // what runs. The buy book is worked UP toward 0 by buying; the sell book
    // DOWN toward 0 by selling.
    // Through the BASE method deliberately, so the mock does not record it.
    // MockPCoord::calls is then purely the work the PROBE handed out, which is
    // what the work() helper reports and what the tests assert on; the lights
    // working that book off is a different thing and would otherwise interleave
    // with it.
    if (side == en::bs::BUY) book_buy.light::PCoord::add_position(en::bs::BUY, sz);
    else                     book_sel.light::PCoord::add_position(en::bs::SEL, sz);

    som::msg::Fill f(1u, en::x::SIM, uint(sym), px, side, double(sz), 0.0,
                     en::trader::SIMULATOR, tim);
    TestHelper::invoke_handler(probe.get(), &f, nullptr);
  }

  // --- observation ------------------------------------------------------

  // Every TARGET_POS the probe pushed at a light, in order. There should be
  // exactly one per light for a whole session: +sz for a BUY light, -sz for a
  // SEL light, sent once and never revised.
  // Every TARGET_POS a light was sent. Must always be empty: targetpos stays 0
  // and the probe drives the POSITION instead, exactly as PositionManager does.
  std::vector<int> targets(const MockLight &l) {
    std::vector<int> v;
    for (size_t i = 0; i < l.message_count(); i++)
      if (auto s = l.get_message<light::msg::Set>(i))
        if (s->key == light::msg::Set::TARGET_POS)
          v.push_back(int(s->dval));
    return v;
  }

  // The sizes handed to a book, one entry per window opened.
  std::vector<int> work(const unit_test::MockPCoord &b) {
    std::vector<int> v;
    for (const auto &c : b.calls) v.push_back(c.side == en::bs::BUY ? c.sz : -c.sz);
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

  // One complete window: both sides fill their size, then the clock passes the
  // minimum so it can close.
  void run_window(uint64_t now, int lots = kParent) {
    quote(kBid, kAsk);
    tick(now);
    uint64_t t = now;
    for (int c : clips_for(lots)) {
      fill(en::bs::BUY, kAsk, c);
      fill(en::bs::SEL, kBid, c);
      tick(t += kSec);
    }
    tick(now + kWindow + kSec);
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

TEST_F(SlippageProbeTest, GivesBothSidesWorkAtTheFirstBoundary) {
  build();
  // Nothing at Start: the probe waits for a boundary inside the session with a
  // usable book, because work handed out before there is anything to measure
  // would have the lights trading outside the window.
  EXPECT_TRUE(work(book_buy).empty()) << "gave work at Start";
  EXPECT_TRUE(work(book_sel).empty()) << "gave work at Start";

  quote(kBid, kAsk);
  tick(kT0);

  // The entire control surface. targetpos stays 0 on every light, so a BUY
  // light works only while its book is SHORT and a SEL light only while its
  // book is LONG (light22.hpp:239,243). Handing a side a parent therefore means
  // moving its book to the opposite position and letting the lights work it
  // back to flat -- what PositionManager does (PositionManager.hpp:56).
  EXPECT_EQ(work(book_buy), std::vector<int>{ -kParent }) << "buy book must go short";
  EXPECT_EQ(work(book_sel), std::vector<int>{  kParent }) << "sel book must go long";
  EXPECT_EQ(book_buy.get_position(), -kParent);
  EXPECT_EQ(book_sel.get_position(),  kParent);
}

TEST_F(SlippageProbeTest, NeverSendsATargetAtAll) {
  // Mutation guard for the arrangement this replaced. TARGET_POS is never sent,
  // not once, not at the end of the session. It is not an off switch either:
  // with targetpos 0 and a long position a SEL light is ACTIVE, so "standing
  // the lights down" by targeting flat is just the liquidation this design
  // removed, arriving through the back door.
  build();
  quote(kBid, kAsk);
  tick(kT0);
  run_window(kT0 + kSec);
  tick(session_end() + kWindow);

  EXPECT_TRUE(targets(mock_buy).empty()) << "a target was sent";
  EXPECT_TRUE(targets(mock_sel).empty()) << "a target was sent";
}

TEST_F(SlippageProbeTest, GivesExactlyOneParentPerWindowPerSide) {
  // The work handed out grows by one parent per window and is never taken back
  // or topped up. A side that overshot simply has less to do next window --
  // that self-corrects and must not be "fixed" by adjusting the book.
  build();
  quote(kBid, kAsk);
  tick(kT0);

  uint64_t t = kT0;
  for (int i = 0; i < 3; i++) {
    fill(en::bs::BUY, kAsk, kParent);
    fill(en::bs::SEL, kBid, kParent);
    t += kWindow + kSec;
    tick(t);
  }

  const std::vector<int> expect_buy(4, -kParent);
  const std::vector<int> expect_sel(4,  kParent);
  EXPECT_EQ(work(book_buy), expect_buy) << "one parent per window, no more";
  EXPECT_EQ(work(book_sel), expect_sel) << "one parent per window, no more";
}

TEST_F(SlippageProbeTest, AnOvershootLeavesLessToDoNextWindow) {
  // The failure this cost a session to find. A side overshoots its work on the
  // last clip -- the lights net against a SHARED position while each throttles
  // against its OWN working size -- so the book ends past flat and the next
  // window holds less than a full parent. Completion is measured against THAT,
  // not against parent_sz: testing against the parent stalled the window
  // forever and lost 22 of 24 windows.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent + 2);   // overshoot the buy side by 2
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);             // window 1 closes, window 2 opens

  // Window 2 must complete on kParent-2 buy lots, because that is all the work
  // the book holds. If it demanded a full parent it would never close.
  fill(en::bs::BUY, kAsk, kParent - 2);
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + 2 * (kWindow + kSec));

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 3u) << "both windows must close";
  EXPECT_EQ(rows[2][kOutcome], "ok");

  // That last tick also OPENED window 3, so each book now holds exactly one
  // fresh parent and nothing carried over: the overshoot was absorbed by
  // window 2 doing less, not by anyone adjusting a book.
  EXPECT_EQ(book_buy.get_position(), -kParent) << "overshoot must not carry past window 2";
  EXPECT_EQ(book_sel.get_position(),  kParent);
}

TEST_F(SlippageProbeTest, DoesNotEmitBeforeTheFirstScheduledTime) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0 - kSec);
  EXPECT_EQ(csv_rows().size(), 1u) << "header only";
}

// --- when a window ends: minimum elapsed AND both legs done ---------------
//
// Four branches, and each gets a test. The rule is a conjunction, so both
// halves have to be shown to bind on their own, or a regression that drops
// either one still passes.

TEST_F(SlippageProbeTest, DoesNotCloseBeforeTheMinimumEvenWithBothLegsDone) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);                                   // opens

  fill(en::bs::BUY, kAsk, kParent);            // both legs finish almost at once
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kSec);
  tick(kT0 + kWindow - kSec);                  // one second short of the minimum

  EXPECT_EQ(csv_rows().size(), 1u)
      << "closed early: the minimum is a floor, not just a deadline";
}

TEST_F(SlippageProbeTest, DoesNotCloseAtTheMinimumWhileALegIsShort) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent);            // buy done
  fill(en::bs::SEL, kBid, kParent - 1);        // sell one short
  tick(kT0 + kWindow + kSec);                  // well past the minimum

  EXPECT_EQ(csv_rows().size(), 1u)
      << "closed on the clock with a short leg: the row would report a partial "
         "execution as a complete one";
}

TEST_F(SlippageProbeTest, ClosesOnceTheMinimumHasPassedAndBothLegsAreDone) {
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kOutcome], "ok");
}

TEST_F(SlippageProbeTest, WaitsPastTheMinimumForASlowLegThenCloses) {
  // The case the fixed-period design got wrong and the assertion turned into a
  // dead run: a thin window simply runs longer.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent - 1);
  tick(kT0 + kWindow + kSec);
  ASSERT_EQ(csv_rows().size(), 1u) << "must still be waiting";

  const uint64_t late = kT0 + kWindow + 180 * kSec;   // three minutes late
  fill(en::bs::SEL, kBid, 1);                         // the leg finally fills
  tick(late);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u) << "should close as soon as the slow leg fills";
  EXPECT_EQ(rows[1][kOutcome], "ok") << "a late window is complete, not partial";
  EXPECT_EQ(std::stod(rows[1][kSelFilled]), kParent);
}

TEST_F(SlippageProbeTest, EachLegIsMeasuredOverItsOwnInterval) {
  // Both legs arrive together, neither ends with the window: each ends at the
  // fill that completed it.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill_at(kT0 + 10 * kSec, en::bs::BUY, kAsk, kParent);   // buy done at 10s
  fill_at(kT0 + 60 * kSec, en::bs::SEL, kBid, kParent);   // sell done at 60s
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  const double buy_ns = std::stod(rows[1][kBuyNs]);
  const double sel_ns = std::stod(rows[1][kSelNs]);
  EXPECT_NEAR(buy_ns, 10.0 * double(kSec), double(kSec) / 2);
  EXPECT_NEAR(sel_ns, 60.0 * double(kSec), double(kSec) / 2);
  EXPECT_LT(buy_ns, sel_ns) << "the legs must not share an end";
  EXPECT_LT(sel_ns, double(kWindow)) << "nor end with the window";
}

TEST_F(SlippageProbeTest, AFinishedLegStopsAccumulating) {
  // Once a leg has its size the window keeps running and the lights keep
  // quoting, but those later fills are not this leg's measurement.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent);
  fill(en::bs::BUY, kAsk, kParent);     // extra flow after both legs are done
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(std::stod(rows[1][kBuyFilled]), kParent)
      << "a finished leg must not keep accumulating";
  EXPECT_EQ(std::stod(rows[1][kSelFilled]), kParent);
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
  tick(kT0);                                   // opens the first

  uint64_t t = kT0;
  for (int i = 1; i <= 3; i++) {
    fill(en::bs::BUY, kAsk, kParent);
    fill(en::bs::SEL, kBid, kParent);
    t += kWindow + kSec;
    tick(t);                                   // closes one and opens the next
  }
  EXPECT_EQ(csv_rows().size(), 4u) << "one row per closed window, no gaps";
}

TEST_F(SlippageProbeTest, CarriesInventoryAcrossAWindowBoundary) {
  // Inventory is NOT flattened at a boundary. It stays on the book, the lights
  // carry it into the next window, and their own targets pull it back toward the
  // band. The old probe traded out here, and that trade never appeared in a row.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);
  mock_buy.clear();                        // drop the startup +sz / -sz
  mock_sel.clear();

  fill(en::bs::BUY, kAsk, kParent);        // both legs complete, position flat
  fill(en::bs::SEL, kBid, kParent);
  fill(en::bs::BUY, kAsk, kParent);        // extra flow after the legs are done
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(std::stoi(rows[1][kPosAtClose]), kParent)
      << "the position the window ended on must be reported, not unwound";
  EXPECT_TRUE(targets(mock_sel).empty())
      << "nothing may be re-targeted to flatten it";
}

TEST_F(SlippageProbeTest, AClipLargerThanTheParentDoesNotAbort) {
  // light22 sizes a clip from ord_sz in lights.ini (5 by default), which the
  // probe never sees and which the grid never overrides. At --probe-size 1 or 2
  // a single legitimate clip therefore exceeds the parent by itself. An
  // overshoot assert here used to bound the FIRST fill at 2*parent_sz and
  // abort() the whole session on a routine 3-lot clip.
  want_csv();
  build(1);
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, 3);            // one ord_sz clip against a 1-lot parent
  fill(en::bs::SEL, kBid, 3);
  tick(kT0 + kWindow + kSec);            // must close, not abort

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kOutcome], "ok");
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyFilled]), 3.0)
      << "the overshoot is real and must be reported, not clamped away";
}

TEST_F(SlippageProbeTest, ALegStampedBeforeItsWindowIsClampedNotAborted) {
  // The leg's end stamp is the Fill's transactTime, set when the order was
  // RECEIVED -- before the modelled --ob-delay-us. An order submitted just
  // before the boundary and released after it carries a stamp earlier than the
  // window that owns it. That is the delay model working, not a bug, so it
  // clamps; asserting ended >= started across the two clocks aborted the close.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill_at(kT0 - kSec, en::bs::BUY, kAsk, kParent);   // stamped BEFORE the open
  fill_at(kT0 + kSec, en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(std::stoull(rows[1][kBuyNs]), 0ull)
      << "a leg cannot have ended before it began";
}

TEST_F(SlippageProbeTest, DriftIsMeasuredForwardFromTheCommonArrival) {
  // Both legs arrive together, so drift cannot be the gap between two arrivals
  // -- that difference is identically zero. It is measured forward: from the
  // arrival mid to the mid each leg finished on, and to the mid at the close.
  want_csv();
  build();
  quote(kBid, kAsk);                                  // arrival mid 23992
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent);                   // buy done at the arrival mid
  quote(kBid + 8, kAsk + 8);                          // market moves up 8 ticks
  fill(en::bs::SEL, kBid + 8, kParent);               // sell finishes up there
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyDrift]), 0.0)
      << "the buy leg finished before the market moved";
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSelDrift]), 8.0)
      << "the sell leg finished 8 ticks higher than the arrival";
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kDrift]), 8.0)
      << "the window closed 8 ticks above where it opened";
}

TEST_F(SlippageProbeTest, DriftIsZeroWhenTheMarketDoesNotMove) {
  want_csv();
  build();
  run_window(kT0);
  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kDrift]), 0.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyDrift]), 0.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSelDrift]), 0.0);
}

TEST_F(SlippageProbeTest, AShortLegAtShutdownIsNamedNotCalledSessionEnd) {
  // A leg that never fills holds its window open for the rest of the session,
  // so every later window simply never happens and the run ends with one row --
  // which run_grid.sh's `wc -l > 1` resume guard accepts as a completed cell.
  // The outcome has to say which side was short, or that case is
  // indistinguishable from the replay simply running out of data.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  fill(en::bs::BUY, kAsk, kParent);        // buy completes, sell never does
  tick(kT0 + 4 * kWindow);                 // long past the minimum, still open
  EXPECT_EQ(csv_rows().size(), 1u) << "it must keep waiting, as specified";

  actors::msg::Shutdown sd;
  TestHelper::invoke_handler(probe.get(), &sd, nullptr);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[1][kOutcome], "sel_short");
}

TEST_F(SlippageProbeTest, AnUnfilledLegScoresNoSlippageRatherThanTheRawPrice) {
  // vwap() is 0 for an unfilled leg. Guarding the touch and VWAP benchmarks on
  // the reference price alone wrote bid_fire - 0 ~ +23990 ticks into columns
  // whose real values are fractions of a tick.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);
  fill(en::bs::BUY, kAsk, kParent);

  actors::msg::Shutdown sd;
  TestHelper::invoke_handler(probe.get(), &sd, nullptr);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipSelVsTouch]), 0.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipSelVsVwap]), 0.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipVsTouch]), 0.0)
      << "a pair benchmark needs both legs";
}

TEST_F(SlippageProbeTest, DoesNotOpenAWindowThatCannotFinishInTheSession) {
  // Reopening with less than a full minimum left produced a window that closed
  // after the session and was emitted "ok", with its VWAPs and participation
  // denominators drawn from post-close liquidity.
  want_csv();
  build();
  quote(kBid, kAsk);

  // Walk windows up to the last boundary that still leaves a full minimum.
  uint64_t t = session_end() - 2 * kWindow;
  tick(t);                                       // opens the final legal window
  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent);
  tick(t + kWindow + kSec);                      // closes it; no room for another

  const size_t rows_after_close = csv_rows().size();
  fill(en::bs::BUY, kAsk, kParent);               // flow keeps arriving
  fill(en::bs::SEL, kBid, kParent);
  tick(session_end() + kWindow);

  EXPECT_EQ(csv_rows().size(), rows_after_close)
      << "no window may open without a full minimum left before the close";
}

TEST_F(SlippageProbeTest, ScoresEachLegAgainstItsOwnAggressorStream) {
  // The hit/take benchmark. Our buy leg rests on the bid and is filled when
  // someone HITS that bid, so the trades it competed with are the other bids
  // that were hit over the same interval -- not every trade, which would
  // include everyone who crossed the spread and so embed the spread itself.
  //
  //   is_hit()  resting BUY  -- a bid was hit    -- peer group for our BUY leg
  //   is_tak()  resting SEL  -- an offer taken   -- peer group for our SEL leg
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  // Hits print at 23980, takes at 23999 -- deliberately far apart and far from
  // our own fills, so a leg scored against the wrong stream cannot pass.
  trade(en::bs::BUY, 23980, 100);    // a bid was hit
  trade(en::bs::SEL, 23999, 100);    // an offer was taken

  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyHitVwap]), 23980.0)
      << "the buy leg must be scored against HITS, not takes";
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSelTakVwap]), 23999.0)
      << "the sell leg must be scored against TAKES, not hits";

  // Sign convention: positive is cost. We bought at kAsk (23994) against hits
  // at 23980, so we paid 14 more than the other filled bids -- a cost. We sold
  // at kBid (23990) against takes at 23999, so we received 9 less -- also a
  // cost.
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipBuyVsHit]), double(kAsk) - 23980.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipSelVsTak]), 23999.0 - double(kBid));

  // The peer group's SIZE, not just its price. Without it the VWAP cannot say
  // how thin the comparison was, and there is no denominator for a
  // participation rate against passive flow specifically.
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyHitVol]), 100.0) << "hits beside the buy leg";
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSelTakVol]), 100.0) << "takes beside the sell leg";

  // And they must not be the all-trades figure: 200 lots traded in total, 100
  // of each aggressor. A leg reading 200 here is summing both streams.
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyMktVol]), 200.0)
      << "the all-trades column still counts both";
}

TEST_F(SlippageProbeTest, ReportsNoAggressorBenchmarkWhenThatStreamIsEmpty) {
  // A leg with no trade of its own aggressor beside it has no peer group, and
  // must report 0 rather than a comparison against a VWAP of nothing. The
  // all-trades benchmark is unaffected -- it still has the other stream.
  want_csv();
  build();
  quote(kBid, kAsk);
  tick(kT0);

  trade(en::bs::BUY, 23980, 100);    // hits only; nothing took an offer
  fill(en::bs::BUY, kAsk, kParent);
  fill(en::bs::SEL, kBid, kParent);
  tick(kT0 + kWindow + kSec);

  auto rows = csv_rows();
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kBuyHitVwap]), 23980.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSelTakVwap]), 0.0) << "no takes, no benchmark";
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipSelVsTak]), 0.0);
  EXPECT_DOUBLE_EQ(std::stod(rows[1][kSlipVsAgg]), 0.0) << "a pair benchmark needs both";
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
