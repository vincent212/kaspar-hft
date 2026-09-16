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
//   t0 = pl->hndl_tim_epoch  -- SOCKET READ TIME, despite the name. Traced
//                               2026-09-16: handler_if.hpp assigns
//                               `l3.handlerendtim = recv_time` at 592, 706,
//                               790 and 851, and recv_time is passed down
//                               unchanged from MessageProcessor::processq,
//                               which got it from message_buffer::recv_ts,
//                               which SocketReader::read() stamped with
//                               Time::epoch() the instant recvmsg returned --
//                               BEFORE the send into MsgBuf. `handlerendtim`
//                               is NOT a handler-end stamp. Do not trust the
//                               field name; it survived a refactor the
//                               meaning did not.
//   t1 = pl->publish_ts      -- stamped in TachBook::publish_book:1237 and in
//                               the trade path at 968, in both cases the
//                               statement immediately before the send to
//                               subscribers.
//   t2 = Time::epoch()       -- read as the first statement of this actor's
//                               handler.
//
//   leg 1 = t1 - t0  "socket read to published"
//   leg 2 = t2 - t1  "published to subscriber"
//
// WHAT IS INSIDE LEG 1. Because t0 is the socket read, leg 1 covers five
// things, not one:
//   (a) the rest of SocketReader's read loop, then send() into MsgBuf;
//   (b) THE WAIT IN THE MsgBuf MAILBOX -- the queue whose depth-at-enqueue is
//       the ingress_qlen column below;
//   (c) dequeue, fast_send, and the MDP3 decode, all inline on MsgBuf's thread;
//   (d) a second mailbox hop, into TachBook. This one is NOT measured by
//       ingress_qlen. QLen's TachBook gauge is the regressor for it.
//   (e) the book build: apply the order, uncross, create_point, fast_compare.
//
// This is why ingress_qlen is a causal regressor for leg 1 and not merely a
// load proxy: depth-at-arrival is the count of messages that must drain before
// this one, and that drain is component (b), inside the interval being timed.
//
// The two legs are kept separate on purpose. Leg 2 is one mailbox hop into
// this actor plus whatever delay the scheduler adds before its thread runs.
// Summing them into a single "wire to book" number would hide which half moved.
//
// WHAT IT DOES NOT MEASURE.
//
//   - Anything before t0. Wire time, NIC, kernel/onload receive path and the
//     feed handler's own decode are all upstream of the first stamp. t0 is this
//     host's clock, not the exchange's, so none of these is a wire-to-wire
//     latency and none is comparable to an exchange timestamp.
//   - Leg 1 cannot be split. There is no stamp anywhere between t0 and t1, so
//     a slow leg 1 does not say which of its five components (a)..(e) above it
//     was. Two of the five are queue waits. Attributing a leg 1 to "the book
//     build" without a stamp between (d) and (e) is a guess.
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
//
// ---------------------------------------------------------------------------
// THE qlen COLUMNS: WHAT NUMBER THIS IS
//
// *_qlen_sum and *_qlen_max carry pl->ingress_qlen, which is the OCCUPANCY OF
// THE CIRCULAR BUFFER BACKING THE MsgBuf ACTOR'S MAILBOX, in messages, at the
// instant the packet this payload was decoded from was enqueued into it. It is
// not a byte count, not a socket receive-queue depth, and not a latency.
//
// Where it comes from, end to end:
//
//   actors/cpp/Actor.cpp
//                     add_message_to_queue() calls dest->circ_buf_len() and
//                     writes it to Message::qlen on every enqueue. This
//                     already existed; nothing here samples anything.
//   MsgBuf.hpp        processq_handler copies Message::qlen onto the
//                     message_buffer, because MessageProcessor.hpp:216 copies
//                     the buffer BY VALUE into the reorder map and anything
//                     left on the Message is lost there.
//   MessageProcessor  hands it to the decoder for the packet about to be
//                     decoded -- the reorder-map entry's own qlen, not the
//                     arriving packet's.
//   handler_if.hpp    stamps it onto bfile::l3_mbo_v2_t::ingress_qlen.
//   TachBook.hpp      copies it to data_pay_load::ingress_qlen.
//
// WHY THIS QUEUE AND NOT ANOTHER. Both socket reader threads (feed A and feed
// B) send into the single shared MsgBuf mailbox. It is the one place in the
// ingress path where two producer threads contend on one consumer, so it is
// the only mailbox whose depth means "decode is falling behind the wire".
// The socket readers' own ring buffers are thread-local and never contended.
//
// THIS IS NOT THE QLen GAUGE. QLen is a 100 ms wall-clock sampler: it reads
// every actor's depth on a timer, so a burst shorter than its period is
// invisible to it. ingress_qlen is per message, taken at enqueue, and cannot
// miss a burst. When both are joined on bin_key_ns they answer different
// questions and are expected to disagree; QLen's TachBook and probe depths
// are still the right regressors for legs 1 and 2, because ingress_qlen is
// upstream of both.
//
// PRECISION. The implementation that actually runs is BQueue.hpp:109,
// `circ_buf_len() { return cb_.size(); }` -- a read of the ring's own counter
// WITHOUT taking the queue lock, deliberately, so the measurement does not
// contend on the mutex it is measuring. (Queue.hpp:43 is the virtual default
// and does the opposite: it forwards to the LOCKED length(). BQueue overrides
// it, so that path is not the one on the hot path here.)
//
// Two consequences. The value is approximate under concurrent push/pop. And
// it counts cb_ only: BQueue::length() returns cb_.size() + overflow_.size(),
// but circ_buf_len() deliberately omits overflow_ because std::deque::size()
// is several pointer loads and reads as garbage while another thread mutates
// it. So the number saturates at cb_.capacity() (ACTOR_BQUEUE_SIZE), and a
// reading equal to it means "at least this deep", not "exactly this deep".
//
// ZEROS ARE REAL AND ARE THE COMMON CASE. An idle mailbox reads 0. So does
// anything that never crossed it: recovery snapshots, replayed records
// (l3_mbo_v2_t::ingress_qlen is absent from the on-disk packed struct on
// purpose), and any message delivered by fast_send, which dispatches inline
// and never enqueues. Do not read a bin of zeros as missing data. The
// *_qlen_nonzero counters in the console dump give the fraction that was not
// zero, which is the denominator that makes the mean interpretable.
//
// The denominator for *_qlen_sum is the matching *_l1_n column -- qlen is
// accumulated in sample(), under exactly the same admission rules as the two
// legs, so every row is self-consistent.

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
    // Series index. Two, one per message population.
    //
    // There was a leg 2 -- publish_ts to handler entry -- and it is gone. That
    // hop has no queue instrumentation and the threads are not pinned, so
    // nothing in the measurement separated genuine queueing from the scheduler
    // taking the core away. A number with two indistinguishable causes is not
    // a measurement, and it was costing eight columns.
    //
    // Leg 1, t0 -> publish_ts, is what remains: socket read to book published.
    // That hop HAS its queue instrumented, via ingress_qlen.
    enum : int { BOOK_L1 = 0, TRADE_L1 = 1, NSERIES = 2 };

    // Populations, for the per-packet accumulators. With leg 2 gone there is
    // exactly one series per population, so series index == population index
    // and the old `pop = series / 2` is gone with it. NPOP is kept as a
    // separate name because the two are different ideas that now coincide.
    enum : int { POP_BOOK = 0, POP_TRADE = 1, NPOP = 2 };

    // One wall-clock bin, on the same grid QLen ticks on.
    //
    // count/sum/min/max only -- no per-bin histogram, no quantiles. A median
    // is not additive: it cannot be recovered from n/sum/min/max, and storing
    // enough to recover one would mean a 4 KB histogram per bin per series.
    // Mean (sum/n), min and max are what a bin-level regression against depth
    // can actually use, and they are 24 bytes per series. The lifetime
    // histograms below carry the shape of the distribution -- a median IS
    // computable there, at 256 ns resolution, for the whole run.
    struct Bin
    {
      static constexpr uint32_t NOMIN = 0xFFFFFFFFu; // sentinel, never written

      uint64_t key = 0; // epoch ns, floored to bin_ns
      uint32_t n[NSERIES] = {0, 0};
      uint64_t sum[NSERIES] = {0, 0};
      uint32_t max[NSERIES] = {0, 0}; // ns, saturating at ~4.29 s
      uint32_t min[NSERIES] = {NOMIN, NOMIN};

      // MsgBuf mailbox occupancy, in messages. No separate count: the
      // denominator is n[BOOK_L1] / n[TRADE_L1], because qlen is accumulated
      // in the same branch of sample() that records the legs.
      uint64_t qlen_sum[NPOP] = {0, 0};
      uint32_t qlen_max[NPOP] = {0, 0};
      uint32_t qlen_min[NPOP] = {NOMIN, NOMIN};

      // ---- pkt_entry_idx. TWO DIFFERENT DENOMINATORS ON ONE ROW. ----
      //
      // all_n / all_pkt_n are the FULL population: every payload handed to
      // this probe, counted BEFORE the t0/t1 rejects.
      //
      // They were added when SocketReader stamped recv_ts on 1 packet in 5 and
      // sample() therefore discarded four fifths of arrivals on `t0 == 0`.
      // That subsample is GONE -- every packet is stamped now, see the
      // measurement in SocketReader::read -- so all_n and the l1_n counts
      // should agree and the sample_ratio below should read 1.0.
      //
      // They are kept, because they no longer measure the same thing and that
      // is the point: all_n counts BEFORE the rejects and l1_n counts AFTER
      // them, so all_n - l1_n is now exactly the reject count. A divergence
      // used to be the expected case; it is now a fault indicator, and it is
      // on the row rather than buried in a console counter.
      //
      //   all_n[p]              payloads seen
      //   all_pkt_n[p]          DISTINCT PACKETS among them
      //   messages per packet = all_n / all_pkt_n
      //   packets per second  = all_pkt_n / (bin_ns / 1e9)
      //
      // HOW all_pkt_n IS COUNTED, AND THE WAY THAT WAS WRONG FIRST TIME.
      // It counts TRANSITIONS OF pkt_seq_num, the MDP3 MsgSeqNum of the
      // packet, which is identical on every record decoded from that packet
      // (DataDecoder reads it once, before the message loop) and which CME
      // increments by one per packet per channel. A new value is a new
      // packet, exactly.
      //
      // It originally counted records with pkt_entry_idx == 0. That is wrong
      // twice over, and the 2026-09-16 12:33 run showed both:
      //
      //   1. MBO and MBOT SHARE ONE COUNTER, deliberately, so a mixed packet
      //      numbers in true decode order. But then a trade is idx == 0 only
      //      if it is the FIRST record in its packet, and trades follow book
      //      records. Measured trade idx_mean was 5.04, and the trade packet
      //      count came out at 6 against 1744 trade records -- "290 messages
      //      per packet", which is not a number, it is the bug.
      //   2. The dedup deletes records. A packet whose record 0 was dropped by
      //      pb_same contributes no idx == 0 at all and vanishes from the
      //      count entirely.
      //
      // A transition has neither failure. It fires on the FIRST SURVIVING
      // record of a packet whatever its position, and it fires independently
      // for each population, so a packet that produced both a book update and
      // a trade counts once in each -- which is what the two separate
      // denominators mean.
      //
      // This was first written against sendtim_epoch, the packet's
      // SendingTime, which is also constant across a packet. That worked but
      // had one unmeasurable failure: two consecutive packets on one channel
      // bearing the same nanosecond SendingTime merge into one, and from
      // inside there is no way to tell a merge from a large packet.
      // MsgSeqNum has no such case, so it is now plumbed and used.
      //
      //   chan_other_n[p]       channel packets carrying nothing for us
      //
      // Because the number is sequential, the boundary check also yields, for
      // free, the count of packets the sequence STEPPED OVER.
      //
      // THIS IS NOT PACKET LOSS. MsgSeqNum is per CHANNEL; this probe is per
      // SYMBOL and sits downstream of the dedup. A channel carries hundreds of
      // instruments, so between two packets bearing a published update for one
      // symbol the channel emitted many packets about other instruments. The
      // sequence steps over all of them. Measured at 0.6 to 430 times the
      // packet count depending on how much of its channel the symbol is.
      //
      // What it IS: the symbol's share of its channel. all_pkt_n /
      // (all_pkt_n + chan_other_n) is the fraction of channel packets that
      // produced a published update here, which is the number that says
      // whether a busy channel is busy on our account or someone else's.
      //
      // For real gap detection look at n_gap. MessageProcessor tracks
      // per-channel sequence UPSTREAM of the dedup, where every packet is
      // still visible, and emits GapDetected; gap_handler already counts it.
      //
      // A step that is negative or zero is never expected and is counted as 1
      // rather than silently ignored, because it would mean a sequence reset.
      //
      // idx_sum / idx_max are the SAMPLED set only, accumulated in the same
      // branch as the legs and qlen, so their denominator is n[BOOK_L1] /
      // n[TRADE_L1] exactly and leg1 can be regressed on mean idx row for row.
      // Dividing idx_sum by all_n instead would mix the two populations and
      // understate the mean by about five times.
      //
      // Neither count is "records decoded". Both are downstream of TachBook's
      // pb_baddata/pb_same dedup, so a packet that produced 40 records but
      // moved the top of book twice contributes 2 to all_n. idx is the only
      // witness to the other 38, which is the entire reason it is carried.
      uint32_t all_n[NPOP] = {0, 0};
      uint32_t all_pkt_n[NPOP] = {0, 0};
      uint64_t idx_sum[NPOP] = {0, 0};
      uint32_t idx_max[NPOP] = {0, 0};

      // Sum over closed packets of (highest idx seen in that packet + 1).
      //
      // This is a LOWER BOUND on records DECODED, where all_n is records
      // PUBLISHED. A surviving record stamped idx=37 proves 37 records were
      // decoded ahead of it in that packet even though the dedup ate them, so
      // the span is evidence about work this probe cannot otherwise see. It is
      // a bound and not a count because the packet's LAST records may also
      // have been dropped, and nothing downstream can know how many.
      //
      //   dedup suppression >= 1 - all_n / pkt_span_sum
      //
      // The file header says published-message suppression "is a separate
      // measurement, not taken here". This is the closest thing to it that the
      // subscriber side can produce, and it is a bound, so read it as one.
      uint64_t pkt_span_sum[NPOP] = {0, 0};
      uint32_t chan_other_n[NPOP] = {0, 0};

      // Packet interarrival moments, nanoseconds. See the file note on why
      // these are moments per bin and not one pooled histogram.
      //   ia_n[p]      gaps observed (packets in this bin, less the first)
      //   ia_sum[p]    sum of gaps, NANOSECONDS
      //   ia_sumsq[p]  sum of squared gaps, MICROSECONDS SQUARED
      //
      // THE TWO COLUMNS ARE IN DIFFERENT UNITS AND THAT IS DELIBERATE. sumsq
      // was ns^2 and it OVERFLOWED: sqrt(2^64) is 4.29e9 ns, so any gap over
      // 4.29 s wrapped. A real 18.2 s gap on ZNZ6 trade squared to 3.31e20,
      // wrapped to 1.76e19, and the bin variance came out NEGATIVE. Quiet
      // instruments produce such gaps routinely, so this was not an edge case.
      //
      // Converting the mean to microseconds before combining:
      //   mean_us = ia_sum / ia_n / 1000
      //   var_us2 = ia_sumsq / ia_n - mean_us^2
      //   CV      = sqrt(var_us2) / mean_us
      // CV == 1 is exponential, i.e. Poisson arrivals.
      //
      // A reader must still reject bins where sumsq*n < sum_us^2, because
      // Cauchy-Schwarz makes that impossible and so it detects any residual
      // wrap for free.
      uint32_t ia_n[NPOP] = {0, 0};
      uint64_t ia_sum[NPOP] = {0, 0};
      uint64_t ia_sumsq[NPOP] = {0, 0};

      // Batch size: messages PUBLISHED per packet. Banked when a packet
      // closes, so these three and pkt_span_sum above share pkt_closed_n as
      // their denominator -- NOT all_pkt_n, which is one higher because the
      // bin's last packet is still open when the bin is written.
      //   mean X = batch_sum / pkt_closed_n
      //   var X  = batch_sumsq / pkt_closed_n - mean^2
      // Message arrivals are simultaneous within a packet, so the arrival
      // process is the packet process compounded with this distribution.
      // Mean alone cannot distinguish a steady 3-per-packet from one that
      // alternates 1 and 5, and those queue very differently.
      uint32_t pkt_closed_n[NPOP] = {0, 0};
      uint64_t batch_sum[NPOP] = {0, 0};
      uint64_t batch_sumsq[NPOP] = {0, 0};
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
    // back_leg1: t1 < t0. Both stamps are CLOCK_REALTIME, which an NTP step
    //   can move backwards. Counted, never folded in: a negative duration is
    //   not a small duration. There was a back_leg2 and it went with leg 2.
    //   Note t2 is still read -- it is the bin key -- but it is no longer
    //   range-checked, because nothing is now computed from it.
    uint64_t rej_zero_t0 = 0;
    uint64_t rej_zero_t1 = 0;
    uint64_t rej_back_leg1 = 0;

    // Events that carry no timing, counted because they bound the validity of
    // the window: a Clear empties the book and a GapDetected means the feed
    // lost data, so samples either side of one are not from the same regime.
    uint64_t n_clear = 0;
    uint64_t n_gap = 0;

    // Lifetime MsgBuf-mailbox-depth accumulators, per population. Not a
    // histogram: the value is a small integer bounded by ACTOR_BQUEUE_SIZE, so
    // a linear NsHist over it would be mostly empty buckets.
    //
    // q_nonzero is the one that carries the finding. An all-zero qlen means
    // the mailbox was empty every time a published message crossed it, i.e.
    // decode never fell behind -- which makes q_sum/n an average of zeros and
    // makes any regression of latency on it meaningless. Report the two
    // together or neither.
    uint64_t q_n[NPOP] = {0, 0};
    uint64_t q_sum[NPOP] = {0, 0};
    uint32_t q_max[NPOP] = {0, 0};
    uint64_t q_nonzero[NPOP] = {0, 0};

    // Lifetime twins of Bin::all_n / all_pkt_n / idx_*, for the console dump.
    // a_n counts before the rejects, q_n after, so a_n / q_n is the reject
    // rate. With the subsample removed from SocketReader::read it should now
    // be 1.0; it read ~5 while the 1-in-5 was in force.
    uint64_t a_n[NPOP] = {0, 0};
    uint64_t a_pkt_n[NPOP] = {0, 0};
    uint64_t i_sum[NPOP] = {0, 0};
    uint32_t i_max[NPOP] = {0, 0};
    uint64_t a_span_sum[NPOP] = {0, 0};
    uint64_t a_chan_other[NPOP] = {0, 0};
    uint64_t a_ia_n[NPOP] = {0, 0};
    uint64_t a_ia_sum[NPOP] = {0, 0};
    uint64_t a_closed_n[NPOP] = {0, 0};
    uint64_t a_batch_sum[NPOP] = {0, 0};

    // Messages published so far from the packet currently open. Counts
    // PUBLISHED, so it is the batch a subscriber actually sees; cur_idx_max
    // alongside it bounds what was decoded before the dedup.
    uint32_t cur_msg_n[NPOP] = {0, 0};

    // Arrival time (t0) of the last packet, per population. NOTIM is the "no
    // packet seen yet" sentinel; 0 cannot serve, because a payload that never
    // crossed the socket carries t0 == 0 and those are real.
    static constexpr uint64_t NOTIM = 0xFFFFFFFFFFFFFFFFull;
    uint64_t last_arr[NPOP] = {NOTIM, NOTIM};

    // Packet-boundary detector state, per population. last_seq is the
    // MsgSeqNum of the packet currently being accumulated; cur_idx_max is the
    // highest pkt_entry_idx seen within it so far.
    //
    // NOPKT is the "no packet open yet" sentinel. It is held in a uint64_t,
    // one bit wider than the uint32_t sequence number it guards, precisely so
    // that no real value can collide with it -- MsgSeqNum legitimately reaches
    // 0 at a sequence reset, and handler_if writes 0 on the recovery and
    // snapshot paths, so 0 is not available as a sentinel.
    static constexpr uint64_t NOPKT = 0xFFFFFFFFFFFFFFFFull;
    uint64_t last_seq[NPOP] = {NOPKT, NOPKT};
    uint32_t cur_idx_max[NPOP] = {0, 0};

    uint64_t started_ts = 0;
    std::size_t flushed_upto = 0; // index of first not-yet-written bin

    // ---- raw arrival log, one record per packet -------------------------
    // See the note at the top of this patch: derived from nothing, derives
    // everything. 16 bytes, checked.
#pragma pack(push, 1)
    struct ArrRec
    {
      uint64_t t0;     // arrival ns (hndl_tim_epoch) of the packet
      uint32_t seq;    // MDP3 MsgSeqNum
      uint16_t batch;  // messages published from it
      uint16_t span;   // messages decoded, lower bound
    };
#pragma pack(pop)
    static_assert(sizeof(ArrRec) == 16, "ArrRec must stay 16 bytes");

    // 4096 records = 64 KB per population. Sized so that at a few hundred
    // packets a second the timer flush empties it long before it fills, and
    // so that if it ever does fill, the inline write is 64 KB and not 1 MB.
    static constexpr std::size_t ARRBUF = 4096;
    ArrRec arr_buf[NPOP][ARRBUF];
    std::size_t arr_n[NPOP] = {0, 0};
    std::ofstream arr_f[NPOP];
    bool arr_open[NPOP] = {false, false};
    uint64_t arr_written[NPOP] = {0, 0};
    uint64_t arr_inline[NPOP] = {0, 0}; // buffer-full writes; expect 0

    // ---- per-MESSAGE log, one record per admitted message ---------------
    //
    // WHY THIS EXISTS. qlen, idx and leg 1 are all in hand, per message, in
    // sample(). They were being folded into bin sums and a bin MAX and then
    // dropped. A max and a sum do not identify a joint distribution: once 166
    // messages collapse to (qlen_max, l1_sum, n), no arithmetic recovers which
    // message was deep. Every qlen-vs-latency table built off the CSV was
    // therefore a BOUND, not a measurement -- the group was SELECTED on the
    // deepest message in the bin but AVERAGED over all of them, and msgs/bin
    // itself rises with depth (ZN book: 6.7 at depth 0, 479.6 at depth 3-4),
    // so the effect was divided by a denominator that grew with it. That is
    // what flattened the observed ladder to ~1.2 us and hid the result.
    //
    // The CSV's qlen_SUM column does not have that defect -- mean depth and
    // mean latency share a denominator, and the bin-level fits built on it are
    // sound. But they stay ECOLOGICAL: they say "bins where messages saw
    // deeper queues were slower", not "a message at depth d costs a + b*d".
    // Only the pairing below can say the second thing.
    //
    // Emitted at the same point as the bin accumulators, after all four
    // rejects, so its population is exactly the row's l1_n -- same admission
    // rule, same denominator, joinable to the CSV on bin_key_ns.
#pragma pack(push, 1)
    struct MsgRec
    {
      uint64_t t1;    // publish ts; t1 - (t1 % bin_ns) == the CSV's bin_key_ns
      uint32_t l1_ns; // t1 - t0 for THIS message. Saturates at ~4.29 s.
      uint16_t qlen;  // ingress_qlen for THIS message (packets ahead in MsgBuf)
      uint16_t idx;   // decode position inside its own packet, THIS message
    };
#pragma pack(pop)
    static_assert(sizeof(MsgRec) == 16, "MsgRec must stay 16 bytes");

    // Messages outnumber packets ~1.1x on book and ~3x on trade, so this is 4x
    // the arrival buffer: 16384 records = 256 KB per population. At the Fed
    // peak (772 pkt/s * 1.09 batch) that is ~19 s of headroom between flushes.
    static constexpr std::size_t MSGBUF = 16384;
    MsgRec msg_buf[NPOP][MSGBUF];
    std::size_t msg_n[NPOP] = {0, 0};
    std::ofstream msg_f[NPOP];
    bool msg_open[NPOP] = {false, false};
    uint64_t msg_written[NPOP] = {0, 0};
    uint64_t msg_inline[NPOP] = {0, 0}; // buffer-full writes; expect 0
    uint64_t msg_sat[NPOP] = {0, 0};    // any field saturated; expect 0

    void msg_ensure(int pop) noexcept
    {
      if (msg_open[pop] || csv_path.empty())
        return;
      msg_open[pop] = true; // set first: a failed open must not retry per msg

      std::string base = csv_path;
      const std::size_t dot = base.rfind(".csv");
      if (dot != std::string::npos)
        base.erase(dot);
      base += (pop == POP_BOOK) ? "_book.msg" : "_trade.msg";

      msg_f[pop].open(base, std::ios::app | std::ios::binary);
      if (!msg_f[pop].is_open())
        return;

      if (msg_f[pop].tellp() == std::streampos(0))
      {
        char h[64];
        memset(h, 0, sizeof(h));
        memcpy(h, "KHMSGV01", 8);
        const uint32_t recsz = uint32_t(sizeof(MsgRec));
        const uint32_t ver = 1;
        memcpy(h + 8, &recsz, 4);
        memcpy(h + 12, &ver, 4);
        snprintf(h + 16, 32, "sym=%d pop=%s", sym,
                 (pop == POP_BOOK) ? "book" : "trade");
        const uint64_t now = chutil::Time::epoch();
        memcpy(h + 48, &now, 8);
        // bin_ns so a reader can rebuild the CSV's bin key without being told.
        memcpy(h + 56, &bin_ns, 8);
        msg_f[pop].write(h, sizeof(h));
      }
    }

    void msg_write(int pop) noexcept
    {
      if (!msg_n[pop])
        return;
      if (msg_open[pop] && msg_f[pop].is_open())
      {
        msg_f[pop].write(reinterpret_cast<const char *>(msg_buf[pop]),
                         std::streamsize(msg_n[pop] * sizeof(MsgRec)));
        msg_written[pop] += msg_n[pop];
      }
      msg_n[pop] = 0;
    }

    // Called from sample() with the values already in registers. No clock read,
    // no allocation, no branch on anything but the buffer bound.
    void msg_record(int pop, uint64_t t1, uint64_t l1,
                    uint32_t qlen, uint32_t idx) noexcept
    {
      if (csv_path.empty())
        return;
      msg_ensure(pop);
      if (!msg_f[pop].is_open())
        return;

      if (msg_n[pop] >= MSGBUF)
      {
        ++msg_inline[pop]; // never drop -- a hole would look like a quiet spell
        msg_write(pop);
      }
      MsgRec &r = msg_buf[pop][msg_n[pop]++];
      r.t1 = t1;
      // Saturate rather than wrap, and COUNT it. A wrapped value is a
      // plausible-looking lie; a saturated one with a non-zero counter beside
      // it is a known bound.
      if (l1 > 0xFFFFFFFFULL || qlen > 0xFFFFu || idx > 0xFFFFu)
        ++msg_sat[pop];
      r.l1_ns = uint32_t(l1 > 0xFFFFFFFFULL ? 0xFFFFFFFFu : l1);
      r.qlen = uint16_t(qlen > 0xFFFFu ? 0xFFFFu : qlen);
      r.idx = uint16_t(idx > 0xFFFFu ? 0xFFFFu : idx);
    }

    // Opened lazily on the first record, so an instrument that never trades
    // leaves no empty file. Header is 64 bytes: magic, record size, version,
    // then the instrument and population as text, so a file found on its own
    // still identifies itself.
    void arr_ensure(int pop) noexcept
    {
      if (arr_open[pop] || csv_path.empty())
        return;
      arr_open[pop] = true; // set first: a failed open must not retry per packet

      std::string base = csv_path;
      const std::size_t dot = base.rfind(".csv");
      if (dot != std::string::npos)
        base.erase(dot);
      base += (pop == POP_BOOK) ? "_book.arr" : "_trade.arr";

      arr_f[pop].open(base, std::ios::app | std::ios::binary);
      if (!arr_f[pop].is_open())
        return;

      if (arr_f[pop].tellp() == std::streampos(0))
      {
        char h[64];
        memset(h, 0, sizeof(h));
        memcpy(h, "KHARRIV1", 8);
        const uint32_t recsz = uint32_t(sizeof(ArrRec));
        const uint32_t ver = 1;
        memcpy(h + 8, &recsz, 4);
        memcpy(h + 12, &ver, 4);
        snprintf(h + 16, 32, "sym=%d pop=%s", sym,
                 (pop == POP_BOOK) ? "book" : "trade");
        const uint64_t now = chutil::Time::epoch();
        memcpy(h + 48, &now, 8);
        arr_f[pop].write(h, sizeof(h));
      }
    }

    void arr_write(int pop) noexcept
    {
      if (!arr_n[pop])
        return;
      if (arr_open[pop] && arr_f[pop].is_open())
      {
        arr_f[pop].write(reinterpret_cast<const char *>(arr_buf[pop]),
                         std::streamsize(arr_n[pop] * sizeof(ArrRec)));
        arr_written[pop] += arr_n[pop];
      }
      arr_n[pop] = 0;
    }

    void arr_flush() noexcept
    {
      for (int p = 0; p < NPOP; ++p)
      {
        arr_write(p);
        if (arr_open[p] && arr_f[p].is_open())
          arr_f[p].flush();
        // Per-message log rides the same tick. Both are binary side-files with
        // their own buffers and both must survive a shutdown, so neither may
        // sit behind the CSV's early return.
        msg_write(p);
        if (msg_open[p] && msg_f[p].is_open())
          msg_f[p].flush();
      }
    }

    // Called at packet close. t0/seq are the CLOSING packet's, which is why
    // this must run before last_arr[pop] is advanced to the new packet.
    void arr_record(int pop, uint64_t t0, uint64_t seq,
                    uint64_t batch, uint64_t span) noexcept
    {
      if (csv_path.empty() || t0 == 0)
        return; // t0 == 0 never crossed the socket; it has no arrival time
      arr_ensure(pop);
      if (!arr_f[pop].is_open())
        return;

      if (arr_n[pop] >= ARRBUF)
      {
        ++arr_inline[pop]; // never drop -- a hole would look like a quiet spell
        arr_write(pop);
      }
      ArrRec &r = arr_buf[pop][arr_n[pop]++];
      r.t0 = t0;
      r.seq = uint32_t(seq);
      // Saturate rather than wrap. A wrapped count is a plausible-looking lie.
      r.batch = uint16_t(batch > 0xFFFFu ? 0xFFFFu : batch);
      r.span = uint16_t(span > 0xFFFFu ? 0xFFFFu : span);
    }

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
      if (ns32 < b->min[series])
        b->min[series] = ns32;
    }

    // The one piece of work on the hot path. t2 is read by the caller before
    // anything else, so nothing this actor does lands inside leg 2.
    void sample(const frame::mda::msg::data_pay_load *pl,
                int series, uint64_t t2) noexcept
    {
      if (!pl)
        return;

      const uint64_t t0 = pl->hndl_tim_epoch;
      const uint64_t t1 = pl->publish_ts;
      const int pop = series; // one series per population since leg 2 went
      const uint32_t idx = pl->pkt_entry_idx;
      const uint32_t seq = pl->pkt_seq_num;

      // Binned on t2, the instant of observation -- the same clock reading
      // QLen's tick uses, so the join key is exact. Taken BEFORE the rejects
      // now, because the counters immediately below are deliberately over the
      // full population and must not inherit the legs' admission rules.
      Bin *b = bin_for(t2);

      // FULL POPULATION. Everything that arrived, counted before the four
      // rejects below can drop it. This used to matter enormously, because the
      // 1-in-5 recv_ts stamping in SocketReader meant ~80% of arrivals died on
      // `t0 == 0` for reasons that had nothing to do with the data. That
      // subsample is gone. What remains is still worth counting: pkt_entry_idx
      // is stamped at decode time rather than off a clock, so it is valid on
      // every payload including any the rejects discard, and the packet rate
      // and messages-per-packet stay exact no matter what the rejects do.
      //
      // These two columns do NOT share a denominator with anything else on the
      // row. Do not divide them into l1_n. See the Bin comment.
      ++b->all_n[pop];
      ++a_n[pop];

      // Packet boundary. See the Bin comment for why this is a sequence
      // transition and not `idx == 0`.
      if (uint64_t(seq) != last_seq[pop])
      {
        // Close the previous packet before opening this one. The span is
        // banked into whichever bin is current NOW, not the bin the packet
        // started in -- a packet spans nanoseconds and a bin spans 100 ms, so
        // the misattribution is at most one packet at a bin edge. Carrying the
        // start bin instead would mean holding an index across bin_for(),
        // which can erase and reallocate the vector.
        if (last_seq[pop] != NOPKT)
        {
          const uint64_t span = uint64_t(cur_idx_max[pop]) + 1;
          b->pkt_span_sum[pop] += span;
          a_span_sum[pop] += span;

          // Batch size of the packet just closed.
          const uint64_t bs = cur_msg_n[pop];
          ++b->pkt_closed_n[pop];
          b->batch_sum[pop] += bs;
          b->batch_sumsq[pop] += bs * bs;
          ++a_closed_n[pop];
          a_batch_sum[pop] += bs;

          // Raw arrival record for the packet just closed. Placed here, above
          // the interarrival block, because that block overwrites
          // last_arr[pop] with the NEW packet's t0 -- at this instant it still
          // holds the closing packet's arrival time, which is the one wanted.
          arr_record(pop, last_arr[pop], last_seq[pop], bs, span);

          // Packets the sequence stepped over -- other instruments on this
          // channel, NOT loss. See the Bin comment. Only meaningful once a
          // packet is already open, hence its place inside this branch. int64
          // because the subtraction must be allowed to go negative: on uint32
          // a sequence reset would wrap to ~4 billion and be banked as that
          // many packets.
          // Interarrival. Guarded on t0 != 0 at BOTH ends: a payload that
          // never crossed the socket has t0 == 0, and a gap measured against
          // one would be the whole epoch. Guarded on t0 >= last too, because
          // the two feed threads can deliver slightly out of order and a
          // negative gap would wrap the unsigned accumulator.
          if (t0 != 0 && last_arr[pop] != NOTIM && t0 >= last_arr[pop])
          {
            const uint64_t gap = t0 - last_arr[pop];
            ++b->ia_n[pop];
            b->ia_sum[pop] += gap;
            // SQUARED IN MICROSECONDS, NOT NANOSECONDS. In ns^2 a uint64
            // holds gaps up to sqrt(2^64) = 4.29e9 ns = 4.29 SECONDS, and a
            // quiet instrument produces gaps far longer than that: a measured
            // 18.2 s gap on ZNZ6 trade squared to 3.31e20, wrapped, and made
            // the bin variance NEGATIVE. Dividing by 1000 first buys 10^6 of
            // headroom, so the limit becomes ~4.29e6 s = 49 days.
            //
            // The truncation is the intended trade: gap/1000 loses at most
            // 999 ns of a quantity whose interesting values are microseconds
            // to seconds, and losing sub-microsecond precision on the SECOND
            // moment is worth not losing the moment entirely. ia_sum stays in
            // ns, so the mean keeps full resolution.
            const uint64_t gap_us = gap / 1000;
            b->ia_sumsq[pop] += gap_us * gap_us;
            ++a_ia_n[pop];
            a_ia_sum[pop] += gap;
          }
          if (t0 != 0)
            last_arr[pop] = t0;

          const int64_t step = int64_t(seq) - int64_t(last_seq[pop]);
          if (step != 1)
          {
            const uint32_t other =
                (step > 1) ? uint32_t(step - 1) : uint32_t(1);
            b->chan_other_n[pop] += other;
            a_chan_other[pop] += other;
          }
        }
        last_seq[pop] = uint64_t(seq);
        cur_msg_n[pop] = 1; // this record is the new packet's first
        if (last_arr[pop] == NOTIM && t0 != 0)
          last_arr[pop] = t0; // seeds the very first packet
        cur_idx_max[pop] = idx;
        ++b->all_pkt_n[pop];
        ++a_pkt_n[pop];
      }
      else
      {
        ++cur_msg_n[pop];
        if (idx > cur_idx_max[pop])
          cur_idx_max[pop] = idx;
      }

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
      record(series, t1 - t0, b);

      // SAMPLED SET. Within-packet decode position, accumulated here -- after
      // all four rejects -- so its denominator is the row's l1_n exactly and
      // leg1 can be regressed on mean idx without a join. This is the
      // regressor that separates serial decode cost from queue wait: qlen
      // counts PACKETS waiting, idx counts MESSAGES already decoded ahead of
      // this one inside its own packet. Measured at 700-1200 ns per position,
      // the same order as the depth effect, which is why leaving it out made
      // the per-symbol queueing slopes disagree by a factor of ten.
      b->idx_sum[pop] += idx;
      i_sum[pop] += idx;
      if (idx > b->idx_max[pop])
        b->idx_max[pop] = idx;
      if (idx > i_max[pop])
        i_max[pop] = idx;

      // MsgBuf mailbox depth for this packet. Recorded here, after all four
      // rejects, so it shares a denominator with the legs exactly: a row's
      // qlen_sum is over the same messages as its l1_n. See the qlen block in
      // the header comment for what the number is.
      const uint32_t q = pl->ingress_qlen;
      ++q_n[pop];
      q_sum[pop] += q;
      if (q > q_max[pop])
        q_max[pop] = q;
      if (q)
        ++q_nonzero[pop];
      b->qlen_sum[pop] += q;
      if (q > b->qlen_max[pop])
        b->qlen_max[pop] = q;
      if (q < b->qlen_min[pop])
        b->qlen_min[pop] = q;

      // THE PAIRING. q, idx and t1-t0 are all in registers here, all for the
      // SAME message. Everything above this line folds them into bin
      // aggregates, which is lossy in a way that cannot be undone downstream.
      // This keeps the triple intact. Last statement in sample() so that if it
      // ever has to be removed, nothing else moves.
      msg_record(pop, t1, t1 - t0, q, idx);
    }

    void eob_handler(const frame::ob::msg::EndOfBurst *m) noexcept
    {
      const uint64_t t2 = chutil::Time::epoch();
      sample(m->payload.get(), BOOK_L1, t2);
    }

    void trade_handler(const frame::ob::msg::TradeNotify *m) noexcept
    {
      const uint64_t t2 = chutil::Time::epoch();
      sample(m->payload.get(), TRADE_L1, t2);
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
      // Arrival log first, and above the early return below: it has its own
      // buffer and must still be flushed on a tick where no bin is ready.
      arr_flush();

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
        // No mean column and no median column. Mean is sum/n and is left to
        // the reader so the denominator is always visible next to it. A median
        // is not additive and is not recoverable from these -- the lifetime
        // histograms in the `mdperf` console dump are where that lives.
        //
        // min/max are over every payload the rejects admitted. SocketReader
        // no longer subsamples -- it stamps every packet -- so these are the
        // bin's real extremes, not bounds on a thinned draw. Rows written
        // before 2026-09-16 are 24 columns wide and ARE thinned 1-in-5; the
        // column count is the only thing that distinguishes them.
        f << "bin_key_ns,"
             "book_l1_n,book_l1_sum_ns,book_l1_min_ns,book_l1_max_ns,"
             "trade_l1_n,trade_l1_sum_ns,trade_l1_min_ns,trade_l1_max_ns,"
             // Messages, not nanoseconds -- MsgBuf mailbox occupancy. Divide
             // by book_l1_n / trade_l1_n respectively. See the qlen block at
             // the top of this file.
             "book_qlen_sum,book_qlen_min,book_qlen_max,"
             "trade_qlen_sum,trade_qlen_min,trade_qlen_max,"
             // pkt_entry_idx. READ THE DENOMINATORS -- they are not the same.
             //
             //   *_all_n      every payload that arrived, FULL population
             //   *_all_pkt_n  distinct packets among them, by MsgSeqNum
             //                transition -- NOT by idx == 0, see the Bin
             //                comment for why that was wrong
             //   *_pkt_closed_n  packets CLOSED in this bin. Denominator for
             //                *_span_sum and *_batch_*, and one lower than
             //                *_all_pkt_n because the last packet is still
             //                open when the bin is written.
             //   *_batch_sum  messages published, summed over closed packets
             //   *_batch_sumsq  and the sum of their squares. Batch size mean
             //                and variance; messages in one packet arrive
             //                simultaneously, so the message arrival process
             //                is the packet process compounded with this.
             //   *_ia_n       packet interarrival gaps in this bin
             //   *_ia_sum     sum of those gaps, NANOSECONDS
             //   *_ia_sumsq_us2  sum of their squares, MICROSECONDS SQUARED.
             //                The units differ from *_ia_sum on purpose and
             //                the name carries it, because ns^2 overflows
             //                uint64 at a 4.29 s gap and silently returned a
             //                negative variance. Convert the mean first:
             //                  mean_us = ia_sum/ia_n/1000
             //                  CV = sqrt(sumsq/n - mean_us^2)/mean_us
             //                CV == 1 is exponential, i.e. Poisson arrivals.
             //                Pool bins only after grouping by rate -- a
             //                mixture of exponentials is not exponential.
             //   *_chan_other_n  channel packets the sequence stepped over,
             //                i.e. ones about OTHER instruments. NOT loss --
             //                see the Bin comment. Channel share is
             //                all_pkt_n / (all_pkt_n + chan_other_n).
             //   *_idx_sum    sum of idx over the SAMPLED set; divide by
             //                *_l1_n, never by *_all_n
             //   *_idx_max    largest idx seen in the sampled set
             //   *_span_sum   sum of (max idx in packet + 1) over packets.
             //                LOWER BOUND on records decoded, where all_n is
             //                records published. Never smaller than all_n.
             //
             // messages per packet   = all_n / all_pkt_n
             // packets per second    = all_pkt_n / (bin_ns / 1e9)
             // dedup suppression    >= 1 - all_n / span_sum
             // all_n - l1_n is the number of payloads the rejects dropped.
             // Expect 0 now that every packet is stamped; it was ~4/5 of
             // all_n while SocketReader subsampled.
             "book_all_n,book_all_pkt_n,book_idx_sum,book_idx_max,book_span_sum,"
             "book_chan_other_n,book_ia_n,book_ia_sum,book_ia_sumsq_us2,"
             "book_pkt_closed_n,book_batch_sum,book_batch_sumsq,"
             "trade_all_n,trade_all_pkt_n,trade_idx_sum,trade_idx_max,trade_span_sum,"
             "trade_chan_other_n,trade_ia_n,trade_ia_sum,trade_ia_sumsq_us2,"
             "trade_pkt_closed_n,trade_batch_sum,trade_batch_sumsq,"
             "flush\n";
        csv_header_done = true;
      }

      for (std::size_t i = flushed_upto; i < end; ++i)
      {
        const Bin &b = bins[i];
        f << b.key;
        // An empty bin's min is still the sentinel. Emit 0, never 4294967295 --
        // that value would be read as a 4.29 s latency by anything downstream.
        // n == 0 on the same row is what says the 0 is "nothing here".
        for (int s = 0; s < NSERIES; ++s)
          f << "," << b.n[s] << "," << b.sum[s]
            << "," << (b.n[s] ? b.min[s] : 0u) << "," << b.max[s];
        for (int p = 0; p < NPOP; ++p)
          f << "," << b.qlen_sum[p]
            << "," << (b.qlen_min[p] == Bin::NOMIN ? 0u : b.qlen_min[p])
            << "," << b.qlen_max[p];
        for (int p = 0; p < NPOP; ++p)
          f << "," << b.all_n[p] << "," << b.all_pkt_n[p]
            << "," << b.idx_sum[p] << "," << b.idx_max[p]
            << "," << b.pkt_span_sum[p]
            << "," << b.chan_other_n[p]
            << "," << b.ia_n[p] << "," << b.ia_sum[p] << "," << b.ia_sumsq[p]
            << "," << b.pkt_closed_n[p] << "," << b.batch_sum[p]
            << "," << b.batch_sumsq[p];
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
         << " clear=" << n_clear
         << " gap=" << n_gap
         << "\n";

      // MsgBuf mailbox occupancy, in messages, not nanoseconds. nonzero= is
      // the number of samples where the mailbox was NOT empty; if it is 0 the
      // mean is a mean of zeros and says only that decode never fell behind.
      static const char *plabels[NPOP] = {"book", "trade"};
      for (int p = 0; p < NPOP; ++p)
        ss << "msgbuf_qlen_" << plabels[p]
           << " n=" << q_n[p]
           << " nonzero=" << q_nonzero[p]
           << " max=" << q_max[p]
           << " mean=" << (q_n[p] ? (double(q_sum[p]) / double(q_n[p])) : 0.0)
           << "\n";

      // Packet shape. all_n counts before the rejects and q_n after, so
      // sample_ratio should now read 1.0 -- SocketReader stamps every packet.
      // Anything above 1.0 is the rejects firing and wants explaining against
      // the rejects= line above; it read ~5 under the old 1-in-5. msgs_per_pkt
      // is the number that makes the qlen curve comparable across symbols:
      // qlen counts packets, so a book averaging 5 messages per packet pays
      // five decodes for every one unit of depth.
      for (int p = 0; p < NPOP; ++p)
        ss << "pkt_" << plabels[p]
           << " all_n=" << a_n[p]
           << " pkts=" << a_pkt_n[p]
           << " msgs_per_pkt="
           << (a_pkt_n[p] ? (double(a_n[p]) / double(a_pkt_n[p])) : 0.0)
           << " sample_ratio="
           << (q_n[p] ? (double(a_n[p]) / double(q_n[p])) : 0.0)
           << " idx_mean="
           << (q_n[p] ? (double(i_sum[p]) / double(q_n[p])) : 0.0)
           << " idx_max=" << i_max[p]
           << " span_sum=" << a_span_sum[p]
           << " decoded_per_pkt="
           << (a_pkt_n[p] ? (double(a_span_sum[p]) / double(a_pkt_n[p])) : 0.0)
           << " dedup_suppression>="
           << (a_span_sum[p] ? (1.0 - double(a_n[p]) / double(a_span_sum[p])) : 0.0)
           << " arr_recs=" << arr_written[p]
           << " arr_inline=" << arr_inline[p]
           // msg_recs must equal this population's l1_n -- same admission
           // rule, same point in sample(). A divergence means the per-message
           // log and the CSV are no longer the same population and nothing
           // may be joined between them.
           << " msg_recs=" << msg_written[p]
           << " msg_inline=" << msg_inline[p]
           << " msg_sat=" << msg_sat[p]
           << " mean_batch="
           << (a_closed_n[p] ? (double(a_batch_sum[p]) / double(a_closed_n[p])) : 0.0)
           << " mean_interarrival_ns="
           << (a_ia_n[p] ? (double(a_ia_sum[p]) / double(a_ia_n[p])) : 0.0)
           << " chan_other=" << a_chan_other[p]
           << " chan_share="
           << ((a_pkt_n[p] + a_chan_other[p])
                   ? (double(a_pkt_n[p]) / double(a_pkt_n[p] + a_chan_other[p]))
                   : 0.0)
           << "\n";

      static const char *labels[NSERIES] = {"book_leg1", "trade_leg1"};
      for (int s = 0; s < NSERIES; ++s)
        hist[s].dump(ss, labels[s]);

      reply(new frame::cons::msg::Page(ss.str()));
    }
  };

}
