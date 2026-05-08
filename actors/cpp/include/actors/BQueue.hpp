#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <mutex>
#include <condition_variable>
#include <boost/circular_buffer.hpp>
#include <deque>
#include <tuple>
#include "actors/Queue.hpp"
#include <type_traits>

namespace actors
{
  /**
   * BQueue - Blocking Queue
   *
   * Uses condition variables for efficient waiting.
   * Low CPU usage when idle.
   */
  template <class T>
  class BQueue : public Queue<T>
  {
  private:
    mutable std::mutex mut;
    mutable std::condition_variable cv;
    boost::circular_buffer<T> cb_;
    std::deque<T> overflow_;

  public:
    explicit BQueue(size_t n) : cb_(n) {}

    std::tuple<T, bool> pop() noexcept override
    {
      std::unique_lock<std::mutex> lock(mut);
      cv.wait(lock, [this]() {
        return !cb_.empty() || !overflow_.empty();
      });

      T ret;
      if (!cb_.empty()) {
        ret = cb_.front();
        cb_.pop_front();
      } else {
        ret = overflow_.front();
        overflow_.pop_front();
      }
      bool last = cb_.empty() && overflow_.empty();
      return std::make_tuple(ret, last);
    }

    T peek() const noexcept override
    {
      std::lock_guard<std::mutex> lock(mut);
      if (cb_.empty() && overflow_.empty()) {
        if constexpr (std::is_pointer<T>::value)
          return nullptr;
        else
          return T{};
      }
      return !cb_.empty() ? cb_.front() : overflow_.front();
    }

    void push(const T& x) noexcept override
    {
      {
        std::lock_guard<std::mutex> lock(mut);
        if (!overflow_.empty() || cb_.full()) {
          overflow_.push_back(x);
        } else {
          cb_.push_back(x);
        }
      }
      cv.notify_one();
    }

    bool is_empty() const noexcept override
    {
      std::lock_guard<std::mutex> lock(mut);
      return cb_.empty() && overflow_.empty();
    }

    std::size_t length() const noexcept override
    {
      std::lock_guard<std::mutex> lock(mut);
      return cb_.size() + overflow_.size();
    }
  };
}
