#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>
#include <chrono>
#include <tuple>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include "actors/Queue.hpp"

namespace actors
{
  /**
   * ShardedBQueue - a mailbox split into N independent lanes to reduce
   * producer-side lock contention (and therefore tail-latency jitter) when many
   * threads send to one actor.
   *
   *   push : round-robin across lanes (a shared atomic cursor picks the lane), so
   *          P producers spread over N locks instead of hammering one.
   *   pull : the single consumer drains round-robin (one item per lane per
   *          rotation), reconstructing round-robin arrival order.
   *
   * Trade-offs vs BQueue (choose per actor):
   *  - Reduces push contention ~N x -> lower jitter with many producers.
   *  - Does NOT preserve per-sender FIFO (round-robin spreads a sender across
   *    lanes). Use BQueue where per-sender order or whole-mailbox batch drain
   *    matters and contention is low.
   *
   * Consumer wakeup: a single condvar + `parked` flag with transition-notify, so
   * producers touch the shared wakeup path ONLY when the consumer is actually
   * asleep (i.e. never, under load). A bounded wait (`wait_for`) is a safety net:
   * even if a wakeup were ever missed, the consumer re-checks within the timeout
   * rather than hanging.
   */
  template <class T>
  class ShardedBQueue : public Queue<T>
  {
    struct alignas(64) Lane {
      std::mutex     mtx;
      std::deque<T>  dq;
    };

    std::vector<Lane>          lanes_;
    const size_t               n_;
    std::atomic<uint64_t>      push_cursor_{0};
    size_t                     pull_cursor_ = 0;  // consumer-only

    // shared consumer wakeup
    std::mutex                 wait_mtx_;
    std::condition_variable    cv_;
    std::atomic<bool>          parked_{false};

    bool any_nonempty() {
      for (size_t i = 0; i < n_; i++) {
        std::lock_guard<std::mutex> lk(lanes_[i].mtx);
        if (!lanes_[i].dq.empty()) return true;
      }
      return false;
    }

  public:
    explicit ShardedBQueue(size_t lanes = 8)
      : lanes_(lanes ? lanes : 1), n_(lanes ? lanes : 1) {}

    void push(const T& x) noexcept override {
      // Shared round-robin cursor: the atomic fetch_add serializes producers just
      // enough to guarantee consecutive pushes land on DIFFERENT lanes, which
      // minimizes lane-lock collisions and keeps the TAIL short (the whole point
      // of a sharded mailbox). It costs some median (contended atomic line) but
      // that cost is bounded, so the distribution stays flat.
      size_t lane = (size_t)(push_cursor_.fetch_add(1, std::memory_order_relaxed) % n_);
      {
        std::lock_guard<std::mutex> lk(lanes_[lane].mtx);
        lanes_[lane].dq.push_back(x);
      }
      // transition-notify: only pay the shared path if the consumer is parked.
      // No full fence on the hot path — the consumer's bounded wait_for() is the
      // safety net, so a rarely-missed wakeup is healed within the timeout, never
      // lost. Under load the consumer never parks, so this is a cheap acquire load.
      if (parked_.load(std::memory_order_acquire)) {
        { std::lock_guard<std::mutex> lk(wait_mtx_); }       // serialize vs consumer entering wait()
        cv_.notify_one();
      }
    }

    // Drain everything currently queued into `out`, in round-robin order.
    // Blocks until at least one item is available.
    void pop_batch(std::vector<T>& out) override {
      out.clear();
      for (;;) {
        // drain: rotate over lanes popping one each, until a full pass is empty
        for (;;) {
          bool progressed = false;
          for (size_t j = 0; j < n_; j++) {
            Lane& L = lanes_[(pull_cursor_ + j) % n_];
            std::lock_guard<std::mutex> lk(L.mtx);
            if (!L.dq.empty()) { out.push_back(L.dq.front()); L.dq.pop_front(); progressed = true; }
          }
          if (!progressed) break;
        }
        if (!out.empty()) return;

        // all lanes empty: park until a producer signals (bounded as a safety net)
        std::unique_lock<std::mutex> lk(wait_mtx_);
        parked_.store(true, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);  // StoreLoad
        if (any_nonempty()) { parked_.store(false, std::memory_order_relaxed); continue; }
        cv_.wait_for(lk, std::chrono::milliseconds(1));
        parked_.store(false, std::memory_order_relaxed);
      }
    }

    // Single-item pop (round-robin). `last` == queue now empty. Blocks until an
    // item is available.
    std::tuple<T, bool> pop() noexcept override {
      for (;;) {
        for (size_t j = 0; j < n_; j++) {
          Lane& L = lanes_[(pull_cursor_ + j) % n_];
          std::unique_lock<std::mutex> lk(L.mtx);
          if (!L.dq.empty()) {
            T v = L.dq.front();
            L.dq.pop_front();
            pull_cursor_ = (pull_cursor_ + j + 1) % n_;  // advance past the lane we took from
            lk.unlock();
            return std::make_tuple(v, !any_nonempty());
          }
        }
        std::unique_lock<std::mutex> lk(wait_mtx_);
        parked_.store(true, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (any_nonempty()) { parked_.store(false, std::memory_order_relaxed); continue; }
        cv_.wait_for(lk, std::chrono::milliseconds(1));
        parked_.store(false, std::memory_order_relaxed);
      }
    }

    T peek() const noexcept override {
      for (size_t j = 0; j < n_; j++) {
        auto& L = const_cast<Lane&>(lanes_[(pull_cursor_ + j) % n_]);
        std::lock_guard<std::mutex> lk(L.mtx);
        if (!L.dq.empty()) return L.dq.front();
      }
      if constexpr (std::is_pointer<T>::value) return nullptr; else return T{};
    }

    bool is_empty() const noexcept override {
      for (size_t i = 0; i < n_; i++) {
        auto& L = const_cast<Lane&>(lanes_[i]);
        std::lock_guard<std::mutex> lk(L.mtx);
        if (!L.dq.empty()) return false;
      }
      return true;
    }

    std::size_t length() const noexcept override {
      std::size_t total = 0;
      for (size_t i = 0; i < n_; i++) {
        auto& L = const_cast<Lane&>(lanes_[i]);
        std::lock_guard<std::mutex> lk(L.mtx);
        total += L.dq.size();
      }
      return total;
    }
  };
}
