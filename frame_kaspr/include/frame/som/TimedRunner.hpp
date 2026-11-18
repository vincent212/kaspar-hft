/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// TimedRunner: runs callbacks at given times on one worker thread. Used by
// simulated brokers to play the broker's own thread: acks now, fills
// later, in time order.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

namespace frame::som
{
  class TimedRunner
  {
  public:
    using Clock = std::chrono::steady_clock;

    TimedRunner() : worker_([this](std::stop_token st) { run(st); }) {}
    ~TimedRunner()
    {
      {
        // Under the lock, so the worker cannot be between testing the stop
        // flag and blocking when the notify arrives.
        std::lock_guard<std::mutex> l(mu_);
        worker_.request_stop();
      }
      cv_.notify_all();
    }

    void after(std::chrono::milliseconds d, std::function<void()> f)
    {
      {
        std::lock_guard<std::mutex> l(mu_);
        q_.push({Clock::now() + d, seq_++, std::move(f)});
      }
      cv_.notify_all();
    }
    void now(std::function<void()> f) { after(std::chrono::milliseconds(0), std::move(f)); }

  private:
    struct Item
    {
      Clock::time_point at;
      uint64_t seq;
      std::function<void()> f;
      bool operator>(const Item &o) const { return at != o.at ? at > o.at : seq > o.seq; }
    };

    void run(std::stop_token st)
    {
      std::unique_lock<std::mutex> l(mu_);
      while (!st.stop_requested())
      {
        if (q_.empty())
        {
          cv_.wait(l, [&] { return st.stop_requested() || !q_.empty(); });
          continue;
        }
        auto at = q_.top().at;
        if (Clock::now() < at)
        {
          cv_.wait_until(l, at);
          continue;
        }
        auto f = std::move(const_cast<Item &>(q_.top()).f);
        q_.pop();
        l.unlock();
        f();
        l.lock();
      }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> q_;
    uint64_t seq_ = 0;
    std::jthread worker_; // last: starts after the members it uses
  };
}
