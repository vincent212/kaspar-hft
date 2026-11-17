/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/BinanceSync.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace sim
{

namespace
{
  // Binance sends prices and quantities as decimal strings.
  double num(const nlohmann::json &v)
  {
    return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
  }

  uint64_t ms_to_ns(const nlohmann::json &v) { return v.get<uint64_t>() * 1000000ull; }
}

void BinanceSync::on_message(const nlohmann::json &data)
{
  const auto e = data.value("e", std::string());
  if (e == "depthUpdate")
  {
    if (!synced_)
    {
      buffer_.push_back(data);
      return;
    }
    if (!apply_depth(data))
      gap(data);
  }
  else if (e == "trade" || e == "aggTrade")
  {
    if (synced_)
      apply_trade(data);
  }
}

void BinanceSync::on_snapshot(const nlohmann::json &snap, uint64_t now_ns)
{
  const int64_t L = snap.at("lastUpdateId").get<int64_t>();

  // Step 4 for the oldest buffered event that is not older than the snapshot:
  // if it starts after L, events between the snapshot and the stream are
  // missing and this snapshot is too old to use. Keep buffering and ask again.
  for (const auto &ev : buffer_)
  {
    const int64_t U = ev.at("U").get<int64_t>(), u = ev.at("u").get<int64_t>();
    const bool old = market_ == Market::SPOT ? u <= L : u < L;
    if (old)
      continue;
    const bool straddles = market_ == Market::SPOT ? U <= L + 1 : U <= L;
    if (!straddles)
    {
      need_snapshot_ = true;
      return;
    }
    break;
  }

  std::vector<std::pair<double, double>> bids, asks;
  for (const auto &l : snap.at("bids"))
    bids.emplace_back(num(l[0]), num(l[1]));
  for (const auto &l : snap.at("asks"))
    asks.emplace_back(num(l[0]), num(l[1]));

  if (!started_)
  {
    out_.start(now_ns);
    started_ = true;
  }
  out_.snapshot(bids, asks, now_ns);
  out_.end_event();

  last_update_id_ = L;
  prev_u_ = -1;
  need_snapshot_ = false;
  synced_ = true;

  std::deque<nlohmann::json> pending;
  pending.swap(buffer_);
  for (const auto &ev : pending)
  {
    if (!synced_)
    {
      buffer_.push_back(ev);   // a gap below: keep the rest for the next snapshot
      continue;
    }
    if (!apply_depth(ev))
      gap(ev);
  }
}

bool BinanceSync::apply_depth(const nlohmann::json &ev)
{
  const int64_t U = ev.at("U").get<int64_t>(), u = ev.at("u").get<int64_t>();
  const int64_t L = last_update_id_;
  const bool spot = market_ == Market::SPOT;

  if (spot ? u <= L : u < L)
    return true;   // older than the snapshot: already in it

  if (prev_u_ < 0)
  {
    if (spot ? !(U <= L + 1 && L + 1 <= u) : !(U <= L && L <= u))
      return false;
  }
  else if (spot ? U != prev_u_ + 1 : ev.at("pu").get<int64_t>() != prev_u_)
    return false;

  const uint64_t tx = ms_to_ns(ev.at("E"));
  for (const auto &l : ev.at("b"))
    out_.level(true, num(l[0]), num(l[1]), tx);
  for (const auto &l : ev.at("a"))
    out_.level(false, num(l[0]), num(l[1]), tx);
  out_.end_event();
  prev_u_ = u;
  ++applied_;
  return true;
}

void BinanceSync::apply_trade(const nlohmann::json &ev)
{
  out_.trade(num(ev.at("p")), num(ev.at("q")), ev.at("m").get<bool>(), ms_to_ns(ev.at("T")));
  out_.end_event();
  ++trades_;
}

void BinanceSync::gap(const nlohmann::json &ev)
{
  ++gaps_;
  std::cerr << "BinanceSync: gap at U=" << ev.at("U") << " u=" << ev.at("u")
            << " after u=" << prev_u_ << "; resynchronising" << std::endl;
  synced_ = false;
  need_snapshot_ = true;
  buffer_.clear();
  buffer_.push_back(ev);
}

}
