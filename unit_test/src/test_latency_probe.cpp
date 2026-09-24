/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Tests for frame::perf::act::LatencyProbe.
//
// The probe's whole value rests on two claims, and both are tested here rather
// than asserted in a comment:
//
//   1. Its bin key is the SAME key QLen ticks on. If that is wrong, every
//      joined row pairs a latency with the wrong queue depth and the headline
//      result is garbage in a way that looks fine.
//   2. A sample with a missing or backwards stamp is rejected and counted, not
//      folded in. A single hndl_tim_epoch of 0 computes leg 1 as roughly 58
//      years and would swamp any mean it touched.
//
// These drive sample() directly instead of running the actor system. That is
// deliberate: the arithmetic, the binning and the reject ladder are what can
// be wrong, and none of them needs a scheduler.
//
// NOTE ON THE SHAPE OF THIS FILE. It was written against a four-series probe
// (BOOK_L1/BOOK_L2/TRADE_L1/TRADE_L2) and a four-argument sample(). Leg 2 was
// removed because that hop had no queue instrumentation and unpinned threads,
// so it could not separate queueing from the scheduler -- and the test was not
// updated with it, so this file had not compiled since. Two consequences are
// now pinned explicitly rather than left implicit:
//
//   - sample() takes (payload, series, t2); series index == population index.
//   - a REJECTED sample still opens a bin and still increments all_n, because
//     all_n is taken before the reject ladder. It is a different denominator
//     from the per-leg n[], and the tests below say so.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "frame/perf/act/LatencyProbe.hpp"

using frame::perf::act::LatencyProbe;
using frame::perf::act::NsHist;

namespace
{

  // Minimal stand-in for the TachBook under measurement. The probe only ever
  // calls get_name() on it outside of start_handler, which these tests do not
  // run, so nothing else needs to exist.
  struct StubBook : public actors::Actor
  {
    const char *get_name() const override { return "stub_book"; }
  };

  constexpr int BIN_MS = 100;
  constexpr uint64_t BIN_NS = uint64_t(BIN_MS) * 1000000ULL;

  // A payload carrying just the two stamps the probe reads.
  boost::intrusive_ptr<frame::mda::msg::data_pay_load>
  make_pl(uint64_t t0, uint64_t t1)
  {
    auto pl = boost::intrusive_ptr<frame::mda::msg::data_pay_load>(
        new frame::mda::msg::data_pay_load());
    pl->hndl_tim_epoch = t0;
    pl->publish_ts = t1;
    return pl;
  }

  // A base timestamp on a bin boundary, far from any edge case.
  uint64_t base_key()
  {
    const uint64_t t = 1789000000000000000ULL;
    return t - (t % BIN_NS);
  }

} // namespace

// ---------------------------------------------------------------------------
// The join key.

// THE claim the CSV depends on. QLen wakes on Timer::wake_up_at, which floors
// milliseconds-since-midnight to the interval. The probe floors nanoseconds
// since epoch to the same interval. These agree only because both read
// system_clock and because 100 ms divides a day exactly -- if the period were,
// say, 700 ms, midnight-floor and epoch-floor would disagree by a remainder
// that changes every day. This test pins that down for the period actually
// used.
TEST(LatencyProbe, BinKeyMatchesQLenTimerGrid)
{
  using namespace std::chrono;

  for (int i = 0; i < 64; ++i)
  {
    // A real point in time, nudged so the sweep crosses many boundaries.
    const auto now = system_clock::now() + milliseconds(i * 37);
    const uint64_t epoch_ns =
        duration_cast<nanoseconds>(now.time_since_epoch()).count();

    // What LatencyProbe does.
    const uint64_t probe_key = epoch_ns - (epoch_ns % BIN_NS);

    // What Timer::wake_up_at does: floor ms-since-midnight to the interval.
    const auto today = floor<days>(now);
    const auto since_midnight = duration_cast<milliseconds>(now - today).count();
    const auto tick_ms = since_midnight - (since_midnight % BIN_MS);

    // Put the timer's tick back on the epoch axis to compare like for like.
    const uint64_t midnight_ns =
        duration_cast<nanoseconds>(today.time_since_epoch()).count();
    const uint64_t timer_key = midnight_ns + uint64_t(tick_ms) * 1000000ULL;

    ASSERT_EQ(probe_key, timer_key)
        << "bin grid does not match QLen's tick grid at i=" << i
        << "; every joined row would carry the wrong queue depth";
  }
}

// A quiet window is a measurement. If empty bins were skipped, any rate
// computed from the CSV downstream would divide by too few bins and overstate
// the message rate.
TEST(LatencyProbe, GapsAreMaterialisedAsEmptyBins)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);

  const uint64_t k = base_key();

  auto pl = make_pl(k + 1000, k + 2000);
  p.sample(pl.get(), LatencyProbe::BOOK_L1, k + 3000);

  // Next sample lands 5 bins later. The 4 silent bins in between must exist.
  auto pl2 = make_pl(k + 5 * BIN_NS + 1000, k + 5 * BIN_NS + 2000);
  p.sample(pl2.get(), LatencyProbe::BOOK_L1, k + 5 * BIN_NS + 3000);

  ASSERT_EQ(p.bins.size(), 6u);
  for (std::size_t i = 0; i < p.bins.size(); ++i)
    EXPECT_EQ(p.bins[i].key, k + i * BIN_NS) << "grid broken at bin " << i;

  EXPECT_EQ(p.bins[0].n[LatencyProbe::BOOK_L1], 1u);
  for (std::size_t i = 1; i <= 4; ++i)
    EXPECT_EQ(p.bins[i].n[LatencyProbe::BOOK_L1], 0u) << "bin " << i;
  EXPECT_EQ(p.bins[5].n[LatencyProbe::BOOK_L1], 1u);
}

TEST(LatencyProbe, SamplesInSameBinAccumulate)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);

  const uint64_t k = base_key();

  // Three samples in one bin, leg 1 of 1000/2000/3000 ns.
  for (uint64_t i = 1; i <= 3; ++i)
  {
    auto pl = make_pl(k + 10000, k + 10000 + i * 1000);
    p.sample(pl.get(), LatencyProbe::BOOK_L1, k + 20000 + i * 1000);
  }

  ASSERT_EQ(p.bins.size(), 1u);
  const auto &b = p.bins[0];
  EXPECT_EQ(b.n[LatencyProbe::BOOK_L1], 3u);
  EXPECT_EQ(b.sum[LatencyProbe::BOOK_L1], 1000u + 2000u + 3000u);
  EXPECT_EQ(b.max[LatencyProbe::BOOK_L1], 3000u);
}

// Book and trade pass different upstream filters, so they must never share a
// counter.
TEST(LatencyProbe, BookAndTradeStaySeparate)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);

  const uint64_t k = base_key();

  auto b1 = make_pl(k + 1000, k + 2000);
  p.sample(b1.get(), LatencyProbe::BOOK_L1, k + 4000);

  auto t1 = make_pl(k + 1000, k + 3000);
  p.sample(t1.get(), LatencyProbe::TRADE_L1, k + 9000);

  ASSERT_EQ(p.bins.size(), 1u);
  const auto &b = p.bins[0];
  EXPECT_EQ(b.n[LatencyProbe::BOOK_L1], 1u);
  EXPECT_EQ(b.sum[LatencyProbe::BOOK_L1], 1000u);
  EXPECT_EQ(b.n[LatencyProbe::TRADE_L1], 1u);
  EXPECT_EQ(b.sum[LatencyProbe::TRADE_L1], 2000u);

  // The full-population counters are per-population too, and they are the ones
  // a rate is computed from, so a leak between them would not show up in the
  // leg sums above.
  EXPECT_EQ(b.all_n[LatencyProbe::POP_BOOK], 1u);
  EXPECT_EQ(b.all_n[LatencyProbe::POP_TRADE], 1u);
}

// ---------------------------------------------------------------------------
// The reject ladder.

// S1a. handler_if.hpp writes a literal 0 for recovery records and MBO
// snapshots. Neither is supposed to reach a subscriber. If one does, leg 1
// would be computed as publish_ts - 0 -- about 58 years -- and would destroy
// the mean of whatever bin it landed in. It must be counted, not folded.
TEST(LatencyProbe, ZeroHandlerStampIsRejectedNotFolded)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);

  const uint64_t k = base_key();

  auto pl = make_pl(0, k + 2000);
  p.sample(pl.get(), LatencyProbe::BOOK_L1, k + 3000);

  EXPECT_EQ(p.rej_zero_t0, 1u);
  EXPECT_EQ(p.hist[LatencyProbe::BOOK_L1].n, 0u);

  // THE TWO DENOMINATORS ARE DIFFERENT, and this is the test that pins it.
  // A bin IS created and all_n IS incremented, because that counter is taken
  // before the reject ladder on purpose: it counts everything that arrived.
  // The leg counter n[] is taken after, so it counts only what was measurable.
  // Dividing an all_n column by l1_n, or vice versa, is therefore wrong, and
  // it is wrong silently.
  ASSERT_EQ(p.bins.size(), 1u);
  EXPECT_EQ(p.bins[0].all_n[LatencyProbe::POP_BOOK], 1u);
  EXPECT_EQ(p.bins[0].n[LatencyProbe::BOOK_L1], 0u);
}

TEST(LatencyProbe, ZeroPublishStampIsRejected)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);

  const uint64_t k = base_key();
  auto pl = make_pl(k + 1000, 0);
  p.sample(pl.get(), LatencyProbe::BOOK_L1, k + 3000);

  EXPECT_EQ(p.rej_zero_t1, 1u);
  EXPECT_EQ(p.hist[LatencyProbe::BOOK_L1].n, 0u);
  // t1 == 0 is checked FIRST, so a payload missing both stamps is counted
  // here and not in rej_zero_t0. The ladder order is what makes the two
  // counters add up to the rejected total instead of double-counting.
  EXPECT_EQ(p.rej_zero_t0, 0u);
}

// Both stamps are CLOCK_REALTIME, which NTP can step backwards. A negative
// duration is not a small duration, so it is counted rather than clamped: on
// unsigned subtraction t1 - t0 with t1 < t0 wraps to something near 2^64, and
// one of those in a bin sum destroys the mean.
//
// There is only leg 1 left to reject on. The leg-2 half of this test (t2 < t1,
// counted in rej_back_leg2) went with the leg itself -- see the series enum in
// LatencyProbe.hpp for why that hop stopped being measurable.
TEST(LatencyProbe, BackwardsLeg1IsRejected)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);

  const uint64_t k = base_key();

  auto a = make_pl(k + 5000, k + 1000); // t1 < t0
  p.sample(a.get(), LatencyProbe::BOOK_L1, k + 9000);

  EXPECT_EQ(p.rej_back_leg1, 1u);
  EXPECT_EQ(p.hist[LatencyProbe::BOOK_L1].n, 0u);

  // Rejected after the bin was opened and counted -- same split as the
  // zero-stamp case above.
  ASSERT_EQ(p.bins.size(), 1u);
  EXPECT_EQ(p.bins[0].all_n[LatencyProbe::POP_BOOK], 1u);
  EXPECT_EQ(p.bins[0].n[LatencyProbe::BOOK_L1], 0u);
  EXPECT_EQ(p.bins[0].sum[LatencyProbe::BOOK_L1], 0u) << "wrapped duration folded in";
}

// Checked before bin_for(), so unlike the rejects above this one produces no
// bin at all.
TEST(LatencyProbe, NullPayloadIsIgnored)
{
  StubBook book;
  LatencyProbe p(&book, 1, "test", BIN_MS, "", 0);
  p.sample(nullptr, LatencyProbe::BOOK_L1, base_key());
  EXPECT_TRUE(p.bins.empty());
}

// ---------------------------------------------------------------------------
// The histogram tail.

// The overflow tail keeps its own count and sum, so a slow outlier is never
// silently truncated into the top bucket.
TEST(NsHistTest, OverflowKeepsCountAndSum)
{
  NsHist h;
  h.add(1000);
  h.add(NsHist::RANGE_NS + 500000); // past 131 us

  EXPECT_EQ(h.n, 2u);
  EXPECT_EQ(h.over_n, 1u);
  EXPECT_EQ(h.over_sum, NsHist::RANGE_NS + 500000);
  EXPECT_EQ(h.max, NsHist::RANGE_NS + 500000);
  EXPECT_EQ(h.min, 1000u);
  EXPECT_EQ(h.sum, 1000u + NsHist::RANGE_NS + 500000);
}

TEST(NsHistTest, BucketIndexIsLinear)
{
  NsHist h;
  h.add(0);
  h.add(NsHist::BUCKET_NS - 1);
  h.add(NsHist::BUCKET_NS);

  EXPECT_EQ(h.buckets[0], 2u);
  EXPECT_EQ(h.buckets[1], 1u);
}

// ---------------------------------------------------------------------------
// CSV output.

// Only completed bins are written. The bin still filling is held back, or it
// would be written short and then written again on the next flush.
TEST(LatencyProbe, CsvWritesOnlyCompletedBinsAndIsJoinable)
{
  const std::string path = "/tmp/kh_test_latency_probe.csv";
  std::remove(path.c_str());

  StubBook book;
  LatencyProbe p(&book, 42, "test", BIN_MS, path, 0);

  const uint64_t k = base_key();

  // Three bins' worth of samples, leg 1 growing bin over bin.
  for (uint64_t bin = 0; bin < 3; ++bin)
  {
    for (uint64_t j = 0; j < 4; ++j)
    {
      const uint64_t t0 = k + bin * BIN_NS + 1000;
      const uint64_t t1 = t0 + (bin + 1) * 1000;
      p.sample(make_pl(t0, t1).get(), LatencyProbe::BOOK_L1, t1 + 500);
    }
  }

  p.flush_csv(false);
  // Bin 2 is still filling, so only bins 0 and 1 are on disk.
  EXPECT_EQ(p.flushed_upto, 2u);

  p.flush_csv(true);
  EXPECT_EQ(p.flushed_upto, 3u);

  std::ifstream f(path);
  ASSERT_TRUE(f.is_open());

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(f, line))
    lines.push_back(line);

  // one '#' metadata line, one header, three data rows
  ASSERT_EQ(lines.size(), 5u);
  EXPECT_EQ(lines[0].rfind("#", 0), 0u);
  EXPECT_EQ(lines[1].rfind("bin_key_ns,", 0), 0u);

  // Keys must be on the grid and strictly increasing, or a join against QLen
  // would mispair rows.
  for (std::size_t i = 0; i < 3; ++i)
  {
    const uint64_t key = std::stoull(lines[2 + i].substr(0, lines[2 + i].find(',')));
    EXPECT_EQ(key, k + i * BIN_NS);
    EXPECT_EQ(key % BIN_NS, 0u);
  }

  std::remove(path.c_str());
}

// Show the numbers rather than only asserting on them: this prints a small
// worked example of the CSV so the format can be eyeballed.
TEST(LatencyProbe, ShowWorkedExample)
{
  StubBook book;
  LatencyProbe p(&book, 42, "demo", BIN_MS, "", 0);

  const uint64_t k = base_key();

  // Bin 0: quiet, 3 msgs, leg 1 about 4 us.
  // Bin 1: busy, 40 msgs, leg 1 about 30 us -- the shape a depth-driven
  //        latency would have.
  //
  // `obs_lag` is only the offset from publish to the probe's own observation.
  // It used to be recorded as leg 2; it is not recorded any more, and it is
  // kept here solely so t2 lands somewhere plausible rather than on t1.
  const struct { int n; uint64_t leg1; uint64_t obs_lag; } script[] = {
      {3, 4000, 900}, {40, 30000, 12000}};

  for (int bin = 0; bin < 2; ++bin)
    for (int j = 0; j < script[bin].n; ++j)
    {
      const uint64_t t0 = k + uint64_t(bin) * BIN_NS + 1000 + uint64_t(j);
      const uint64_t t1 = t0 + script[bin].leg1;
      p.sample(make_pl(t0, t1).get(), LatencyProbe::BOOK_L1,
               t1 + script[bin].obs_lag);
    }

  std::printf("\n  bin_key_ns            n   mean_leg1_ns  max_leg1_ns   all_n\n");
  for (const auto &b : p.bins)
  {
    const uint32_t n = b.n[LatencyProbe::BOOK_L1];
    std::printf("  %llu  %3u   %10llu   %10u   %5llu\n",
                (unsigned long long)b.key, n,
                (unsigned long long)(n ? b.sum[LatencyProbe::BOOK_L1] / n : 0),
                b.max[LatencyProbe::BOOK_L1],
                (unsigned long long)b.all_n[LatencyProbe::POP_BOOK]);
  }
  std::printf("  lifetime book_leg1: n=%llu min=%llu max=%llu mean=%llu\n\n",
              (unsigned long long)p.hist[LatencyProbe::BOOK_L1].n,
              (unsigned long long)p.hist[LatencyProbe::BOOK_L1].min,
              (unsigned long long)p.hist[LatencyProbe::BOOK_L1].max,
              (unsigned long long)(p.hist[LatencyProbe::BOOK_L1].sum /
                                   p.hist[LatencyProbe::BOOK_L1].n));

  ASSERT_EQ(p.bins.size(), 2u);
  EXPECT_EQ(p.bins[0].n[LatencyProbe::BOOK_L1], 3u);
  EXPECT_EQ(p.bins[1].n[LatencyProbe::BOOK_L1], 40u);
}
