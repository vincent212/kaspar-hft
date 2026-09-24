#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>

#include "actors/Actor.hpp"

// Passive HI-priority subscriber to one TachBook. Reads two stamps off each
// published payload and accumulates the two legs between them:
//
//   leg 1 = publish_ts - hndl_tim_epoch   handler done -> book published
//   leg 2 = observed   - publish_ts       book published -> subscriber runs
//
// Kept separate on purpose: leg 1 is a mailbox hop plus the whole book build,
// leg 2 is a mailbox hop plus scheduling delay. Book updates and trades are
// accumulated separately too -- they pass different admission filters upstream,
// so pooling them would average two populations.
//
// Counts are out of messages TachBook chose to PUBLISH, not out of messages
// that arrived. Six filters upstream (securityID, order-not-found, price
// bounds, recovery, fast_compare equal, baddata) are invisible from here.
//
//   ob         the TachBook to measure. Must outlive the probe.
//   sym        symbol id, recorded in the output header only.
//   tag        goes into the actor name. The name MUST keep the "perf"
//              substring -- TachBook::subscribe_handler admits HI-priority
//              subscribers only on that gate.
//   bin_ms     latency accumulation grid. SET THIS EQUAL TO QLen's period_ms:
//              the CSV is keyed by floor(observed_ns / bin_ms) and QLen's tick
//              snaps to the same wall-clock boundary, so the two join bin for
//              bin. Mismatch it and the join is silently wrong.
//   csv_path   "" => no file; lifetime histograms still available on the
//              console via `mdperf`.
//   flush_s    append completed bins every N seconds. 0 => only on shutdown.
//   max_bins   cap on bins held in memory; oldest are flushed and recycled.
//              300000 is 8h20m at 100 ms.
//
// The join it enables is bin-level, not per-message: mean latency over a bin
// against depth sampled once in that bin. Strong correlation is evidence that
// depth drives latency; a weak one is NOT evidence of absence, because short
// bursts relative to the bin attenuate the slope toward zero.
actor_ptr create_LatencyProbe(actor_ptr ob,
                              int sym,
                              const char *tag,
                              int bin_ms = 100,
                              const std::string &csv_path = "",
                              int flush_s = 60,
                              std::size_t max_bins = 300000);
