/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "post_trade/TradeLedger.hpp"

namespace post_trade
{
  const char *to_string(Applied a)
  {
    switch (a)
    {
    case Applied::Added: return "Added";
    case Applied::Amended: return "Amended";
    case Applied::Removed: return "Removed";
    case Applied::Duplicate: return "Duplicate";
    case Applied::Stale: return "Stale";
    case Applied::Orphan: return "Orphan";
    case Applied::Invalid: return "Invalid";
    }
    return "?";
  }

  bool TradeLedger::ordered(const msg::TradeCapture &r, const State &s) const
  {
    return r.report_seq != 0 && s.seq != 0;
  }

  bool TradeLedger::newer(const msg::TradeCapture &r, const State &s) const
  {
    if (ordered(r, s))
      return r.report_seq > s.seq;
    return true; // no venue order: the later arrival wins
  }

  ApplyResult TradeLedger::apply(const msg::TradeCapture &r)
  {
    ApplyResult res;
    const std::string &key =
        (r.event != Event::New && !r.ref_trade_id.empty()) ? r.ref_trade_id : r.trade_id;
    if (key.empty() || r.report_id.empty())
      return res; // Invalid

    if (seen_.count(r.report_id))
    {
      res.what = Applied::Duplicate;
      return res;
    }

    const uint64_t arrival = ++arrivals_;
    auto it = trades_.find(key);

    switch (r.event)
    {
    case Event::New:
      if (it == trades_.end())
      {
        trades_.emplace(key, State{r, r.report_seq, arrival, true});
        res.what = Applied::Added;
        res.out = r;
      }
      else if (!it->second.live || !newer(r, it->second) ||
               (!ordered(r, it->second) && it->second.trade.event == Event::Amend))
        // Removed, or an amendment of it came first: with no venue order, a
        // New of a trade an amendment stands for is its late New.
        res.what = Applied::Stale;
      else
        return res; // Invalid: a second New for a live trade; report_id not recorded
      break;

    case Event::Amend:
      if (it == trades_.end())
      {
        // The amendment arrived before its trade: it carries the full
        // economics, so it stands for the trade; the New is Stale when it comes.
        trades_.emplace(key, State{r, r.report_seq, arrival, true});
        res.what = Applied::Added;
        res.out = r;
      }
      else if (!it->second.live || !newer(r, it->second))
        res.what = Applied::Stale;
      else
      {
        it->second.trade = r;
        it->second.seq = r.report_seq;
        it->second.arrival = arrival;
        res.what = Applied::Amended;
        res.out = r;
      }
      break;

    case Event::Cancel:
    case Event::Bust:
      if (it == trades_.end())
      {
        State tomb{r, r.report_seq, arrival, false};
        trades_.emplace(key, std::move(tomb));
        res.what = Applied::Orphan;
      }
      else if (!it->second.live)
        res.what = Applied::Stale; // already removed
      else
      {
        // Removal is final whatever its order relative to amendments.
        const msg::TradeCapture &was = it->second.trade;
        res.out = r;
        if (res.out.qty == 0) res.out.qty = was.qty;
        if (res.out.side == Side::Unknown) res.out.side = was.side;
        if (res.out.symbol.empty())
        {
          res.out.symbol = was.symbol;
          res.out.asset = was.asset;
        }
        if (res.out.price_mantissa == 0)
        {
          res.out.price_mantissa = was.price_mantissa;
          res.out.price_decimals = was.price_decimals;
        }
        if (res.out.ref_trade_id.empty()) res.out.ref_trade_id = key;
        it->second.trade = res.out;
        it->second.seq = r.report_seq;
        it->second.arrival = arrival;
        it->second.live = false;
        res.what = Applied::Removed;
      }
      break;
    }

    seen_.insert(r.report_id);
    return res;
  }

  const msg::TradeCapture *TradeLedger::find(const std::string &trade_id) const
  {
    auto it = trades_.find(trade_id);
    return it != trades_.end() && it->second.live ? &it->second.trade : nullptr;
  }

  bool TradeLedger::removed(const std::string &trade_id) const
  {
    auto it = trades_.find(trade_id);
    return it != trades_.end() && !it->second.live;
  }

  size_t TradeLedger::live() const
  {
    size_t n = 0;
    for (const auto &[k, s] : trades_)
      n += s.live;
    return n;
  }

  int64_t TradeLedger::net_qty(const std::string &symbol) const
  {
    int64_t n = 0;
    for (const auto &[k, s] : trades_)
      if (s.live && s.trade.symbol == symbol)
        n += sign(s.trade.side) * s.trade.qty;
    return n;
  }

  void TradeLedger::clear()
  {
    trades_.clear();
    seen_.clear();
    arrivals_ = 0;
  }
}
