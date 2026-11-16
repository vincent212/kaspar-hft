/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/Mbp2Mbo.hpp"

#include <algorithm>
#include <cmath>

#include "chutil/Assert.hpp"
#include "enum/e_names.hpp"

namespace sim
{

Mbp2Mbo::Mbp2Mbo(const Config &cfg, Emit emit) : cfg_(cfg), emit_(std::move(emit))
{
  ASSERT(cfg_.tick > 0, "Mbp2Mbo: tick must be positive");
  ASSERT(cfg_.chunk > 0, "Mbp2Mbo: chunk must be positive");
  ASSERT(emit_, "Mbp2Mbo: no emit function");
}

int64_t Mbp2Mbo::ticks(double px) const
{
  return std::llround(px / cfg_.tick);
}

// The quantity is a decimal string on the wire, so 0.03 / 0.01 can come out as
// 2.9999999999999996. Round to the nearest millionth of a chunk first.
size_t Mbp2Mbo::target(double qty) const
{
  if (qty <= 0)
    return 0;
  const double c = std::round(qty / cfg_.chunk * 1e6) / 1e6;
  return size_t(std::ceil(c));
}

// All records of one event share its transactTime, and transactTime strictly
// increases from one event to the next: OB checks the book for a cross when
// the transactTime changes, so two events with one stamp would be checked as
// one, and an event stamped in the past is rejected after a recovery.
uint64_t Mbp2Mbo::stamp(uint64_t tx)
{
  if (pending_.empty())
    event_tx_ = std::max(tx, last_tx_ + 1);
  return event_tx_;
}

void Mbp2Mbo::push_mbo(uint8_t action, bool bid, int64_t t, uint64_t oid, uint64_t tx,
                       bool recovery)
{
  bfile::l3_mbo_v2_t r{};
  r.typ = en::l3::MBO_V2;
  r.venue = cfg_.venue;
  r.transactTime = tx;
  r.sendingTime = tx;
  r.orderUpdateAction = action;
  r.securityID = cfg_.sec_id;
  r.orderID = oid;
  r.priority = oid;
  r.pxd = double(t);
  r.displayQty = 1;
  r.side = bid ? '0' : '1';
  r.recovery = recovery;
  pending_.emplace_back(r);
}

void Mbp2Mbo::push_exec(uint64_t oid, uint64_t tx)
{
  bfile::l3_mbo_trd_v2_t r{};
  r.typ = en::l3::MBOT_V2;
  r.venue = cfg_.venue;
  r.transactTime = tx;
  r.sendingTime = tx;
  r.lastQty = 1;
  r.orderID = oid;
  pending_.emplace_back(r);
}

void Mbp2Mbo::start(uint64_t tx)
{
  bfile::l3_sst_t r{};
  r.typ = en::l3::SST;
  r.venue = cfg_.venue;
  r.txtim = stamp(tx);
  r.sendtim = r.txtim;
  r.securityID = cfg_.sec_id;
  r.tradingStatus = 17;   // ReadyToTrade: OB's only matching status
  pending_.emplace_back(r);
  end_event();
}

// Bring the level to `n` chunks: new ones join the back, surplus leaves from
// the back.
void Mbp2Mbo::set_level(Book &book, bool bid, int64_t t, size_t n, uint64_t tx,
                        bool recovery)
{
  auto it = book.find(t);
  if (it == book.end())
  {
    if (n == 0)
      return;
    it = book.emplace(t, std::deque<uint64_t>{}).first;
  }
  auto &q = it->second;
  while (q.size() > n)
  {
    push_mbo(2, bid, t, q.back(), tx);
    q.pop_back();
  }
  while (q.size() < n)
  {
    const uint64_t oid = next_oid_++;
    push_mbo(0, bid, t, oid, tx, recovery);
    q.push_back(oid);
  }
  if (q.empty())
    book.erase(it);
}

void Mbp2Mbo::level(bool bid, double px, double qty, uint64_t tx)
{
  set_level(bid ? bids_ : asks_, bid, ticks(px), target(qty), stamp(tx));
}

void Mbp2Mbo::trade(double px, double qty, bool buyer_is_maker, uint64_t tx)
{
  // buyer_is_maker: the buyer's order was resting, so the bid side was hit.
  const bool bid = buyer_is_maker;
  double &acc = bid ? acc_bid_ : acc_ask_;
  acc += qty;
  const double whole = std::floor(std::round(acc / cfg_.chunk * 1e6) / 1e6);
  if (whole < 1)
    return;
  acc -= whole * cfg_.chunk;
  if (acc < 0)
    acc = 0;

  auto &book = bid ? bids_ : asks_;
  auto it = book.find(ticks(px));
  if (it == book.end())
    return;   // nothing of ours at the print: no chunk to execute
  auto &q = it->second;
  const uint64_t t = stamp(tx);
  for (size_t i = 0; i < size_t(whole) && !q.empty(); ++i)
  {
    const uint64_t oid = q.front();
    q.pop_front();
    push_exec(oid, t);
    push_mbo(2, bid, it->first, oid, t);
  }
  if (q.empty())
    book.erase(it);
}

void Mbp2Mbo::snapshot(const std::vector<std::pair<double, double>> &bids,
                       const std::vector<std::pair<double, double>> &asks, uint64_t tx)
{
  bids_.clear();
  asks_.clear();
  acc_bid_ = acc_ask_ = 0;
  const uint64_t t = stamp(tx);
  for (const auto &[px, qty] : bids)
    set_level(bids_, true, ticks(px), target(qty), t, true);
  for (const auto &[px, qty] : asks)
    set_level(asks_, false, ticks(px), target(qty), t, true);
}

void Mbp2Mbo::end_event()
{
  if (pending_.empty())
    return;
  auto &last = pending_.back();
  if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&last))
    m->endOfEvent = true;
  else if (auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&last))
    x->endOfEvent = true;
  last_tx_ = event_tx_;
  for (const auto &r : pending_)
    emit_(r);
  pending_.clear();
  bfile::l3_eob_t eob{};
  eob.typ = en::l3::EOB;
  eob.venue = cfg_.venue;
  emit_(eob);
}

size_t Mbp2Mbo::chunks(bool bid, int64_t t) const
{
  const auto *q = queue(bid, t);
  return q ? q->size() : 0;
}

const std::deque<uint64_t> *Mbp2Mbo::queue(bool bid, int64_t t) const
{
  const auto &book = bid ? bids_ : asks_;
  auto it = book.find(t);
  return it == book.end() ? nullptr : &it->second;
}

int64_t Mbp2Mbo::best(bool bid) const
{
  if (bid)
    return bids_.empty() ? 0 : bids_.rbegin()->first;
  return asks_.empty() ? 0 : asks_.begin()->first;
}

}
