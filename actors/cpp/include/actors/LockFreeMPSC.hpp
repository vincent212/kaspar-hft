#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <tuple>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "actors/Queue.hpp"

namespace actors
{
  /**
   * LockFreeMPSC - a bounded, lock-free multi-producer / single-consumer mailbox
   * (Dmitry Vyukov's bounded MPMC ring, used here as MPSC).
   *
   * Push is PARK-FREE: a producer claims a slot with a CAS on enqueue_pos and
   * publishes it via a per-cell sequence number. Producers never hold a lock and
   * never sleep, so contention costs bounded cache-coherence latency (hundreds of
   * ns) instead of a microsecond futex park. That is the point: a flat tail.
   *
   * Consumer blocking (pop/pop_batch) uses a condvar + `parked` flag with
   * transition-notify, so producers touch the shared wakeup path ONLY when the
   * consumer is actually asleep (never, under load). A bounded wait_for() is a
   * safety net against a missed wakeup.
   *
   * Bounded: if the ring is full, push() spins until the consumer frees a slot.
   * Size the ring for peak backlog.
   */
  template <class T>
  class LockFreeMPSC : public Queue<T>
  {
    struct Cell {
      std::atomic<size_t> seq;
      T                   data;
    };

    static size_t round_up_pow2(size_t n) {
      size_t p = 1; while (p < n) p <<= 1; return p;
    }

    std::vector<Cell>          buf_;
    const size_t               mask_;
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};

    // consumer wakeup
    alignas(64) std::mutex          wait_mtx_;
    std::condition_variable         cv_;
    std::atomic<bool>               parked_{false};

  public:
    explicit LockFreeMPSC(size_t capacity = 1024)
      : buf_(round_up_pow2(capacity)), mask_(buf_.size() - 1)
    {
      for (size_t i = 0; i < buf_.size(); i++)
        buf_[i].seq.store(i, std::memory_order_relaxed);
    }

    // --- lock-free primitives ---
    bool try_push(const T& x) noexcept {
      Cell* cell;
      size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
      for (;;) {
        cell = &buf_[pos & mask_];
        size_t seq = cell->seq.load(std::memory_order_acquire);
        intptr_t dif = (intptr_t)seq - (intptr_t)pos;
        if (dif == 0) {
          if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
            break;
        } else if (dif < 0) {
          return false;  // full
        } else {
          pos = enqueue_pos_.load(std::memory_order_relaxed);
        }
      }
      cell->data = x;
      cell->seq.store(pos + 1, std::memory_order_release);
      return true;
    }

    bool try_pop(T& out) noexcept {
      Cell* cell;
      size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
      for (;;) {
        cell = &buf_[pos & mask_];
        size_t seq = cell->seq.load(std::memory_order_acquire);
        intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
        if (dif == 0) {
          if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
            break;
        } else if (dif < 0) {
          return false;  // empty
        } else {
          pos = dequeue_pos_.load(std::memory_order_relaxed);
        }
      }
      out = cell->data;
      cell->seq.store(pos + mask_ + 1, std::memory_order_release);
      return true;
    }

    // --- Queue<T> interface ---
    void push(const T& x) noexcept override {
      while (!try_push(x)) { /* ring full: spin until the consumer drains */ }
      if (parked_.load(std::memory_order_acquire)) {
        { std::lock_guard<std::mutex> lk(wait_mtx_); }
        cv_.notify_one();
      }
    }

    void pop_batch(std::vector<T>& out) override {
      out.clear();
      for (;;) {
        T v;
        while (try_pop(v)) out.push_back(v);
        if (!out.empty()) return;
        // empty: park with a bounded safety-net wait
        std::unique_lock<std::mutex> lk(wait_mtx_);
        parked_.store(true, std::memory_order_release);
        if (try_pop(v)) {                                   // re-check after arming
          parked_.store(false, std::memory_order_relaxed);
          out.push_back(v);
          while (try_pop(v)) out.push_back(v);
          return;
        }
        cv_.wait_for(lk, std::chrono::milliseconds(1));
        parked_.store(false, std::memory_order_relaxed);
      }
    }

    std::tuple<T, bool> pop() noexcept override {
      for (;;) {
        T v;
        if (try_pop(v)) return std::make_tuple(v, is_empty());
        std::unique_lock<std::mutex> lk(wait_mtx_);
        parked_.store(true, std::memory_order_release);
        if (try_pop(v)) { parked_.store(false, std::memory_order_relaxed); return std::make_tuple(v, is_empty()); }
        cv_.wait_for(lk, std::chrono::milliseconds(1));
        parked_.store(false, std::memory_order_relaxed);
      }
    }

    T peek() const noexcept override {
      size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
      Cell& cell = const_cast<Cell&>(buf_[pos & mask_]);
      size_t seq = cell.seq.load(std::memory_order_acquire);
      if ((intptr_t)seq - (intptr_t)(pos + 1) == 0) return cell.data;
      if constexpr (std::is_pointer<T>::value) return nullptr; else return T{};
    }

    bool is_empty() const noexcept override {
      return dequeue_pos_.load(std::memory_order_relaxed) ==
             enqueue_pos_.load(std::memory_order_relaxed);
    }

    std::size_t length() const noexcept override {
      size_t e = enqueue_pos_.load(std::memory_order_relaxed);
      size_t d = dequeue_pos_.load(std::memory_order_relaxed);
      return e > d ? e - d : 0;
    }
  };
}
