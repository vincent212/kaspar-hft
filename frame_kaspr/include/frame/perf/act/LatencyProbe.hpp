#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LatencyProbe -- a passive HI-priority subscriber to one TachBook. It reads
// two timestamps off each payload it is handed, subtracts, and accumulates the
// result. It sends nothing back and touches no book state.
//
// WHAT IT MEASURES. Three instants, all from the same clock
// (chutil::Time::epoch(), CLOCK_REALTIME in nanoseconds):
//
//   t0 = pl->hndl_tim_epoch  -- stamped in mdp3/handler_if.hpp as `recv_time`,
//                               when the feed handler finished with the packet.
//   t1 = pl->publish_ts      -- stamped in TachBook::publish_book (and in the
//                               trade path), immediately before fan-out.
//   t2 = Time::epoch()       -- read as the first statement of this actor's
//                               handler.
//
//   leg 1 = t1 - t0  "handler to published"
//   leg 2 = t2 - t1  "published to subscriber"
//
// The two are kept separate on purpose. They are different work: leg 1 is one
// mailbox hop into TachBook plus the whole book build (apply the order,
// uncross, create_point, fast_compare); leg 2 is one mailbox hop into this
// actor plus whatever delay the scheduler adds before its thread runs. Summing
// them into a single "wire to book" number would hide which half moved.
//
// WHAT IT DOES NOT MEASURE.
//
//   - Anything before t0. Wire time, NIC, kernel/onload receive path and the
//     feed handler's own decode are all upstream of the first stamp. t0 is this
//     host's clock, not the exchange's, so none of these is a wire-to-wire
//     latency and none is comparable to an exchange timestamp.
//   - Leg 1 cannot be split. There is no stamp between the mailbox hop into
//     TachBook and the start of the book build, so a slow leg 1 does not say
//     which of the two it was.
//
// THE DENOMINATOR, WHICH IS NOT THE ONE YOU PROBABLY WANT. Every count here is
// out of *messages TachBook chose to publish*, not out of messages that arrived
// on the wire. TachBook drops book updates before fan-out when: securityID does
// not match, the order is not in the map on a cancel, the price is out of
// bounds, the record is a recovery record, the new top-of-book is byte-equal to
// the previous one (fast_compare -- by far the largest of these), or the point
// is flagged baddata. The probe is downstream of all six and can see none of
// them. Published-message suppression is a separate measurement, not taken here.
//
// BOOK UPDATES AND TRADES ARE ACCUMULATED SEPARATELY, and must stay that way.
// They are selected by different rules: a book update survives all six filters
// above, a trade survives exactly one (order-not-found). Pooling them averages
// two populations with different admission criteria.
//
// ---------------------------------------------------------------------------
// JOINING TO QUEUE DEPTH
//
// The point of the binned output below is to test whether latency tracks
// mailbox depth. QLen samples depth for every actor on a wall-clock grid and
// stamps each pass with l3_qlen_t::t0 = chutil::Time::epoch(). This probe
// accumulates latency on the SAME grid, keyed by floor(t2 / bin_ns). The two
// keys are directly comparable: Timer::wake_up_at snaps to a boundary measured
// in ms since midnight on system_clock, 100 ms divides a day evenly, and
// Time::epoch() is the same clock -- so the midnight-based grid and the
// epoch-based floor agree bin for bin. Set bin_ms equal to QLen's period_ms.
//
// One QLen pass produces a depth for every actor at once, so a joined row
// carries both queues that matter: TachBook's (the leg 1 regressor) and this
// probe's (the leg 2 regressor).
//
// WHAT THE JOIN CAN AND CANNOT SUPPORT. It is a bin-level regression, not a
// per-message one: it relates *mean latency over 100 ms* to *depth sampled once
// in that 100 ms*. That is a real test if depth stays elevated for stretches
// long compared to the bin -- the gauge then tracks the regime and the slope is
// meaningful. If bursts are short compared to 100 ms, the gauge mostly samples
// an empty queue, the regressor is mostly measurement error, and the fitted
// slope is biased toward zero. So a strong correlation here is evidence; a weak
// one is NOT evidence of absence, and must not be reported as such. The burst
// duration that decides which case you are in is itself measurable -- from the
// per-bin `n` column, which is the message rate.
//
// Empty bins are emitted, not skipped. A 100 ms window with no published
// updates is a fact about the feed and it has to survive into the output, or
// any rate computed downstream is wrong.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <limits>

#include "chutil/Macros.hpp"
#include "chutil/Time.hpp"
#include "actors/Actor.hpp"
#include "actors/act/Timer.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Timeout.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "frame/ob/msg/Clear.hpp"
#include "frame/ob/msg/GapDected.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"

namespace frame::perf::act
{

  // Fixed-width linear histogram over nanoseconds, plus an overflow tail that
  // keeps its own count and sum so the tail is never silently truncated.
  //
  // Linear rather than logarithmic because the interesting range is narrow and
  // known: single-digit to low-hundreds of microseconds. 256 ns per bucket over
  // 512 buckets covers 0..131 us and costs 4 KB.
  //
  // No quantiles are computed here. The buckets are dumped raw and whoever
  // reads them decides what to ask of them.
  struct NsHist
  {
    static constexpr uint64_t BUCKET_NS = 256;
    static constexpr std::size_t NBUCKETS = 512;
    static constexpr uint64_t RANGE_NS = BUCKET_NS * NBUCKETS; // 131072

    uint64_t buckets[NBUCKETS] = {0};

    uint64_t n = 0;   // in-range + overflow
    uint64_t sum = 0; // ns, in-range + overflow
    uint64_t min = std::numeric_limits<uint64_t>::max();
    uint64_t max = 0;

    uint64_t over_n = 0;
    uint64_t over_sum = 0;

    void add(uint64_t ns) noexcept
    {
      ++n;
      sum += ns;
      if (ns < min)
        min = ns;
      if (ns > max)
        max = ns;

      if (ns >= RANGE_NS)
      {
        ++over_n;
        over_sum += ns;
        return;
      }
      ++buckets[ns / BUCKET_NS];
    }

    void dump(std::stringstream &ss, const char *label) const
    {
      ss << label
         << " n=" << n
         << " min_ns=" << (n ? min : 0)
         << " max_ns=" << max
         << " mean_ns=" << (n ? (sum / n) : 0)
         << " over_n=" << over_n
         << " over_mean_ns=" << (over_n ? (over_sum / over_n) : 0)
         << " bucket_ns=" << BUCKET_NS
         << " nbuckets=" << NBUCKETS
         << "\n";
      for (std::size_t i = 0; i < NBUCKETS; ++i)
      {
        if (!buckets[i])
          continue;
        ss << label << "\t" << (i * BUCKET_NS) << "\t" << buckets[i] << "\n";
      }
    }
  };

  struct LatencyProbe : public actors::Actor
  {
    // Series index. Four, because the two legs and the two message
    // populations are all separate denominators.
    enum : int { BOOK_L1 = 0, BOOK_L2 = 1, TRADE_L1 = 2, TRADE_L2 = 3, NSERIES = 4 };

    // One wall-clock bin, on the same grid QLen ticks on.
    //
    // count/sum/max only -- no per-bin histogram, no quantiles. Mean and max
    // are what a bin-level regression against depth can actually use, and they
    // are 20 bytes per series instead of 4 KB. The lifetime histograms below
    // carry the shape of the distribution; the bins carry its time series.
    struct Bin
    {
      uint64_t key = 0; // epoch ns, floored to bin_ns
      uint32_t n[NSERIES] = {0, 0, 0, 0};
      uint64_t sum[NSERIES] = {0, 0, 0, 0};
      uint32_t max[NSERIES] = {0, 0, 0, 0}; // ns, saturating at ~4.29 s
    };

    // The name is load-bearing. TachBook::subscribe_handler admits a HI-prio
    // subscriber only if its name contains "aggr" or "perf"; this is the "perf"
    // half of that gate. Renaming without the substring trips the ASSERTF at
    // startup.
    char name[128];
    const char *get_name() const override { return name; }

    actor_ptr ob; // the TachBook under measurement
    int sym;

    uint64_t bin_ns;        // MUST equal QLen's period_ms, in ns
    std::size_t max_bins;   // hard cap; oldest bins are flushed, never dropped
    std::string csv_path;   // "" => no file, console dump only
    int flush_s;            // 0 => flush only on shutdown / console command

    NsHist hist[NSERIES];
    std::vector<Bin> bins;  // contiguous grid, gaps filled with empty bins
    bool csv_header_done = false;

    // Rejected samples, by reason. All are expected to be zero. Counted
    // separately because a non-zero value on any one is a different finding.
    //
    // zero_t0: hndl_tim_epoch == 0. handler_if.hpp writes a literal 0 in two
    //   places -- the recovery record and the MBO snapshot. Neither should ever
    //   reach a subscriber: recovery MBOs are gated out before publish_book,
    //   and the snapshot type falls through TachBook's data_handler to
    //   "unknown message type". So zero here confirms that analysis, and
    //   non-zero means a record with no handler stamp is being published --
    //   which would compute leg 1 as publish_ts - 0, about 58 years, and
    //   poison everything downstream of it.
    // zero_t1: publish_ts == 0, i.e. a payload that reached a subscriber
    //   without passing either stamp site.
    // back_*: t1 < t0 or t2 < t1. Both stamps are CLOCK_REALTIME, which an NTP
    //   step can move backwards. Counted, never folded in: a negative duration
    //   is not a small duration.
    uint64_t rej_zero_t0 = 0;
    uint64_t rej_zero_t1 = 0;
    uint64_t rej_back_leg1 = 0;
    uint64_t rej_back_leg2 = 0;

    // Events that carry no timing, counted because they bound the validity of
    // the window: a Clear empties the book and a GapDetected means the feed
    // lost data, so samples either side of one are not from the same regime.
    uint64_t n_clear = 0;
    uint64_t n_gap = 0;

    uint64_t started_ts = 0;
    std::size_t flushed_upto = 0; // index of first not-yet-written bin

    LatencyProbe(actor_ptr _ob,
                 int _sym,
                 const char *tag,
                 int _bin_ms = 100,
                 const std::string &_csv_path = "",
                 int _flush_s = 60,
                 std::size_t _max_bins = 300000) // 8h20m at 100 ms
        : ob(_ob), sym(_sym),
          bin_ns(uint64_t(_bin_ms) * 1000000ULL),
          max_bins(_max_bins),
          csv_path(_csv_path),
          flush_s(_flush_s)
    {
      snprintf(name, sizeof(name), "perf_LatencyProbe_%s", tag ? tag : "unk");

      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
      MESSAGE_HANDLER(frame::ob::msg::EndOfBurst, eob_handler);
      MESSAGE_HANDLER(frame::ob::msg::TradeNotify, trade_handler);
      MESSAGE_HANDLER(frame::ob::msg::Clear, clear_handler);
      MESSAGE_HANDLER(frame::ob::msg::GapDetected, gap_handler);
      MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);

      ASSERT(ob, "no order book to probe");
      ASSERT(_bin_ms > 0, "bin_ms must be positive");
    }

    void start_handler(const actors::msg::Start *) noexcept
    {
      started_ts = chutil::Time::epoch();
      // Reserve up front: a push_back that reallocates mid-burst would land
      // inside a measured leg 2 and show up as a tail this probe invented.
      bins.reserve(max_bins);
      ob->send(new frame::mda::msg::Subscribe(frame::mda::msg::Subscribe::HI), this);
      if (flush_s > 0)
        actors::act::Timer::wake_up_in(this, flush_s, 0);
    }

    void shutdown_handler(const actors::msg::Shutdown *) noexcept
    {
      flush_csv(true);
    }

    void timeout_handler(const actors::msg::Timeout *) noexcept
    {
      // Rearm first so a slow write does not stretch the flush interval.
      if (flush_s > 0)
        actors::act::Timer::wake_up_in(this, flush_s, 0);
      flush_csv(false);
    }

    // Return the bin for this timestamp, extending the grid as needed. Gaps are
    // materialised as empty bins rather than skipped -- a 100 ms window with no
    // published updates is a measurement, and dropping it would silently turn a
    // quiet period into a shorter sample.
    Bin *bin_for(uint64_t ts) noexcept
    {
      const uint64_t key = ts - (ts % bin_ns);

      if (bins.empty())
      {
        bins.push_back(Bin{});
        bins.back().key = key;
        return &bins.back();
      }

      const uint64_t last = bins.back().key;
      if (key == last)
        return &bins.back();

      // Out-of-order or backwards: only reachable through an NTP step, which
      // the back_* counters already flag. Fold into the current bin rather
      // than corrupting the grid ordering.
      if (key < last)
        return &bins.back();

      const uint64_t gaps = (key - last) / bin_ns;
      if (bins.size() + gaps > max_bins)
      {
        // Ring the buffer rather than stop measuring. Everything before
        // flushed_upto is already on disk; if nothing is, we drop the oldest
        // and say so in the console dump.
        flush_csv(false);
        if (flushed_upto > 0)
        {
          bins.erase(bins.begin(), bins.begin() + flushed_upto);
          flushed_upto = 0;
        }
        if (bins.size() + gaps > max_bins)
        {
          bins.clear();
          flushed_upto = 0;
          bins.push_back(Bin{});
          bins.back().key = key;
          return &bins.back();
        }
      }

      for (uint64_t g = 1; g <= gaps; ++g)
      {
        bins.push_back(Bin{});
        bins.back().key = last + g * bin_ns;
      }
      return &bins.back();
    }

    void record(int series, uint64_t ns, Bin *b) noexcept
    {
      hist[series].add(ns);
      ++b->n[series];
      b->sum[series] += ns;
      const uint32_t ns32 = ns > 0xFFFFFFFFULL ? 0xFFFFFFFFu : uint32_t(ns);
      if (ns32 > b->max[series])
        b->max[series] = ns32;
    }

    // The one piece of work on the hot path. t2 is read by the caller before
    // anything else, so nothing this actor does lands inside leg 2.
    void sample(const frame::mda::msg::data_pay_load *pl,
                int s_leg1, int s_leg2, uint64_t t2) noexcept
    {
      if (!pl)
        return;

      const uint64_t t0 = pl->hndl_tim_epoch;
      const uint64_t t1 = pl->publish_ts;

      if (t1 == 0)
      {
        ++rej_zero_t1;
        return;
      }
      if (t0 == 0)
      {
        ++rej_zero_t0;
        // Leg 2 is still well defined here -- it needs only t1 and t2. It is
        // deliberately not recorded, so the two legs always share a
        // denominator and can be compared row for row.
        return;
      }
      if (t1 < t0)
      {
        ++rej_back_leg1;
        return;
      }
      if (t2 < t1)
      {
        ++rej_back_leg2;
        return;
      }

      // Binned on t2, the instant of observation -- the same clock reading
      // QLen's tick uses, so the join key is exact.
      Bin *b = bin_for(t2);
      record(s_leg1, t1 - t0, b);
      record(s_leg2, t2 - t1, b);
    }

    void eob_handler(const frame::ob::msg::EndOfBurst *m) noexcept
    {
      const uint64_t t2 = chutil::Time::epoch();
      sample(m->payload.get(), BOOK_L1, BOOK_L2, t2);
    }

    void trade_handler(const frame::ob::msg::TradeNotify *m) noexcept
    {
      const uint64_t t2 = chutil::Time::epoch();
      sample(m->payload.get(), TRADE_L1, TRADE_L2, t2);
    }

    void clear_handler(const frame::ob::msg::Clear *) noexcept { ++n_clear; }
    void gap_handler(const frame::ob::msg::GapDetected *) noexcept { ++n_gap; }

    // Append completed bins. The current (still filling) bin is held back
    // unless `final`, so no bin is ever written twice or written short.
    //
    // This runs on the probe's own thread, so the bin containing a flush pays
    // for the write. That is why the flush interval is seconds, not the bin
    // period: the contaminated bins are few, and the CSV records the flush
    // instants so they can be identified and excluded rather than guessed at.
    void flush_csv(bool final_flush) noexcept
    {
      if (csv_path.empty() || bins.empty())
        return;

      const std::size_t end = final_flush ? bins.size() : (bins.size() - 1);
      if (end <= flushed_upto)
        return;

      std::ofstream f(csv_path, std::ios::app);
      if (!f.is_open())
        return;

      if (!csv_header_done)
      {
        f << "# probe=" << name << " sym=" << sym << " ob=" << ob->get_name()
          << " bin_ns=" << bin_ns << "\n";
        f << "bin_key_ns,"
             "book_l1_n,book_l1_sum_ns,book_l1_max_ns,"
             "book_l2_n,book_l2_sum_ns,book_l2_max_ns,"
             "trade_l1_n,trade_l1_sum_ns,trade_l1_max_ns,"
             "trade_l2_n,trade_l2_sum_ns,trade_l2_max_ns,"
             "flush\n";
        csv_header_done = true;
      }

      for (std::size_t i = flushed_upto; i < end; ++i)
      {
        const Bin &b = bins[i];
        f << b.key;
        for (int s = 0; s < NSERIES; ++s)
          f << "," << b.n[s] << "," << b.sum[s] << "," << b.max[s];
        // Marks the bin this flush ran in, so its leg 2 can be excluded.
        f << "," << ((i + 1 == end) ? 1 : 0) << "\n";
      }
      flushed_upto = end;
    }

    // Console: `mdperf` dumps the lifetime histograms. Raw buckets, no
    // quantiles, no verdict. The time series lives in the CSV.
    void get_handler(const frame::cons::msg::Get *m)
    {
      if (m->what != "mdperf")
        return;

      const uint64_t now = chutil::Time::epoch();

      std::stringstream ss;
      ss << "probe=" << name
         << " sym=" << sym
         << " ob=" << ob->get_name()
         << " uptime_ns=" << (started_ts ? (now - started_ts) : 0)
         << " bin_ns=" << bin_ns
         << " bins_held=" << bins.size()
         << " bins_flushed=" << flushed_upto
         << " csv=" << (csv_path.empty() ? "-" : csv_path)
         << "\n";
      ss << "rejects"
         << " zero_t0=" << rej_zero_t0
         << " zero_t1=" << rej_zero_t1
         << " back_leg1=" << rej_back_leg1
         << " back_leg2=" << rej_back_leg2
         << " clear=" << n_clear
         << " gap=" << n_gap
         << "\n";

      static const char *labels[NSERIES] = {
          "book_leg1", "book_leg2", "trade_leg1", "trade_leg2"};
      for (int s = 0; s < NSERIES; ++s)
        hist[s].dump(ss, labels[s]);

      reply(new frame::cons::msg::Page(ss.str()));
    }
  };

}
