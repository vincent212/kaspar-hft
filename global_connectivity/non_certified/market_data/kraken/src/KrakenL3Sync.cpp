/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_md/KrakenL3Sync.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "enum/e_names.hpp"
#include "kraken_md/KrakenJson.hpp"

namespace kraken
{
  using json = nlohmann::json;
  constexpr uint64_t kBudgetNs = 1'000'000'000ull; // a trade's budget lasts 1 s

  KrakenL3Sync::KrakenL3Sync(const Config &cfg, std::string symbol, Emit emit)
      : cfg_(cfg), symbol_(std::move(symbol)), emit_(std::move(emit))
  {
  }

  void KrakenL3Sync::reset()
  {
    synced_ = false;
    budget_.clear();
  }

  uint64_t KrakenL3Sync::kid(const std::string &id) const
  {
    auto it = index_.find(id);
    return it == index_.end() ? 0 : it->second.it->kid;
  }

  uint32_t KrakenL3Sync::units(int64_t qty_fp) const
  {
    if (qty_fp <= 0)
      return 0;
    const double u = std::round(fixed_to_double(qty_fp) / cfg_.qty_unit * 1e6) / 1e6;
    const double c = std::ceil(u);
    return c > 4294967295.0 ? 4294967295u : uint32_t(c);
  }

  uint32_t KrakenL3Sync::checksum() const
  {
    std::string s;
    int n = 0;
    for (auto it = asks_.begin(); it != asks_.end() && n < 10; ++it, ++n)
      for (const auto &o : it->second.q)
        s += checksum_token(o.px) + checksum_token(o.qty);
    n = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && n < 10; ++it, ++n)
      for (const auto &o : it->second.q)
        s += checksum_token(o.px) + checksum_token(o.qty);
    return crc32(s);
  }

  uint64_t KrakenL3Sync::stamp(uint64_t tx)
  {
    if (pending_.empty())
      event_tx_ = std::max(tx, last_tx_ + 1);
    return event_tx_;
  }

  void KrakenL3Sync::push_mbo(uint8_t action, bool bid, int64_t px, const Order &o, uint64_t tx, bool recovery)
  {
    bfile::l3_mbo_v2_t r{};
    r.typ = en::l3::MBO_V2;
    r.venue = cfg_.venue;
    r.transactTime = stamp(tx);
    r.sendingTime = r.transactTime;
    r.orderUpdateAction = action;
    r.securityID = cfg_.sec_id;
    r.orderID = o.kid;
    r.priority = o.kid;
    r.pxd = double(std::llround(fixed_to_double(px) / cfg_.tick));
    r.displayQty = action == 2 ? 0 : units(o.qty_fp);
    r.side = bid ? '0' : '1';
    r.recovery = recovery;
    pending_.emplace_back(r);
  }

  void KrakenL3Sync::push_exec(uint64_t kid, int64_t qty_fp, uint64_t tx)
  {
    bfile::l3_mbo_trd_v2_t r{};
    r.typ = en::l3::MBOT_V2;
    r.venue = cfg_.venue;
    r.transactTime = stamp(tx);
    r.sendingTime = r.transactTime;
    r.orderID = kid;
    r.lastQty = units(qty_fp);
    pending_.emplace_back(r);
    ++st_.executions;
  }

  void KrakenL3Sync::flush()
  {
    if (pending_.empty())
      return;
    auto &last = pending_.back();
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&last))
      m->endOfEvent = true;
    else if (auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&last))
      x->endOfEvent = true;
    else if (auto *p = std::get_if<bfile::l3_mbp_trd_t>(&last))
      p->endOfEvent = true;
    last_tx_ = event_tx_;
    for (const auto &r : pending_)
      emit_(r);
    pending_.clear();
    bfile::l3_eob_t eob{};
    eob.typ = en::l3::EOB;
    eob.venue = cfg_.venue;
    emit_(eob);
  }

  // The part of a reduction at (side, price) that a recent trade explains.
  int64_t KrakenL3Sync::consume(bool bid, int64_t px, int64_t reduction, uint64_t tx)
  {
    auto it = budget_.find({bid, px});
    if (it == budget_.end())
      return 0;
    if (tx > it->second.tx + kBudgetNs)
    {
      budget_.erase(it);
      return 0;
    }
    const int64_t x = std::min(reduction, it->second.qty_fp);
    it->second.qty_fp -= x;
    if (it->second.qty_fp <= 0)
      budget_.erase(it);
    return x;
  }

  void KrakenL3Sync::trade(const std::string &px, const std::string &qty, bool taker_buy, uint64_t tx)
  {
    int64_t pf, qf;
    if (!to_fixed(px, pf) || !to_fixed(qty, qf))
      return;
    ++st_.trades;
    bfile::l3_mbp_trd_t r{};
    r.typ = en::l3::MBPT;
    r.venue = cfg_.venue;
    r.transactTime = stamp(tx);
    r.sendingTime = r.transactTime;
    r.securityID = cfg_.sec_id;
    r.pxd = double(std::llround(fixed_to_double(pf) / cfg_.tick));
    r.aggresorSide = taker_buy ? 1 : 2;
    r.sz = int32_t(std::min<uint32_t>(units(qf), INT32_MAX));
    pending_.emplace_back(r);
    flush();
    // The resting side is the taker's opposite: a taker buy hit the asks.
    auto &b = budget_[{!taker_buy, pf}];
    b.qty_fp += qf;
    b.tx = tx;
  }

  void KrakenL3Sync::erase(const std::string &id, uint64_t tx, int64_t executed_fp)
  {
    auto w = index_.find(id);
    Side &side = w->second.bid ? bids_ : asks_;
    auto lvl = side.find(w->second.px);
    if (executed_fp > 0)
      push_exec(w->second.it->kid, executed_fp, tx);
    push_mbo(2, w->second.bid, w->second.px, *w->second.it, tx, false);
    lvl->second.q.erase(w->second.it);
    if (lvl->second.q.empty())
      side.erase(lvl);
    index_.erase(w);
  }

  void KrakenL3Sync::apply(bool bid, const json &e, bool snapshot)
  {
    const std::string id = e.at("order_id").get<std::string>();
    const std::string px = num_text(e.at("limit_price"));
    const std::string qty = num_text(e.at("order_qty"));
    int64_t pf, qf;
    if (!to_fixed(px, pf) || !to_fixed(qty, qf))
      throw std::invalid_argument("bad order " + id + " " + px + " " + qty);
    const uint64_t tx = event_tx_ ? event_tx_ : last_tx_;
    const std::string ev = snapshot ? std::string("add") : e.value("event", std::string());
    Side &side = bid ? bids_ : asks_;
    auto w = index_.find(id);

    if (ev == "add")
    {
      if (w != index_.end())
      {
        // Already live (an order re-sent as it comes back into scope while we
        // still hold it): treat as a modify.
        ++st_.duplicate_add;
      }
      else
      {
        ++st_.adds;
        auto &q = side[pf].q;
        q.push_back(Order{id, px, qty, qf, next_kid_++});
        index_[id] = Where{bid, pf, std::prev(q.end())};
        push_mbo(0, bid, pf, q.back(), tx, snapshot);
        return;
      }
    }
    if (ev == "delete")
    {
      if (w == index_.end())
      {
        ++st_.unknown_order;
        return;
      }
      ++st_.deletes;
      const int64_t x = consume(bid, w->second.px, w->second.it->qty_fp, tx);
      erase(id, tx, x);
      return;
    }
    if (ev == "modify" || ev == "add")
    {
      if (w == index_.end())
      {
        ++st_.unknown_order;
        return;
      }
      ++st_.modifies;
      Order &o = *w->second.it;
      const bool moved = pf != w->second.px || w->second.bid != bid;
      const bool up = qf > o.qty_fp;
      int64_t x = 0;
      if (!moved && !up)
        x = consume(bid, pf, o.qty_fp - qf, tx);
      if (x > 0)
        push_exec(o.kid, x, tx);
      if (moved || up)
      {
        ++st_.moved_to_back;
        Order copy = o;
        copy.px = px;
        copy.qty = qty;
        copy.qty_fp = qf;
        Side &old_side = w->second.bid ? bids_ : asks_;
        auto lvl = old_side.find(w->second.px);
        lvl->second.q.erase(w->second.it);
        if (lvl->second.q.empty())
          old_side.erase(lvl);
        auto &q = side[pf].q;
        q.push_back(copy);
        w->second = Where{bid, pf, std::prev(q.end())};
        push_mbo(1, bid, pf, q.back(), tx, false);
      }
      else
      {
        o.qty = qty;
        o.qty_fp = qf;
        push_mbo(1, bid, pf, o, tx, false);
      }
      return;
    }
    ++st_.errors;
  }

  void KrakenL3Sync::truncate(bool bid, uint64_t tx)
  {
    Side &side = bid ? bids_ : asks_;
    while (side.size() > cfg_.depth)
    {
      auto lvl = bid ? side.begin() : std::prev(side.end());
      std::vector<std::string> ids;
      for (const auto &o : lvl->second.q)
        ids.push_back(o.id);
      for (const auto &id : ids)
      {
        erase(id, tx, 0);
        ++st_.truncated;
      }
    }
  }

  KrakenL3Sync::Result KrakenL3Sync::on_data(const json &d, bool snapshot)
  {
    if (!snapshot && !synced_)
      return Result::Ok;
    uint64_t tx = parse_rfc3339(d.value("timestamp", std::string()));
    if (!tx)
      tx = last_tx_ + 1;
    stamp(tx);
    if (snapshot)
    {
      ++st_.snapshots;
      bids_.clear();
      asks_.clear();
      index_.clear();
      budget_.clear();
      if (!started_)
      {
        bfile::l3_sst_t r{};
        r.typ = en::l3::SST;
        r.venue = cfg_.venue;
        r.txtim = event_tx_;
        r.sendtim = r.txtim;
        r.securityID = cfg_.sec_id;
        r.tradingStatus = 17;
        pending_.emplace_back(r);
        flush();
        stamp(tx);
        started_ = true;
      }
    }
    else
      ++st_.updates;
    if (auto it = d.find("bids"); it != d.end())
      for (const auto &e : *it)
        apply(true, e, snapshot);
    if (auto it = d.find("asks"); it != d.end())
      for (const auto &e : *it)
        apply(false, e, snapshot);
    truncate(true, event_tx_);
    truncate(false, event_tx_);
    if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first)
      ++st_.crossed;
    flush();
    synced_ = true;
    auto ck = d.find("checksum");
    if (ck == d.end() || !ck->is_number())
      return Result::Ok;
    if (checksum() == ck->get<uint32_t>())
    {
      ++st_.checksum_ok;
      return Result::Ok;
    }
    ++st_.checksum_bad;
    synced_ = false;
    return Result::ChecksumMismatch;
  }

  KrakenL3Sync::Result KrakenL3Sync::on_message(const json &msg)
  {
    try
    {
      if (msg.value("channel", std::string()) != "level3")
        return Result::Ok;
      const bool snapshot = msg.value("type", std::string()) == "snapshot";
      Result r = Result::Ok;
      for (const auto &d : msg.at("data"))
        if (d.value("symbol", std::string()) == symbol_)
          if (auto x = on_data(d, snapshot); x != Result::Ok)
            r = x;
      return r;
    }
    catch (const std::exception &e)
    {
      ++st_.errors;
      pending_.clear();
      std::cerr << "KrakenL3Sync: " << e.what() << std::endl;
      synced_ = false;
      return Result::Error;
    }
  }
}
