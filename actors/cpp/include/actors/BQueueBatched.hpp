#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <vector>
#include "actors/BQueue.hpp"

namespace actors
{
  /**
   * BQueueBatched - BQueue with a whole-mailbox batch drain.
   *
   * Same storage and blocking semantics as BQueue; adds pop_batch(), which
   * moves the ENTIRE mailbox into the caller's vector under ONE lock
   * acquisition. The actor run loop then processes the whole batch under a
   * single fast_send_mutex hold instead of one lock per message — N queued
   * messages cost one lock, not N.
   *
   * This is a distinct type (not a runtime flag on BQueue) so the mailbox
   * variant / run loop selects the batch-drain strategy by type at compile
   * time — no per-message branch, no virtual dispatch.
   *
   * Trade-off: because the run loop holds fast_send_mutex across the whole
   * batch, a fast_send to this actor waits for the batch to finish. Batching
   * buys throughput at the cost of fast_send latency — do not use it on an
   * actor whose point is inline low latency.
   */
  template <class T>
  class BQueueBatched : public BQueue<T>
  {
  public:
    explicit BQueueBatched(size_t n) : BQueue<T>(n) {}

    // Block until >=1 item, then move the whole queue (ring then overflow,
    // FIFO) into `out` in a single lock. `out` is cleared first.
    void pop_batch(std::vector<T>& out)
    {
      out.clear();
      std::unique_lock<std::mutex> lock(this->mut);
      this->cv.wait(lock, [this]() {
        return !this->cb_.empty() || !this->overflow_.empty();
      });
      out.reserve(this->cb_.size() + this->overflow_.size());
      while (!this->cb_.empty())       { out.push_back(this->cb_.front());       this->cb_.pop_front(); }
      while (!this->overflow_.empty()) { out.push_back(this->overflow_.front()); this->overflow_.pop_front(); }
    }
  };
}
