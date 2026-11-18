/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/som/BrokerOrderBook.hpp"

#include <chrono>

namespace frame::som
{
  ClientIdGen ClientIdGen::from_clock()
  {
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return ClientIdGen(uint64_t(ms) % MS_PER_DAY);
  }

  const char *to_string(State s)
  {
    switch (s)
    {
    case State::PendingNew: return "PendingNew";
    case State::Working: return "Working";
    case State::PendingCancel: return "PendingCancel";
    case State::PendingReplace: return "PendingReplace";
    case State::Filled: return "Filled";
    case State::Canceled: return "Canceled";
    case State::Rejected: return "Rejected";
    }
    return "?";
  }

  const char *to_string(Outcome o)
  {
    switch (o)
    {
    case Outcome::Ok: return "Ok";
    case Outcome::Unknown: return "Unknown";
    case Outcome::Duplicate: return "Duplicate";
    case Outcome::Late: return "Late";
    case Outcome::Busy: return "Busy";
    case Outcome::Invalid: return "Invalid";
    }
    return "?";
  }

  Entry *BrokerOrderBook::lookup(ClientId cid)
  {
    auto c = cid_oid_.find(cid);
    if (c == cid_oid_.end())
      return nullptr;
    auto e = by_oid_.find(c->second);
    return e == by_oid_.end() ? nullptr : &e->second;
  }

  Result BrokerOrderBook::finish(Entry &e, State s)
  {
    e.state = s;
    e.leaves = 0;
    e.pending_cid = 0;
    return {Outcome::Ok, &e, 0, true};
  }

  Result BrokerOrderBook::add(const frame::som::msg::Order &o, actors::Actor *sender)
  {
    auto it = by_oid_.find(o.oid);
    if (it != by_oid_.end())
      return {Outcome::Duplicate, &it->second};
    if (o.sz <= 0)
      return {Outcome::Invalid, nullptr};
    Entry &e = by_oid_[o.oid];
    e.oid = o.oid;
    e.sender = sender;
    e.order = o;
    e.cid = gen_.next();
    e.qty = int(o.sz);
    e.leaves = e.qty;
    e.px = o.px;
    cid_oid_[e.cid] = e.oid;
    return {Outcome::Ok, &e};
  }

  Result BrokerOrderBook::request_cancel(int oid)
  {
    auto it = by_oid_.find(oid);
    if (it == by_oid_.end())
      return {Outcome::Unknown, nullptr};
    Entry &e = it->second;
    if (is_done(e.state))
      return {Outcome::Late, &e};
    if (e.pending_cid)
      return {Outcome::Busy, &e};
    e.pending_cid = gen_.next();
    cid_oid_[e.pending_cid] = e.oid;
    e.state = State::PendingCancel;
    return {Outcome::Ok, &e};
  }

  Result BrokerOrderBook::request_replace(int oid, int new_qty, int new_px)
  {
    auto it = by_oid_.find(oid);
    if (it == by_oid_.end())
      return {Outcome::Unknown, nullptr};
    Entry &e = it->second;
    if (is_done(e.state))
      return {Outcome::Late, &e};
    if (e.pending_cid)
      return {Outcome::Busy, &e};
    if (new_qty <= e.cum)
      return {Outcome::Invalid, &e}; // nothing would be left to work
    e.pending_cid = gen_.next();
    cid_oid_[e.pending_cid] = e.oid;
    e.pending_qty = new_qty;
    e.pending_px = new_px;
    e.state = State::PendingReplace;
    return {Outcome::Ok, &e};
  }

  Result BrokerOrderBook::send_failed(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (cid == e->cid && e->state == State::PendingNew)
      return finish(*e, State::Rejected);
    if (cid == e->pending_cid)
    {
      e->pending_cid = 0;
      e->state = e->exch_oid || e->cum ? State::Working : State::PendingNew;
      return {Outcome::Ok, e};
    }
    return {Outcome::Late, e};
  }

  Result BrokerOrderBook::on_accepted(ClientId cid, uint64_t exch_oid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state))
      return {Outcome::Late, e};
    if (exch_oid)
      e->exch_oid = exch_oid;
    if (e->state == State::PendingNew)
      e->state = State::Working;
    return {Outcome::Ok, e};
  }

  Result BrokerOrderBook::on_fill(ClientId cid, int qty)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr, qty};
    if (e->state == State::Rejected || e->state == State::Filled)
      return {Outcome::Late, e, qty};
    if (qty <= 0)
      return {Outcome::Invalid, e, qty};
    // A fill that races a cancel ack can arrive after Canceled; it still
    // happened, so it counts, but it cannot exceed what was ordered.
    if (e->cum + qty > e->qty)
      return {Outcome::Invalid, e, qty};
    e->cum += qty;
    if (e->state == State::PendingNew)
      e->state = State::Working;
    if (e->state == State::Canceled)
      return {Outcome::Ok, e, qty, false};
    e->leaves = e->qty - e->cum;
    if (e->leaves == 0)
    {
      auto r = finish(*e, State::Filled);
      r.fill_qty = qty;
      return r;
    }
    return {Outcome::Ok, e, qty};
  }

  Result BrokerOrderBook::on_canceled(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state))
      return {Outcome::Late, e};
    return finish(*e, State::Canceled);
  }

  Result BrokerOrderBook::on_cancel_rejected(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (e->state != State::PendingCancel)
      return {is_done(e->state) ? Outcome::Late : Outcome::Invalid, e};
    e->pending_cid = 0;
    e->state = State::Working;
    return {Outcome::Ok, e};
  }

  Result BrokerOrderBook::on_replaced(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state))
      return {Outcome::Late, e};
    if (e->state != State::PendingReplace)
      return {Outcome::Invalid, e};
    // Fills that arrived while the replace was in flight are already in cum.
    e->cid = e->pending_cid;
    e->pending_cid = 0;
    e->qty = e->pending_qty;
    e->px = e->pending_px;
    e->leaves = e->qty - e->cum;
    e->state = State::Working;
    if (e->leaves <= 0)
      return finish(*e, State::Filled);
    return {Outcome::Ok, e};
  }

  Result BrokerOrderBook::on_replace_rejected(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (e->state != State::PendingReplace)
      return {is_done(e->state) ? Outcome::Late : Outcome::Invalid, e};
    e->pending_cid = 0;
    e->state = State::Working;
    return {Outcome::Ok, e};
  }

  Result BrokerOrderBook::on_rejected(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state))
      return {Outcome::Late, e};
    if (e->state != State::PendingNew)
      return {Outcome::Invalid, e}; // a reject for a live order is a cancel/replace reject
    return finish(*e, State::Rejected);
  }

  const Entry *BrokerOrderBook::find_oid(int oid) const
  {
    auto it = by_oid_.find(oid);
    return it == by_oid_.end() ? nullptr : &it->second;
  }

  const Entry *BrokerOrderBook::find_cid(ClientId cid) const
  {
    auto c = cid_oid_.find(cid);
    return c == cid_oid_.end() ? nullptr : find_oid(c->second);
  }

  size_t BrokerOrderBook::live() const
  {
    size_t n = 0;
    for (auto &[oid, e] : by_oid_)
      n += !is_done(e.state);
    return n;
  }

  void BrokerOrderBook::purge_done()
  {
    for (auto c = cid_oid_.begin(); c != cid_oid_.end();)
    {
      auto e = by_oid_.find(c->second);
      if (e != by_oid_.end() && is_done(e->second.state))
        c = cid_oid_.erase(c);
      else
        ++c;
    }
    std::erase_if(by_oid_, [](auto &kv) { return is_done(kv.second.state); });
  }
}
