/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_md/LmaxMdSync.hpp"

#include <algorithm>

#include "enum/e_names.hpp"
#include "lmax_fix/LmaxFix.hpp"

namespace lmax::md
{
  void LmaxMdSync::on_snapshot(const MdSnapshot &s, uint64_t tx)
  {
    if (s.security_id != inst_.security_id)
    {
      ++st_.wrong_instrument;
      return;
    }
    ++st_.snapshots;

    Levels bids, asks;
    std::set<TradeKey> trades;
    std::vector<const MdEntry *> trade_entries;
    for (const auto &e : s.entries)
    {
      int64_t steps;
      if (e.type == '0' || e.type == '1')
      {
        if (!e.has_px || !e.has_size || e.size <= 0 || !fix::to_steps(e.px, inst_.tick, steps) || steps <= 0)
        {
          ++st_.bad_entries;
          continue;
        }
        (e.type == '0' ? bids : asks).emplace_back(e.px, e.size);
      }
      else if (e.type == '2')
      {
        if (!e.has_px || !e.has_size || e.size <= 0)
        {
          ++st_.bad_entries;
          continue;
        }
        if (trades.emplace(e.px, e.size, e.date, e.time).second)
          trade_entries.push_back(&e);
      }
    }
    std::sort(bids.begin(), bids.end(), [](auto &a, auto &b) { return a.first > b.first; });
    std::sort(asks.begin(), asks.end(), [](auto &a, auto &b) { return a.first < b.first; });
    if (bids.empty() || asks.empty())
      ++st_.one_sided;
    else if (bids.front().first >= asks.front().first)
      ++st_.crossed;

    if (clear_pending_)
    {
      if (raw_)
      {
        bfile::l3_chr_v2_t c{};
        c.typ = en::l3::CHR_V2;
        c.venue = venue_;
        raw_(c);
      }
      clear_pending_ = false;
    }
    if (!started_)
    {
      out_.start(tx);
      started_ = true;
    }

    if (synced_)
    {
      // Trades first: classified against the book before this W.
      for (const MdEntry *e : trade_entries)
      {
        if (last_trades_.count(TradeKey(e->px, e->size, e->date, e->time)))
        {
          ++st_.trades_repeated;
          continue;
        }
        bool buyer_is_maker;
        if (!asks_.empty() && e->px >= asks_.front().first)
          buyer_is_maker = false; // a buy lifted the offer
        else if (!bids_.empty() && e->px <= bids_.front().first)
          buyer_is_maker = true;  // a sell hit the bid
        else
        {
          ++st_.trades_unclassified;
          continue;
        }
        out_.trade(e->px, e->size, buyer_is_maker, tx);
        ++st_.trades_applied;
      }
    }
    else
      st_.trades_first_book += trade_entries.size();

    window_.clear();
    for (const auto &[px, sz] : bids)
      window_.set(true, px, sz);
    for (const auto &[px, sz] : asks)
      window_.set(false, px, sz);
    if (!synced_)
    {
      window_.resync(tx);
      synced_ = true;
      ++st_.resyncs;
    }
    else
      window_.flush(tx);
    out_.end_event();

    bids_ = std::move(bids);
    asks_ = std::move(asks);
    last_trades_ = std::move(trades);
  }
}
