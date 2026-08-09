#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <tuple>
#include <cstddef>
#include <vector>

namespace actors
{
  // Abstract base class for message queues
  template <class T>
  class Queue
  {
  public:
    Queue() = default;
    virtual ~Queue() = default;

    // Non-copyable
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    virtual std::tuple<T, bool> pop() = 0;
    virtual T peek() const = 0;
    virtual void push(const T& x) = 0;
    virtual bool is_empty() const = 0;
    virtual std::size_t length() const = 0;

    // Batch-blocking drain: block until >=1 item, then move all currently-queued
    // items into `out` (FIFO). Default loops pop() (correct but still one lock
    // per item); BQueue overrides it with a single-lock drain.
    virtual void pop_batch(std::vector<T>& out)
    {
      out.clear();
      for (;;) {
        auto r = pop();
        out.push_back(std::get<0>(r));
        if (std::get<1>(r)) return; // `last` == queue now empty
      }
    }
  };
}
