/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/som/BrokerOrderBook.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <thread>

namespace frame::som
{
  // Milliseconds since EPOCH_UNIX_MS on the wall clock (0 before it).
  static uint64_t wall_ms()
  {
    using namespace std::chrono;
    auto unix_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return unix_ms > int64_t(ClientIdGen::EPOCH_UNIX_MS) ? uint64_t(unix_ms) - ClientIdGen::EPOCH_UNIX_MS : 0;
  }

  // The next slot no from_clock() generator in this process has had, at
  // least at_least, and never later than the wall clock: if it is still in
  // the future, wait for it. A later process therefore starts above it.
  uint64_t ClientIdGen::claim_slot(uint64_t at_least)
  {
    static std::atomic<uint64_t> last{0};
    uint64_t prev = last.load();
    for (;;)
    {
      uint64_t now = wall_ms();
      uint64_t slot = std::max({now, prev + 1, at_least});
      if (slot > now)
      {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        prev = last.load();
        continue;
      }
      if (last.compare_exchange_weak(prev, slot))
        return slot;
    }
  }

  ClientIdGen ClientIdGen::from_clock()
  {
    ClientIdGen g(claim_slot(0));
    g.from_clock_ = true;
    return g;
  }

  void ClientIdGen::next_slot()
  {
    ms_ = from_clock_ ? claim_slot(ms_ + 1) : ((ms_ + 1) & ((1ULL << MS_BITS) - 1));
    n_ = 0;
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

  // The state an order returns to when its cancel or replace fails.
  static State resting(const Entry &e)
  {
    return e.accepted ? State::Working : State::PendingNew;
  }

  // A cancel or replace report is about the request in flight if it carries
  // the request's id, or the order's own id (APIs that cancel by it). Any
  // other id is an earlier request's.
  static bool current_request(const Entry &e, ClientId cid)
  {
    return cid == e.pending_cid || cid == e.cid;
  }

  Result BrokerOrderBook::finish(Entry &e, State s)
  {
    // A replace still in flight: its confirmation, or fills up to its size,
    // may still arrive.
    e.done_replace_cid = e.state == State::PendingReplace ? e.pending_cid : 0;
    e.state = s;
    e.leaves = 0;
    e.pending_cid = 0;
    return {Outcome::Ok, &e, 0, true};
  }

  // The cancel or replace in flight is over without taking effect (rejected,
  // or refused by the API): the order goes back to how it was, or is done if
  // it was filled to its size meanwhile.
  Result BrokerOrderBook::abandon_request(Entry &e)
  {
    e.pending_cid = 0;
    e.state = resting(e);
    if (e.cum >= e.qty)
      return finish(e, State::Filled);
    e.leaves = e.qty - e.cum;
    return {Outcome::Ok, &e};
  }

  Result BrokerOrderBook::add(const frame::som::msg::Order &o, actors::Actor *sender)
  {
    auto it = by_oid_.find(o.oid);
    if (it != by_oid_.end())
      return {Outcome::Duplicate, &it->second};
    if (!(o.sz > 0) || o.sz > INT_MAX || std::floor(o.sz) != o.sz)
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
    if (!is_done(e->state) && cid == e->pending_cid)
      return abandon_request(*e);
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
    e->accepted = true;
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
    // happened, so it counts, but it cannot exceed what was ordered. While a
    // replace is in flight the venue may fill against either quantity, also
    // when a cancel finished the order before the replace was answered.
    bool replace_open = e->state == State::PendingReplace || e->done_replace_cid;
    int limit = replace_open ? std::max(e->qty, e->pending_qty) : e->qty;
    if (qty > limit - e->cum) // not e->cum + qty: that can overflow
      return {Outcome::Invalid, e, qty};
    e->cum += qty;
    e->accepted = true;
    if (e->state == State::PendingNew)
      e->state = State::Working;
    if (e->state == State::Canceled)
      return {Outcome::Ok, e, qty, false};
    if (e->state == State::PendingReplace && e->cum >= e->qty && e->pending_qty > e->cum)
    {
      // Filled to the old size during a replace up: what remains depends on
      // the replace, so leaves is what is left under the new size until it
      // is confirmed or rejected.
      e->leaves = e->pending_qty - e->cum;
      return {Outcome::Ok, e, qty, false};
    }
    if (e->state == State::PendingReplace && e->cum >= e->qty)
    {
      // Filled to the old size, and the replace target is no larger than
      // that: the venue filled the order before the replace, so it is done
      // whatever the replace answer is. A later confirmation of a size below
      // what was filled is reported Invalid by on_replaced.
      auto r = finish(*e, State::Filled);
      r.fill_qty = qty;
      return r;
    }
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
    // An unsolicited cancel comes on the order's current id, our cancel's ack
    // on the cancel's id. Any other id is one the order was replaced away
    // from, or an earlier cancel's: a late report, not about the live order.
    if (cid != e->cid && cid != e->pending_cid)
      return {Outcome::Late, e};
    return finish(*e, State::Canceled);
  }

  Result BrokerOrderBook::on_cancel_rejected(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state) || !current_request(*e, cid))
      return {Outcome::Late, e}; // done, or an earlier request's reject
    if (e->state != State::PendingCancel)
      return {Outcome::Invalid, e};
    return abandon_request(*e);
  }

  Result BrokerOrderBook::on_replaced(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state))
    {
      // Finished at the old size while this replace was in flight; the venue
      // now confirms a size below what was filled.
      if (e->state == State::Filled && e->done_replace_cid &&
          (cid == e->done_replace_cid || cid == e->cid) && e->pending_qty < e->cum)
      {
        e->done_replace_cid = 0; // reported once; a duplicate is Late
        return {Outcome::Invalid, e};
      }
      return {Outcome::Late, e};
    }
    if (!current_request(*e, cid))
      return {Outcome::Late, e}; // an earlier id: a duplicate or late report
    if (e->state != State::PendingReplace)
      return {Outcome::Invalid, e};
    // Fills that arrived while the replace was in flight are already in cum.
    e->cid = e->pending_cid;
    e->pending_cid = 0;
    e->qty = e->pending_qty;
    e->px = e->pending_px;
    e->accepted = true;
    e->leaves = e->qty - e->cum;
    e->state = State::Working;
    if (e->leaves == 0)
      return finish(*e, State::Filled);
    if (e->leaves < 0)
    {
      // Filled beyond the new size while the replace was in flight. The order
      // is done; Invalid tells the order manager to report the overfill.
      auto r = finish(*e, State::Filled);
      r.outcome = Outcome::Invalid;
      return r;
    }
    return {Outcome::Ok, e};
  }

  Result BrokerOrderBook::on_replace_rejected(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state) || !current_request(*e, cid))
      return {Outcome::Late, e}; // done, or an earlier request's reject
    if (e->state != State::PendingReplace)
      return {Outcome::Invalid, e};
    return abandon_request(*e);
  }

  Result BrokerOrderBook::on_rejected(ClientId cid)
  {
    Entry *e = lookup(cid);
    if (!e)
      return {Outcome::Unknown, nullptr};
    if (is_done(e->state))
      return {Outcome::Late, e};
    // A new order can be rejected until it is accepted, also after a cancel
    // or replace was sent for it. A reject on an accepted order, or on a
    // cancel's or replace's id, is a cancel/replace reject.
    if (e->accepted || cid != e->cid)
      return {Outcome::Invalid, e};
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
