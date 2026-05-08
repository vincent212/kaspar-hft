#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <thread>
#include <chrono>
#include <functional>
#include "actors/msg/Timeout.hpp"
#include "actors/Actor.hpp"

namespace actors::act
{
  /**
   * Timer - Simple timer utility for actors
   *
   * Usage:
   *   Timer::wake_up_in(my_actor, 5, 0);  // Wake up in 5 seconds
   *   Timer::wake_up_at(my_actor, 1000);  // Wake up at next 1-second boundary
   */
  class Timer
  {
  public:
    /// Wake up actor after specified delay
    static void wake_up_in(Actor* subscriber, int seconds, int msecs = 0, int data = 0)
    {
      std::thread([=]() {
        sleep(seconds, msecs);
        subscriber->send(new actors::msg::Timeout(data), nullptr);
      }).detach();
    }

    /// Wake up actor at next interval boundary (e.g., every 1000ms)
    static void wake_up_at(Actor* subscriber, int interval_ms, int data = 0)
    {
      using namespace std::chrono;
      auto now = system_clock::now();
      auto today = floor<days>(now);
      auto time_since_midnight = duration_cast<milliseconds>(now - today);
      auto curr_ms = time_since_midnight.count();
      auto rounded_down = curr_ms - (curr_ms % interval_ms);
      auto next_timeout = rounded_down + interval_ms;
      auto time_to_wait = next_timeout - curr_ms;

      std::thread([=]() {
        sleep(0, int(time_to_wait));
        subscriber->send(new actors::msg::Timeout(data), nullptr);
      }).detach();
    }

    static void sleep(int seconds, int msecs = 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(seconds * 1000 + msecs));
    }

  private:
    Timer() = delete;
  };
}
