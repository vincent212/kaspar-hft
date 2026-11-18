/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_md/BookCheck.hpp"

#include <cmath>
#include <sstream>

namespace lmax::md
{
  void BookCheck::level(bool bid, int64_t t, int64_t d)
  {
    auto &side = bid ? bids_ : asks_;
    auto &n = side[t];
    n = uint64_t(int64_t(n) + d);
    if (n == 0)
      side.erase(t);
  }

  void BookCheck::on_record(const bfile::l3_t &rec)
  {
    if (auto *r = std::get_if<bfile::l3_mbo_v2_t>(&rec))
    {
      if (r->securityID != sec_id_)
        return;
      if (r->recovery && !in_recovery_)
      {
        orders_.clear();
        bids_.clear();
        asks_.clear();
      }
      in_recovery_ = r->recovery;
      const bool bid = r->side == '0';
      const int64_t t = std::llround(r->pxd);
      switch (r->orderUpdateAction)
      {
      case 0:
        if (!orders_.emplace(r->orderID, Ord{bid, t, r->displayQty}).second)
        {
          ++errors_;
          return;
        }
        level(bid, t, r->displayQty);
        break;
      case 1:
      {
        auto it = orders_.find(r->orderID);
        if (it == orders_.end())
        {
          ++errors_;
          return;
        }
        level(it->second.bid, it->second.ticks, -int64_t(it->second.qty));
        it->second = Ord{bid, t, r->displayQty};
        level(bid, t, r->displayQty);
        break;
      }
      case 2:
      {
        auto it = orders_.find(r->orderID);
        if (it == orders_.end())
        {
          ++errors_;
          return;
        }
        level(it->second.bid, it->second.ticks, -int64_t(it->second.qty));
        orders_.erase(it);
        break;
      }
      default:
        ++errors_;
      }
    }
    else if (std::get_if<bfile::l3_mbo_trd_v2_t>(&rec))
      ++execs_;
    else if (std::get_if<bfile::l3_chr_v2_t>(&rec))
    {
      // OB clears the book; it stays in recovery if it was (OB.cpp).
      orders_.clear();
      bids_.clear();
      asks_.clear();
    }
  }

  size_t BookCheck::compare(const std::vector<std::pair<double, double>> &bids,
                            const std::vector<std::pair<double, double>> &asks, size_t depth,
                            std::string &why) const
  {
    size_t bad = 0;
    std::ostringstream w;
    auto side = [&](const std::vector<std::pair<double, double>> &lv, const std::map<int64_t, uint64_t> &book,
                    const char *name) {
      std::map<int64_t, uint64_t> want;
      for (size_t i = 0; i < lv.size() && i < depth; ++i)
      {
        const int64_t t = std::llround(lv[i].first / tick_);
        want[t] = uint64_t(std::ceil(lv[i].second / chunk_ - 1e-9));
      }
      for (auto &[t, n] : want)
      {
        auto it = book.find(t);
        uint64_t have = it == book.end() ? 0 : it->second;
        if (have != n && ++bad <= 3)
          w << name << " " << t << ": " << have << " chunks, LMAX " << n << "; ";
      }
      for (auto &[t, n] : book)
        if (!want.count(t) && ++bad <= 3)
          w << name << " " << t << ": " << n << " chunks not in LMAX's book; ";
    };
    side(bids, bids_, "bid");
    side(asks, asks_, "ask");
    why = w.str();
    return bad;
  }

  int64_t BookCheck::best(bool bid) const
  {
    if (bid)
      return bids_.empty() ? 0 : bids_.rbegin()->first;
    return asks_.empty() ? 0 : asks_.begin()->first;
  }

  bool BookCheck::crossed() const
  {
    return !bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first;
  }
}
