#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
  protected:
    // protected (not private) so BQueueBatched can add a whole-mailbox drain
    // that reuses this storage and lock.
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

    // Unlocked ring occupancy. See Queue::circ_buf_len for why this exists.
    //
    // Safe to read without the lock ONLY because boost::circular_buffer::size()
    // is `return m_size;` (circular_buffer/base.hpp:777) — one aligned load of
    // one member. A concurrent push/pop makes it stale by one, never garbage.
    //
    // overflow_ is deliberately NOT added in. std::deque::size() is
    // `_M_finish - _M_start` (stl_deque.h:1330), five pointer loads across two
    // iterators combined arithmetically; a concurrent mutation yields an
    // arbitrary result, and because the type is unsigned a transient negative
    // reads as an enormous number. So this counter saturates at the ring size:
    // if it reports cb_.capacity(), the true depth is >= that, possibly far
    // more. Size the ring (ACTOR_BQUEUE_SIZE) so that does not happen.
    std::size_t circ_buf_len() const noexcept override { return cb_.size(); }
  };
}
