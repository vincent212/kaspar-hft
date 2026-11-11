#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstddef>
#include <vector>
#include "actors/LockFreeMPSC.hpp"

namespace actors
{
  /**
   * ExternalLane - an input to a mailbox that something other than a CPU actor
   * writes: a ring in host memory that an FPGA card writes into.
   *
   * poll() runs on the mailbox owner's thread. It delivers at most one message
   * (through Actor::deliver_external) and returns true if it did. It must never
   * block: the owner's thread checks its lanes and its CPU queue in turn.
   */
  struct ExternalLane
  {
    virtual ~ExternalLane() = default;
    virtual bool poll() noexcept = 0;
  };

  /**
   * FpgaLanes - a mailbox with two parts:
   *   - the ordinary LockFreeMPSC queue, which CPU actors push into as before;
   *   - one or more lanes, rings the card writes into.
   *
   * Only the owning thread reads it. Its loop (Actor::run_loop) checks the
   * lanes, then the CPU queue, one message at a time. There is no routing
   * thread: the card writes straight into the receiving thread's lanes.
   *
   * The consumer always spins: a sleeping thread can wait on a condition
   * variable (CPU senders) or on an interrupt (the card), not on both, so the
   * thread checks both on every pass. That costs a full core; put actors that
   * receive from the card in one Group to share one.
   */
  template <class T>
  class FpgaLanes : public LockFreeMPSC<T>
  {
    std::vector<ExternalLane *> lanes_;

  public:
    explicit FpgaLanes(size_t capacity = 1024) : LockFreeMPSC<T>(capacity, true) {}

    // Add lanes before the owning thread starts.
    void add_lane(ExternalLane *lane) { lanes_.push_back(lane); }
    const std::vector<ExternalLane *> &lanes() const noexcept { return lanes_; }
  };
}
