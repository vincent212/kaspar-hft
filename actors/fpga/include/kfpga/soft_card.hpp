#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * SoftCard - runs an FPGA design in software until there is a card.
 *
 *   start()/stop()  one thread per process, each spinning on its inputs, as
 *                   every process on the chip advances every cycle;
 *   run()           single-threaded: one step of every process in turn until
 *                   nothing moves; deterministic, for tests.
 *
 * It stands in for the card's hardware only. It routes nothing: processes read
 * their own FIFOs, windows and lanes directly.
 */

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "kfpga/ring.hpp"

namespace kfpga {

class SoftCard
{
  std::vector<std::function<bool()>> steps_;
  std::vector<std::thread> threads_;
  std::atomic<bool> stop_{false};

public:
  ~SoftCard() { stop(); }

  template <class P>
  void add(P &process)
  {
    steps_.push_back([&process] { return process.step(); });
  }

  void start()
  {
    stop_ = false;
    for (auto &s : steps_)
      threads_.emplace_back([this, &s] {
        while (!stop_.load(std::memory_order_relaxed))
          if (!s())
            cpu_relax();
      });
  }

  void stop()
  {
    stop_ = true;
    for (auto &t : threads_)
      t.join();
    threads_.clear();
  }

  // Step every process in turn until none moves. Returns the rounds taken.
  int run(int max_rounds = 1000000)
  {
    int r = 0;
    for (bool moved = true; moved && r < max_rounds; ++r)
    {
      moved = false;
      for (auto &s : steps_)
        moved |= s();
    }
    return r;
  }
};

} // namespace kfpga
