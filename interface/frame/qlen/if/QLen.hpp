#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"

// Periodic per-actor mailbox-depth sampler. Writes one l3_qlen_t per actor
// per tick to `binrec`, so the samples share an L3 stream with the market
// data and can be joined on t0.
//
//   period_ms  sampling grid in ms; 100 => 10 Hz.
//   ctx_every  scrape /proc for context switches every Nth tick only.
//              The /proc pass costs one open+read+close per thread and is
//              three orders of magnitude dearer than the depth walk.
//   prefixes   record only actors whose name starts with one of these.
//              Empty => all. At 10 Hz every actor recorded costs the
//              recorder 10 messages/sec.
//
// This is a sampled gauge, not per-message queue depth: bursts that fill and
// drain a mailbox between ticks are invisible. The `msg` field is cumulative,
// so differencing it across ticks does give a true messages/sec rate.
actor_ptr create_QLen(actors::Manager *man,
                      actor_ptr binrec,
                      int period_ms = 100,
                      int ctx_every = 10,
                      const std::vector<std::string> &prefixes = {});
