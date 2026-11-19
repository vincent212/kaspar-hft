/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange/CoinbaseExchangeL2Sync.hpp"

#include <algorithm>
#include <cmath>

namespace coinbase_exchange
{

CoinbaseExchangeL2Sync::CoinbaseExchangeL2Sync(const ProductSpec &spec, sim::Mbp2Mbo &out, size_t depth)
    : spec_(spec), out_(out), window_(out, depth)
{
}

double CoinbaseExchangeL2Sync::px(int64_t ticks) const
{
  return double(ticks * spec_.price_increment) * std::pow(10.0, -spec_.price_decimals);
}

double CoinbaseExchangeL2Sync::qty(int64_t size) const
{
  return double(size) * std::pow(10.0, -spec_.size_decimals);
}

int64_t CoinbaseExchangeL2Sync::best(bool bid) const
{
  if (bid)
    return bids_.empty() ? 0 : bids_.rbegin()->first;
  return asks_.empty() ? 0 : asks_.begin()->first;
}

void CoinbaseExchangeL2Sync::on_message(const nlohmann::json &m, uint64_t recv_ns)
{
  if (!m.is_object())
    return;
  const auto type = m.value("type", std::string());
  if (type == "heartbeat")
  {
    // last_trade_id: the newest trade on the product when the heartbeat left.
    // Kept for the end-of-run check; the matches channel may still be behind.
    if (m.value("product_id", std::string()) == spec_.product)
      hb_last_trade_ = std::max(hb_last_trade_, m.value("last_trade_id", int64_t(-1)));
    return;
  }
  if (m.value("product_id", std::string()) != spec_.product)
    return;
  const uint64_t tx = m.contains("time") ? parse_iso_time(m.value("time", std::string())) : recv_ns;

  if (type == "snapshot" || type == "l2update")
  {
    const bool snap = type == "snapshot";
    if (!snap && !synced_)
      return;
    if (snap)
    {
      bids_.clear();
      asks_.clear();
      window_.clear();
    }
    auto set = [&](bool bid, const std::string &p, const std::string &q) {
      auto t = spec_.ticks(p);
      auto s = spec_.size(q);
      if (!t || !s)
      {
        ++st_.bad_message;
        return;
      }
      auto &book = bid ? bids_ : asks_;
      if (*s == 0)
        book.erase(*t);
      else
        book[*t] = *s;
      window_.set(bid, px(*t), qty(*s));
      ++st_.level_changes;
    };
    if (snap)
    {
      for (const auto &l : m.at("bids"))
        set(true, l[0].get<std::string>(), l[1].get<std::string>());
      for (const auto &l : m.at("asks"))
        set(false, l[0].get<std::string>(), l[1].get<std::string>());
      const uint64_t t = tx ? tx : recv_ns;
      if (!started_)
      {
        out_.start(t);
        started_ = true;
      }
      window_.resync(t);
      synced_ = true;
      ++st_.snapshots;
    }
    else
    {
      for (const auto &c : m.at("changes"))
        set(c[0].get<std::string>() == "buy", c[1].get<std::string>(), c[2].get<std::string>());
      window_.flush(tx);
      ++st_.updates;
      if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first)
        ++st_.crossed_updates;
    }
    out_.end_event();
    book_time_ = tx;
    book_recv_ = recv_ns;
    return;
  }

  if (type == "match" || type == "last_match")
  {
    int64_t id = m.value("trade_id", int64_t(-1));
    if (id >= 0)
    {
      if (last_trade_ >= 0 && id <= last_trade_)
      {
        ++st_.trade_dups;
        return;
      }
      if (last_trade_ >= 0 && id > last_trade_ + 1 && type == "match")
        st_.trades_missing += uint64_t(id - last_trade_ - 1);
      last_trade_ = id;
    }
    if (type == "last_match" || !synced_)
      return; // last_match is history from before the subscription
    auto t = spec_.ticks(m.value("price", std::string()));
    auto s = spec_.size(m.value("size", std::string()));
    if (!t || !s)
    {
      ++st_.bad_message;
      return;
    }
    // side is the maker's: "buy" means a resting bid was hit
    out_.trade(px(*t), qty(*s), m.value("side", std::string()) == "buy", tx);
    out_.end_event();
    ++st_.trades;
  }
}

}
