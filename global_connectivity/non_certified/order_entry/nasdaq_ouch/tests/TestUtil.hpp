/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Shared by the OuchOM tests: a recorder that stands in for the SOM, a
// polling wait, and the test instruments.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "actors/Actor.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Reject.hpp"

namespace nasdaq::ouch::test
{
  using namespace std::chrono_literals;

  template <class P>
  bool wait_until(P p, std::chrono::milliseconds limit = std::chrono::seconds(5))
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (p())
        return true;
      std::this_thread::sleep_for(2ms);
    }
    return p();
  }

  // The SOM: records every message sent to it.
  class Recorder : public actors::Actor
  {
  public:
    explicit Recorder(const char *n) { strncpy(name, n, sizeof(name) - 1); }
    ~Recorder() override
    {
      for (auto m : msgs)
        delete m;
    }
    const char *get_name() const override { return name; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu);
      msgs.push_back(m);
    }
    template <class T>
    std::vector<const T *> all()
    {
      std::lock_guard<std::mutex> l(mu);
      std::vector<const T *> v;
      for (auto m : msgs)
        if (auto t = dynamic_cast<const T *>(m))
          v.push_back(t);
      return v;
    }
    template <class T>
    const T *wait_for(size_t nth = 0, std::chrono::milliseconds limit = std::chrono::seconds(5))
    {
      const T *r = nullptr;
      wait_until(
          [&] {
            auto v = all<T>();
            if (v.size() > nth)
            {
              r = v[nth];
              return true;
            }
            return false;
          },
          limit);
      return r;
    }
    size_t count()
    {
      std::lock_guard<std::mutex> l(mu);
      return msgs.size();
    }
    // The SOM replies in arrival order, by name, for checking sequences.
    std::vector<std::string> replies()
    {
      namespace som = frame::som::msg;
      std::lock_guard<std::mutex> l(mu);
      std::vector<std::string> v;
      for (auto m : msgs)
      {
        if (dynamic_cast<const som::Ack *>(m))
          v.push_back("Ack");
        else if (dynamic_cast<const som::Fill *>(m))
          v.push_back("Fill");
        else if (dynamic_cast<const som::CancAck *>(m))
          v.push_back("CancAck");
        else if (dynamic_cast<const som::CancReject *>(m))
          v.push_back("CancReject");
        else if (dynamic_cast<const som::Reject *>(m))
          v.push_back("Reject");
      }
      return v;
    }

  private:
    std::mutex mu;
    std::vector<const actors::Message *> msgs;
  };

  // A US stock with a one-cent tick, and one whose symbol does not fit OUCH.
  inline const frame::ref::Asset *test_stock(const std::string &name = "MSFT")
  {
    static std::once_flag once;
    std::call_once(once, [] {
      const char *k = std::getenv("KSPRPROJ");
      frame::ref::RefData::set_universe(std::string(k ? k : "") + "/unit_test/config/universe.csv", "");
      frame::ref::RefData::inst();
    });
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    static int n = 0;
    return frame::ref::RefData::add_future_asset(name, en::x::NASDAQ, 990000 + ++n, "ESXXXX", name, 0.01);
  }
}
