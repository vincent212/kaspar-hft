/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Recorder: stands in for the subscriber of TradeCapture messages. It keeps
// every message sent to it, under a lock, since the client may send from
// another thread.

#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "actors/Actor.hpp"

namespace nasdaq_post_trade::test
{
  class Recorder : public actors::Actor
  {
  public:
    explicit Recorder(const char *n) { std::strncpy(name_, n, sizeof(name_) - 1); }
    ~Recorder() override
    {
      for (auto m : msgs_)
        delete m;
    }
    const char *get_name() const override { return name_; }

    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu_);
      msgs_.push_back(m);
    }

    size_t count() const
    {
      std::lock_guard<std::mutex> l(mu_);
      return msgs_.size();
    }

    template <class T>
    std::vector<const T *> all() const
    {
      std::lock_guard<std::mutex> l(mu_);
      std::vector<const T *> v;
      for (auto m : msgs_)
        if (auto t = dynamic_cast<const T *>(m))
          v.push_back(t);
      return v;
    }

  private:
    char name_[64] = {};
    mutable std::mutex mu_;
    std::vector<const actors::Message *> msgs_;
  };

  template <class Pred>
  bool wait_until(Pred pred, std::chrono::milliseconds limit = std::chrono::seconds(10))
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (pred())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
  }
}
