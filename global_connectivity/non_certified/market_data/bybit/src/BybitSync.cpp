/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_md/BybitSync.hpp"

#include <cmath>
#include <iostream>
#include <utility>

#include "chutil/Assert.hpp"
#include "enum/e_names.hpp"

namespace bybit
{

namespace
{
  uint64_t ms_to_ns(uint64_t ms) { return ms * 1000000ull; }

  uint64_t as_u64(const nlohmann::json &v)
  {
    return v.is_string() ? std::stoull(v.get<std::string>()) : v.get<uint64_t>();
  }

  // Matching-engine time of a book message (cts), else the system time (ts).
  uint64_t book_ms(const nlohmann::json &msg, const nlohmann::json &data)
  {
    if (auto it = msg.find("cts"); it != msg.end() && !it->is_null())
      return as_u64(*it);
    if (auto it = data.find("cts"); it != data.end() && !it->is_null())
      return as_u64(*it);
    if (auto it = msg.find("ts"); it != msg.end() && !it->is_null())
      return as_u64(*it);
    if (auto it = data.find("ts"); it != data.end() && !it->is_null())
      return as_u64(*it);
    return 0;
  }

  bool ends_with(const std::string &s, const std::string &tail)
  {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
  }
}

double BybitSync::num(const nlohmann::json &v)
{
  return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
}

BybitSync::BybitSync(const Config &cfg, sim::Mbp2Mbo &out, Emit direct)
    : cfg_(cfg), out_(out), direct_(std::move(direct)), window_(out, cfg.window)
{
  ASSERT(cfg_.tick > 0, "BybitSync: tick must be positive");
  ASSERT(cfg_.print_unit > 0, "BybitSync: print_unit must be positive");
  ASSERT(!cfg_.symbol.empty(), "BybitSync: no symbol");
}

int64_t BybitSync::ticks(double px) const
{
  return std::llround(px / cfg_.tick);
}

void BybitSync::reset()
{
  // A lost connection: OB holds a book that is no longer updated until the
  // next snapshot. Tell it, as for a gap.
  if (synced_)
    emit_gap();
  buffer_.clear();
  synced_ = false;
  prev_u_ = -1;
  prev_seq_ = -1;
}

bool BybitSync::on_message(const nlohmann::json &msg)
{
  auto t = msg.find("topic");
  if (t == msg.end() || !t->is_string() || msg.find("data") == msg.end())
    return true;
  const std::string topic = t->get<std::string>();
  if (!ends_with(topic, "." + cfg_.symbol))
    return true;
  if (topic.rfind("orderbook.", 0) == 0)
  {
    if (topic.rfind("orderbook.rpi.", 0) == 0)
      return true;
    const auto &data = msg.at("data");
    if (cfg_.mode == Mode::DEPTH)
    {
      if (msg.value("type", std::string()) == "snapshot")
      {
        replace_book(data, ms_to_ns(book_ms(msg, data)));
        prev_u_ = data.at("u").get<int64_t>();
        prev_seq_ = data.value("seq", int64_t(-1));
        synced_ = true;
        after_book(prev_u_, true);
        return true;
      }
      if (!synced_)
        return true; // deltas before the first snapshot
      if (data.at("u").get<int64_t>() == 1 || !apply_delta(msg))
      {
        gap(msg, "u not previous + 1");
        return false;
      }
      return true;
    }
    on_book(msg);
    return true;
  }
  if (topic.rfind("publicTrade.", 0) == 0)
    on_trades(msg);
  return true;
}

// FULL mode.
void BybitSync::on_book(const nlohmann::json &msg)
{
  const auto &data = msg.at("data");
  const int64_t u = data.at("u").get<int64_t>();
  const int64_t seq = data.value("seq", int64_t(-1));

  if (synced_)
  {
    if (u == 1)
    {
      ++stats_.resets;
      std::cerr << "BybitSync " << cfg_.symbol << ": u=1 (reset); resynchronising" << std::endl;
      emit_gap();
      synced_ = false;
      buffer_.clear();
      buffer_.push_back(msg);
      return;
    }
    if (!apply_delta(msg))
      gap(msg, "u jumped");
    return;
  }

  // Not synchronised: buffer (Bybit's procedure, steps 1-2).
  if (!buffer_.empty())
  {
    const auto &last = buffer_.back().at("data");
    const int64_t lu = last.at("u").get<int64_t>();
    const int64_t lseq = last.value("seq", int64_t(-1));
    if (seq >= 0 && lseq >= 0 && seq < lseq)
    {
      ++stats_.seq_back;
      return;
    }
    if (u == lu)
    {
      ++stats_.duplicates; // the last delta delivered again: not a break
      return;
    }
    if (u != lu + 1)
      buffer_.clear(); // a break in u: restart buffering from here
  }
  buffer_.push_back(msg);
}

BybitSync::SnapResult BybitSync::on_snapshot(const nlohmann::json &result)
{
  if (cfg_.mode != Mode::FULL || synced_)
    return SnapResult::Ignored;

  const int64_t L = result.at("u").get<int64_t>();
  const int64_t S = result.value("seq", int64_t(-1));

  // Step 4: the snapshot must not be older than the oldest buffered delta,
  // and at equal u it must be the same book version (seq).
  if (!buffer_.empty())
  {
    const int64_t first_u = buffer_.front().at("data").at("u").get<int64_t>();
    if (first_u > L + 1)
    {
      ++stats_.too_old;
      return SnapResult::TooOld;
    }
    for (const auto &m : buffer_)
    {
      const auto &d = m.at("data");
      if (d.at("u").get<int64_t>() == L && S >= 0 && d.value("seq", int64_t(-1)) != S)
      {
        ++stats_.seq_mismatch;
        return SnapResult::SeqMismatch;
      }
    }
  }

  replace_book(result, ms_to_ns(book_ms(result, result)));
  prev_u_ = L;
  prev_seq_ = S;
  synced_ = true;
  after_book(L, true);

  std::deque<nlohmann::json> pending;
  pending.swap(buffer_);
  for (auto &m : pending)
  {
    if (!synced_)
    {
      buffer_.push_back(std::move(m)); // a gap below: keep for the next snapshot
      continue;
    }
    const int64_t u = m.at("data").at("u").get<int64_t>();
    if (u <= L)
      continue; // already in the snapshot
    if (!apply_delta(m))
      gap(m, "u jumped after snapshot");
  }
  return SnapResult::Applied;
}

bool BybitSync::apply_delta(const nlohmann::json &msg)
{
  const auto &data = msg.at("data");
  const int64_t u = data.at("u").get<int64_t>();
  const int64_t seq = data.value("seq", int64_t(-1));
  if (u <= prev_u_)
  {
    ++stats_.duplicates;
    return true;
  }
  if (u != prev_u_ + 1)
    return false;
  if (seq >= 0 && prev_seq_ >= 0 && seq < prev_seq_)
    ++stats_.seq_back; // contradicts u; counted, u governs
  last_cts_ms_ = book_ms(msg, data);
  apply_levels(data, false);
  window_.flush(ms_to_ns(last_cts_ms_));
  out_.end_event();
  prev_u_ = u;
  prev_seq_ = seq;
  ++stats_.deltas;
  after_book(u, false);
  return true;
}

void BybitSync::set_level(bool bid, double px, double qty)
{
  auto &side = bid ? bids_ : asks_;
  const int64_t t = ticks(px);
  if (before_level)
  {
    auto it = side.find(t);
    before_level(bid, t, it == side.end() ? 0.0 : it->second);
  }
  if (qty <= 0)
    side.erase(t);
  else
    side[t] = qty;
  window_.set(bid, px, qty);
}

void BybitSync::apply_levels(const nlohmann::json &data, bool)
{
  if (auto it = data.find("b"); it != data.end())
    for (const auto &l : *it)
      set_level(true, num(l[0]), num(l[1]));
  if (auto it = data.find("a"); it != data.end())
    for (const auto &l : *it)
      set_level(false, num(l[0]), num(l[1]));
}

void BybitSync::replace_book(const nlohmann::json &data, uint64_t tx)
{
  bids_.clear();
  asks_.clear();
  window_.clear();
  last_cts_ms_ = tx / 1000000ull;
  apply_levels(data, true);
  if (!started_)
  {
    out_.start(tx);
    started_ = true;
  }
  window_.resync(tx);
  out_.end_event();
  ++stats_.snapshots;
  if (bids_.empty() && asks_.empty())
  {
    // Delisting or before an auction: no levels, so Mbp2Mbo sent no
    // recovery records to clear OB with. Clear it explicitly.
    ++stats_.empty_resets;
    emit_clear();
  }
}

void BybitSync::after_book(int64_t u, bool snapshot)
{
  if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first)
    ++stats_.crossed;
  if (bids_.size() > stats_.max_levels_bid)
    stats_.max_levels_bid = bids_.size();
  if (asks_.size() > stats_.max_levels_ask)
    stats_.max_levels_ask = asks_.size();
  if (after_event)
    after_event(u, snapshot);
}

void BybitSync::on_trades(const nlohmann::json &msg)
{
  for (const auto &t : msg.at("data"))
  {
    if (!synced_)
    {
      ++stats_.trades_unsynced;
      continue;
    }
    const double px = num(t.at("p")), qty = num(t.at("v"));
    const bool taker_sell = t.at("S").get<std::string>() == "Sell";
    const uint64_t tx = ms_to_ns(as_u64(t.at("T")));

    bfile::l3_mbp_trd_t p{};
    p.typ = en::l3::MBPT;
    p.venue = cfg_.venue;
    p.transactTime = tx;
    p.sendingTime = tx;
    p.securityID = cfg_.sec_id;
    p.pxd = double(ticks(px));
    p.aggresorSide = taker_sell ? 2 : 1; // CME AggressorSide: 1 buy, 2 sell
    p.sz = int32_t(std::llround(qty / cfg_.print_unit));
    p.endOfEvent = true;
    p.lastTrade = true;
    if (direct_)
      direct_(p);

    if (t.value("RPI", false) || t.value("BT", false))
    {
      ++stats_.prints_only;
      continue;
    }
    // A taker sell hits the bid: the buyer was the maker.
    out_.trade(px, qty, taker_sell, tx);
    out_.end_event();
    ++stats_.trades;
  }
}

void BybitSync::gap(const nlohmann::json &msg, const char *why)
{
  ++stats_.gaps;
  std::cerr << "BybitSync " << cfg_.symbol << ": gap (" << why << ") at u="
            << msg.at("data").at("u") << " after u=" << prev_u_ << "; resynchronising" << std::endl;
  emit_gap();
  synced_ = false;
  buffer_.clear();
  if (cfg_.mode == Mode::FULL)
    buffer_.push_back(msg);
}

void BybitSync::emit_gap()
{
  if (!direct_)
    return;
  bfile::l3_gap_v2_t g{};
  g.typ = en::l3::GAP_V2;
  g.venue = cfg_.venue;
  direct_(g);
}

void BybitSync::emit_clear()
{
  if (!direct_)
    return;
  bfile::l3_chr_v2_t c{};
  c.typ = en::l3::CHR_V2;
  c.venue = cfg_.venue;
  direct_(c);
}

}
