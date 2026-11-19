/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// TimedRunner: callbacks at given times on one worker thread.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "frame/som/TimedRunner.hpp"

using frame::som::TimedRunner;
using namespace std::chrono_literals;

namespace
{
  template <typename F>
  bool wait_until(F f, std::chrono::milliseconds limit = 2s)
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (!f())
    {
      if (std::chrono::steady_clock::now() > end)
        return false;
      std::this_thread::sleep_for(1ms);
    }
    return true;
  }
} // namespace

// Callbacks run in time order; equal times in the order they were added.
TEST(TimedRunner, RunsInTimeOrder)
{
  std::mutex mu;
  std::vector<int> seen;
  auto note = [&](int i) { return [&, i] { std::lock_guard<std::mutex> l(mu); seen.push_back(i); }; };
  TimedRunner r;
  r.after(30ms, note(3));
  r.after(10ms, note(2));
  r.now(note(0));
  r.now(note(1));
  ASSERT_TRUE(wait_until([&] { std::lock_guard<std::mutex> l(mu); return seen.size() == 4; }));
  EXPECT_EQ(seen, (std::vector<int>{0, 1, 2, 3}));
}

// Destroying an idle runner returns at once, every time: the stop request
// cannot be lost between the worker's check and its wait.
TEST(TimedRunner, DestroyIdleDoesNotHang)
{
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 2000; ++i)
    TimedRunner r;
  EXPECT_LT(std::chrono::steady_clock::now() - start, 5s);
}

// Destroying a runner with a callback far in the future returns without
// waiting for it, and the callback never runs.
TEST(TimedRunner, DestroyWithPendingCallbackDoesNotWait)
{
  std::atomic<bool> ran{false};
  auto start = std::chrono::steady_clock::now();
  {
    TimedRunner r;
    r.after(1h, [&] { ran = true; });
  }
  EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
  EXPECT_FALSE(ran);
}

// A callback that throws is logged, and the runner goes on to the next one.
TEST(TimedRunner, ThrowingCallbackIsLoggedAndRunnerContinues)
{
  std::atomic<bool> ran{false};
  testing::internal::CaptureStderr();
  {
    TimedRunner r;
    r.now([] { throw std::runtime_error("boom"); });
    r.now([] { throw 42; });
    r.now([&] { ran = true; });
    EXPECT_TRUE(wait_until([&] { return ran.load(); }));
  }
  auto err = testing::internal::GetCapturedStderr();
  EXPECT_NE(err.find("callback threw: boom"), std::string::npos) << err;
  EXPECT_NE(err.find("non-std exception"), std::string::npos) << err;
}

// A callback that destroys its own runner aborts with a clear message.
TEST(TimedRunnerDeathTest, DestroyFromOwnCallbackAborts)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_DEATH(
      {
        auto *r = new TimedRunner;
        r->now([r] { delete r; });
        std::this_thread::sleep_for(2s);
      },
      "must not destroy its runner");
}
