#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <tuple>
#include <cstddef>

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
  };
}
