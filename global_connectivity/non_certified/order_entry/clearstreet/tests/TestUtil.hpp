/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Shared by the Clear Street OM tests: a recorder standing in for the SOM, a
// polling wait, and test equities in RefData (Fill looks its asset up).

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

namespace clearstreet::om::test
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
    // SOM replies in arrival order: "Ack 5", "Fill 5 30", "CancAck 5", ...
    std::vector<std::string> replies()
    {
      namespace som = frame::som::msg;
      std::lock_guard<std::mutex> l(mu);
      std::vector<std::string> v;
      for (auto m : msgs)
      {
        if (auto a = dynamic_cast<const som::Ack *>(m))
          v.push_back("Ack " + std::to_string(a->id));
        else if (auto f = dynamic_cast<const som::Fill *>(m))
          v.push_back("Fill " + std::to_string(f->id) + " " + std::to_string(int(f->sz)));
        else if (auto c = dynamic_cast<const som::CancAck *>(m))
          v.push_back("CancAck " + std::to_string(c->id));
        else if (auto r = dynamic_cast<const som::CancReject *>(m))
          v.push_back("CancReject " + std::to_string(r->id));
        else if (auto j = dynamic_cast<const som::Reject *>(m))
          v.push_back("Reject " + std::to_string(j->id));
      }
      return v;
    }

  private:
    std::mutex mu;
    std::vector<const actors::Message *> msgs;
  };

  // A US equity in RefData, priced in cents.
  inline const frame::ref::Asset *equity(const std::string &name)
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
    return frame::ref::RefData::add_future_asset(name, en::x::CLEARSTREET, 893000 + ++n, "CSXXXX", name, 0.01);
  }
}
