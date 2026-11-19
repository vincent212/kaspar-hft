/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "BookCheck.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>

#include <boost/property_tree/ptree.hpp>

#include "actors/msg/Shutdown.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ref/RefData.hpp"

namespace jnx_md::check
{
  namespace
  {
    // OB prints to std::cerr as it is built and shut down; keep it quiet.
    struct QuietCerr
    {
      std::ostringstream sink;
      std::streambuf *old;
      QuietCerr() : old(std::cerr.rdbuf(sink.rdbuf())) {}
      ~QuietCerr() { std::cerr.rdbuf(old); }
    };

    std::string page(frame::ob::act::OB &ob, const frame::cons::msg::Get &g)
    {
      auto r = ob.fast_send(g, nullptr);
      auto p = dynamic_cast<const frame::cons::msg::Page *>(r.get());
      return p ? p->val : std::string();
    }
  }

  ObSink::~ObSink() = default;

  frame::ob::act::OB *ObSink::create(int32_t asset)
  {
    boost::property_tree::ptree pt;
    QuietCerr q;
    auto ob = std::make_unique<frame::ob::act::OB>(nullptr, cross_check_, nullptr,
                                                   uint(asset), pt);
    auto *p = ob.get();
    obs_[asset] = std::move(ob);
    return p;
  }

  void ObSink::send(frame::ob::act::OB *ob, const bfile::l3_t &r)
  {
    frame::mda::msg::Data d(true);
    d.l3 = r;
    ob->fast_send(d, nullptr);
  }

  void ObSink::book(int32_t asset, const bfile::l3_t &r)
  {
    ++records;
    auto *ob = this->ob(asset);
    if (!ob)
    {
      // An OB is built on the instrument's first order: building one for each
      // of the ~4,000 books in the directory costs memory for nothing. Its
      // trading status is kept until then.
      if (std::holds_alternative<bfile::l3_sst_t>(r))
      {
        pending_status_[asset] = r;
        return;
      }
      if (!std::holds_alternative<bfile::l3_mbo_v2_t>(r))
        return;
      ob = create(asset);
      auto it = pending_status_.find(asset);
      if (it != pending_status_.end())
        send(ob, it->second);
    }
    touched_.insert(asset);
    send(ob, r);
  }

  void ObSink::all_books(const bfile::l3_t &r)
  {
    ++all_book_records;
    for (auto &[asset, ob] : obs_)
    {
      send(ob.get(), r);
      touched_.insert(asset);
    }
  }

  void ObSink::recorder(const bfile::l3_t &)
  {
    ++recorder_records;
  }

  uint64_t ObSink::shutdown_bad_prices()
  {
    uint64_t bad = 0;
    for (auto &[asset, ob] : obs_)
    {
      QuietCerr q;
      actors::msg::Shutdown s;
      ob->fast_send(s, nullptr);
      const std::string out = q.sink.str();
      const auto k = out.find("badpx: ");
      if (k != std::string::npos)
        bad += std::stoull(out.substr(k + 7));
    }
    return bad;
  }

  std::pair<int, int> bbbo(frame::ob::act::OB &ob)
  {
    int b = -1, a = -1;
    std::sscanf(page(ob, frame::cons::msg::Get("bbbo")).c_str(), "BBBO: %d %d", &b, &a);
    return {b, a};
  }

  Level qat(frame::ob::act::OB &ob, bool bid, int64_t px)
  {
    frame::cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(px)}});
    long sz = -1, cnt = -1, sim = -1;
    std::sscanf(page(ob, g).c_str(), "QAT: %ld %ld %ld", &sz, &cnt, &sim);
    return Level{px, sz, cnt};
  }

  std::vector<Level> ob_levels(frame::ob::act::OB &ob, bool bid, int maxpx, size_t n,
                               int max_ticks)
  {
    std::vector<Level> out;
    const auto [bb, ba] = bbbo(ob);
    int64_t px = bid ? bb : ba;
    if (bid ? px <= 0 : px >= maxpx - 1)
      return out;
    for (int k = 0; k < max_ticks && out.size() < n && px > 0 && px < maxpx; ++k)
    {
      Level l = qat(ob, bid, px);
      if (l.qty > 0 || l.count > 0)
        out.push_back(l);
      px += bid ? -1 : 1;
    }
    return out;
  }

  std::vector<Level> handler_levels(const Instrument &i, bool bid, size_t n)
  {
    std::vector<Level> out;
    auto take = [&](const auto &side) {
      for (const auto &[px, l] : side)
      {
        if (out.size() >= n)
          break;
        out.push_back(Level{int64_t(px / i.unit), long(l.qty), long(l.q.size())});
      }
    };
    if (bid)
      take(i.book.bids);
    else
      take(i.book.asks);
    return out;
  }

  namespace
  {
    std::string show(const std::vector<Level> &v)
    {
      std::ostringstream o;
      for (const auto &l : v)
        o << " " << l.px << ":" << l.qty << "/" << l.count;
      return o.str();
    }
  }

  void compare_ob(const Instrument &i, frame::ob::act::OB &ob, size_t n, int max_ticks,
                  ObCompare &out)
  {
    ++out.instruments;
    bool bad = false;
    // Best bid and ask: OB says 0 and maxpx - 1 for an empty side.
    const auto [bb, ba] = bbbo(ob);
    const int hb = i.book.bids.empty() ? 0 : int(i.book.best_bid() / i.unit);
    const int ha = i.book.asks.empty() ? i.maxpx - 1 : int(i.book.best_ask() / i.unit);
    // OB walks an emptied bid side down to its floor, price 1, and leaves
    // best_bid there: 0 or an empty 1 both mean no bids.
    const bool bid_empty_ok = hb == 0 && (bb == 0 || (bb == 1 && qat(ob, true, 1).count == 0));
    if ((bb != hb && !bid_empty_ok) || ba != ha)
    {
      ++out.bbo_mismatches;
      bad = true;
      if (out.examples.size() < 20)
        out.examples.push_back(i.sicc + "." + i.group + " bbbo OB " + std::to_string(bb) + "/" +
                               std::to_string(ba) + " handler " + std::to_string(hb) + "/" +
                               std::to_string(ha));
    }
    for (bool bid : {true, false})
    {
      const auto h = handler_levels(i, bid, n);
      // Walk OB far enough to reach the handler's n-th level.
      int span = max_ticks;
      if (!h.empty())
        span = std::max<int>(span, int(std::llabs(h.back().px - h.front().px)) + 1);
      const auto o = ob_levels(ob, bid, i.maxpx, n, span);
      out.levels += h.size();
      size_t same = 0;
      for (size_t k = 0; k < std::min(h.size(), o.size()); ++k)
        same += h[k] == o[k];
      if (h.size() != o.size() || same != h.size())
      {
        out.mismatched_levels += std::max(h.size(), o.size()) - same;
        bad = true;
        if (out.examples.size() < 20)
          out.examples.push_back(i.sicc + "." + i.group + (bid ? " bids" : " asks") +
                                 " handler" + show(h) + " | OB" + show(o));
      }
    }
    out.mismatched_instruments += bad;
  }

  HandlerCompare compare_handlers(const Handler &a, const Handler &b)
  {
    HandlerCompare out;
    const auto &ia = a.instruments().all();
    auto &ib = const_cast<Handler &>(b).instruments();
    auto note = [&](const std::string &s) {
      ++out.mismatches;
      if (out.examples.size() < 20)
        out.examples.push_back(s);
    };
    for (const auto &pa : ia)
    {
      ++out.instruments;
      const Instrument *pb = ib.find(pa->key);
      const std::string name = pa->sicc + "." + pa->group;
      if (!pb)
      {
        if (!pa->book.empty())
          note(name + ": missing in b");
        continue;
      }
      if (pa->status_sent != pb->status_sent)
        note(name + ": status " + std::to_string(pa->status_sent) + " vs " +
             std::to_string(pb->status_sent));
      auto side = [&](const auto &la, const auto &lb, const char *what) {
        if (la.size() != lb.size())
        {
          note(name + " " + what + ": " + std::to_string(la.size()) + " vs " +
               std::to_string(lb.size()) + " levels");
          return;
        }
        auto x = la.begin();
        auto y = lb.begin();
        for (; x != la.end(); ++x, ++y)
        {
          if (x->first != y->first || x->second.qty != y->second.qty ||
              x->second.q.size() != y->second.q.size())
          {
            note(name + " " + what + " level " + std::to_string(x->first) + " differs");
            return;
          }
          auto ox = x->second.q.begin();
          auto oy = y->second.q.begin();
          for (; ox != x->second.q.end(); ++ox, ++oy)
          {
            ++out.orders;
            if ((*ox)->number != (*oy)->number || (*ox)->qty != (*oy)->qty)
            {
              note(name + " " + what + " level " + std::to_string(x->first) +
                   ": queue differs at order " + std::to_string((*ox)->number));
              return;
            }
          }
        }
      };
      side(pa->book.bids, pb->book.bids, "bids");
      side(pa->book.asks, pb->book.asks, "asks");
    }
    return out;
  }
}
