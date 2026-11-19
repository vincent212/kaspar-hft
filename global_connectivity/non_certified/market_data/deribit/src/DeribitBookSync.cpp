/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_md/DeribitBookSync.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace deribit
{

namespace
{
  double num(const nlohmann::json &v)
  {
    return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
  }
  constexpr uint64_t kMs = 1000000ull;
}

DeribitBookSync::DeribitBookSync(std::string instrument, double tick, sim::Mbp2Mbo *out,
                                 size_t depth)
    : instrument_(std::move(instrument)),
      book_prefix_("book." + instrument_ + "."),
      trades_prefix_("trades." + instrument_ + "."),
      tick_(tick),
      out_(out)
{
  if (out_)
    window_ = std::make_unique<sim::LevelWindow>(*out_, depth);
}

int64_t DeribitBookSync::ticks(double px) const
{
  return std::llround(px / tick_);
}

void DeribitBookSync::reset()
{
  invalidate();
  last_change_id_ = -1;
  hist_.clear();
}

void DeribitBookSync::invalidate()
{
  if (!synced_)
    return;
  synced_ = false;
  if (on_gap_)
    on_gap_();
}

bool DeribitBookSync::on_message(const nlohmann::json &msg)
{
  if (msg.value("method", std::string()) != "subscription")
    return true;
  const auto &params = msg.at("params");
  const auto channel = params.value("channel", std::string());
  const auto &data = params.at("data");
  if (channel.rfind(book_prefix_, 0) == 0)
  {
    // A notification after the first must chain onto the previous one.
    const auto prev = data.find("prev_change_id");
    if (synced_ && prev != data.end() && prev->get<int64_t>() != last_change_id_)
    {
      ++st_.gaps;
      std::cerr << "DeribitBookSync " << instrument_ << ": prev_change_id " << prev->get<int64_t>()
                << " after change_id " << last_change_id_ << "; resubscribing" << std::endl;
      reset();
      return false;
    }
    on_book(data);
  }
  else if (channel.rfind(trades_prefix_, 0) == 0)
    on_trades(data);
  return true;
}

void DeribitBookSync::apply_side(bool bid, const nlohmann::json &levels, bool snapshot)
{
  Side &side = bid ? bids_ : asks_;
  for (const auto &l : levels)
  {
    // [action, price, amount]
    const auto action = l.at(0).get<std::string>();
    const double px = num(l.at(1));
    const double amount = num(l.at(2));
    const int64_t t = ticks(px);
    const bool exists = side.count(t) != 0;
    ++st_.level_updates;
    if (action == "new")
    {
      if (exists && !snapshot)
        ++st_.bad_actions;
    }
    else if (action == "change")
    {
      if (!exists)
        ++st_.bad_actions;
    }
    else if (action == "delete")
    {
      if (!exists)
        ++st_.bad_actions;
    }
    else
    {
      ++st_.unknown_action;
      continue;
    }
    const double q = action == "delete" ? 0.0 : amount;
    if (q > 0)
      side[t] = q;
    else
      side.erase(t);
    if (window_)
      window_->set(bid, px, q);
  }
}

void DeribitBookSync::on_book(const nlohmann::json &data)
{
  const bool snapshot = data.find("prev_change_id") == data.end() ||
                        data.value("type", std::string()) == "snapshot";
  if (!snapshot && !synced_)
    return;   // a change before any snapshot: cannot happen on one subscription
  const uint64_t ts_ms = data.value("timestamp", uint64_t(0));
  const uint64_t tx = ts_ms * kMs;
  if (snapshot)
  {
    bids_.clear();
    asks_.clear();
    if (window_)
      window_->clear();
    ++st_.snapshots;
  }
  else
    ++st_.notifications;
  apply_side(true, data.at("bids"), snapshot);
  apply_side(false, data.at("asks"), snapshot);
  last_change_id_ = data.at("change_id").get<int64_t>();
  synced_ = true;

  if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first)
    ++st_.crossed;

  if (out_)
  {
    if (!started_)
    {
      out_->start(tx);
      started_ = true;
    }
    if (snapshot)
    {
      if (st_.snapshots > 1 && on_resync_)
        on_resync_(tx);
      window_->resync(tx);
    }
    else
      window_->flush(tx);
    out_->end_event();
  }
  remember(ts_ms);
}

void DeribitBookSync::on_trades(const nlohmann::json &data)
{
  std::vector<const nlohmann::json *> trades;
  for (const auto &t : data)
  {
    if (!synced_ || t.value("instrument_name", std::string()) != instrument_)
    {
      ++st_.trades_skipped;
      continue;
    }
    trades.push_back(&t);
  }
  std::sort(trades.begin(), trades.end(), [](const nlohmann::json *a, const nlohmann::json *b) {
    return a->at("trade_seq").get<int64_t>() < b->at("trade_seq").get<int64_t>();
  });
  for (const auto *t : trades)
  {
    ++st_.trades;
    if (!out_)
      continue;
    // direction is the taker's: a taker sell hit a resting bid, so the buyer
    // was the maker.
    const bool buyer_is_maker = t->at("direction").get<std::string>() == "sell";
    out_->trade(num(t->at("price")), num(t->at("amount")), buyer_is_maker,
                t->at("timestamp").get<uint64_t>() * kMs);
    out_->end_event();
  }
}

DeribitBookSync::Top DeribitBookSync::top(size_t n) const
{
  Top t;
  t.change_id = last_change_id_;
  for (auto it = bids_.rbegin(); it != bids_.rend() && t.bids.size() < n; ++it)
    t.bids.emplace_back(it->first, it->second);
  for (auto it = asks_.begin(); it != asks_.end() && t.asks.size() < n; ++it)
    t.asks.emplace_back(it->first, it->second);
  return t;
}

void DeribitBookSync::remember(uint64_t ts_ms)
{
  if (!hist_keep_)
    return;
  hist_.push_back(top(hist_levels_));
  hist_.back().ts_ms = ts_ms;
  while (hist_.size() > hist_keep_)
    hist_.pop_front();
}

const DeribitBookSync::Top *DeribitBookSync::at_change_id(int64_t id) const
{
  for (const auto &t : hist_)
    if (t.change_id == id)
      return &t;
  return nullptr;
}

std::pair<const DeribitBookSync::Top *, const DeribitBookSync::Top *>
DeribitBookSync::around(int64_t id) const
{
  const Top *before = nullptr, *after = nullptr;
  for (const auto &t : hist_)
  {
    if (t.change_id <= id)
      before = &t;
    else if (!after)
      after = &t;
  }
  return {before, after};
}

}
