#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// ChainOB: the chain BybitSync -> LevelWindow -> Mbp2Mbo -> OB in one thread,
// without actors running, and a check of OB against the sync's book. Used by
// the verify and replay tools and the chain unit test. The OB is the real
// frame::ob::act::OB, driven through its handlers as unit_test/
// test_mbp2mbo_ob.cpp drives it; the asset is registered with units 1, since
// Mbp2Mbo sends prices in ticks.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include <boost/property_tree/ptree.hpp>

#include "bfile/r_l3.hpp"
#include "bybit_md/BybitSync.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

namespace bybit
{
  class ChainOB
  {
  public:
    struct OBCheck
    {
      uint64_t levels = 0;          // levels compared
      uint64_t size_mismatch = 0;   // OB size != ceil(qty / chunk)
      uint64_t mbp_mismatch = 0;    // Mbp2Mbo chunks != ceil(qty / chunk)
      bool bbo_ok = true;           // OB best bid and ask == the sync's
      std::string first_bad;
    };

    struct TestableOB : public frame::ob::act::OB
    {
      using frame::ob::act::OB::OB;
      using frame::ob::act::OB::reply_to;
    };

    // universe: a universe.csv for RefData (unit_test/config/universe.csv).
    ChainOB(const std::string &universe, const std::string &name, en::x venue, int32_t sec_id,
            int ladder_ticks)
    {
      static bool universe_set = false;
      if (!universe_set)
      {
        frame::ref::RefData::set_universe(universe, "");
        universe_set = true;
      }
      frame::ref::RefData::inst();
      auto a = frame::ref::RefData::add_future_asset(name, venue, sec_id, "", name, 1.0);
      if (!a)
        throw std::runtime_error("ChainOB: add_future_asset failed for " + name);
      a->maxpx = 1 << 30;
      sym_ = a->id;
      boost::property_tree::ptree pt;
      pt.put("ladder_ticks", ladder_ticks);
      ob_ = std::make_unique<TestableOB>(nullptr, true, nullptr, sym_, pt);
    }

    void feed(const bfile::l3_t &r)
    {
      frame::mda::msg::Data d(true);
      d.l3 = r;
      dispatch(&d);
      ++records_;
    }

    uint64_t records() const { return records_; }

    std::pair<int, int> bbo()
    {
      probe_.clear();
      frame::cons::msg::Get g("bbbo");
      dispatch(&g);
      auto page = probe_.get_message<frame::cons::msg::Page>(0);
      int b = -1, a = -1;
      if (page)
        std::sscanf(page->val.c_str(), "BBBO: %d %d", &b, &a);
      return {b, a};
    }

    // Real size at one level, 0 when empty.
    long qat(bool bid, int64_t t)
    {
      probe_.clear();
      frame::cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}});
      dispatch(&g);
      auto page = probe_.get_message<frame::cons::msg::Page>(0);
      long sz = -1, cnt = -1, sim = -1;
      if (page)
        std::sscanf(page->val.c_str(), "QAT: %ld %ld %ld", &sz, &cnt, &sim);
      return sz < 0 ? 0 : sz;
    }

    // The best `depth` levels per side of the sync's book against OB and
    // Mbp2Mbo. Call right after a book event: a trade takes chunks off a level
    // until the next book event restores them.
    OBCheck check(const BybitSync &sync, const sim::Mbp2Mbo &m, size_t depth)
    {
      OBCheck c;
      auto side = [&](bool bid) {
        const auto &book = bid ? sync.bids() : sync.asks();
        size_t n = 0;
        auto one = [&](int64_t t, double qty) {
          const long want = long(m.target(qty));
          const long got = qat(bid, t);
          const long mbp = long(m.chunks(bid, t));
          ++c.levels;
          if (got != want)
          {
            if (!c.size_mismatch && c.first_bad.empty())
              c.first_bad = std::string(bid ? "bid " : "ask ") + std::to_string(t) + " OB " +
                            std::to_string(got) + " want " + std::to_string(want);
            ++c.size_mismatch;
          }
          if (mbp != want)
            ++c.mbp_mismatch;
        };
        if (bid)
          for (auto it = book.rbegin(); it != book.rend() && n < depth; ++it, ++n)
            one(it->first, it->second);
        else
          for (auto it = book.begin(); it != book.end() && n < depth; ++it, ++n)
            one(it->first, it->second);
      };
      side(true);
      side(false);
      auto [b, a] = bbo();
      const int wb = sync.bids().empty() ? 0 : int(sync.bids().rbegin()->first);
      const int wa = sync.asks().empty() ? 0 : int(sync.asks().begin()->first);
      c.bbo_ok = (sync.bids().empty() || b == wb) && (sync.asks().empty() || a == wa);
      if (!c.bbo_ok && c.first_bad.empty())
        c.first_bad = "bbo OB " + std::to_string(b) + "/" + std::to_string(a) + " want " +
                      std::to_string(wb) + "/" + std::to_string(wa);
      return c;
    }

  private:
    unit_test::MockActor probe_{"probe"};
    std::unique_ptr<TestableOB> ob_;
    int sym_ = 0;
    uint64_t records_ = 0;

    void dispatch(const actors::Message *msg)
    {
      ob_->reply_to = &probe_;
      unit_test::TestHelper::invoke_handler(ob_.get(), msg, &probe_);
      ob_->reply_to = nullptr;
    }
  };
}
