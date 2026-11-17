/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/CoinbaseSync.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <vector>

namespace sim
{

namespace
{
  double num(const nlohmann::json &v)
  {
    return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
  }
}

uint64_t CoinbaseSync::parse_time(const std::string &s)
{
  int Y, M, D, h, m, sec, n = 0;
  if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &Y, &M, &D, &h, &m, &sec, &n) != 6)
    return 0;
  std::tm tm{};
  tm.tm_year = Y - 1900;
  tm.tm_mon = M - 1;
  tm.tm_mday = D;
  tm.tm_hour = h;
  tm.tm_min = m;
  tm.tm_sec = sec;
  const int64_t epoch = int64_t(timegm(&tm));
  uint64_t frac = 0;
  int digits = 0;
  if (size_t(n) < s.size() && s[n] == '.')
    for (size_t i = n + 1; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
      if (digits < 9)
      {
        frac = frac * 10 + uint64_t(s[i] - '0');
        ++digits;
      }
  for (; digits < 9; ++digits)
    frac *= 10;
  return uint64_t(epoch) * 1000000000ull + frac;
}

bool CoinbaseSync::on_message(const nlohmann::json &msg)
{
  if (auto it = msg.find("sequence_num"); it != msg.end())
  {
    const int64_t seq = it->get<int64_t>();
    if (prev_seq_ >= 0 && seq != prev_seq_ + 1)
    {
      ++gaps_;
      std::cerr << "CoinbaseSync: sequence " << seq << " after " << prev_seq_
                << "; resubscribing" << std::endl;
      prev_seq_ = -1;
      synced_ = false;
      return false;
    }
    prev_seq_ = seq;
  }

  const auto channel = msg.value("channel", std::string());
  const auto events = msg.find("events");
  if (events == msg.end())
    return true;
  const uint64_t msg_tx = parse_time(msg.value("timestamp", std::string()));
  for (const auto &ev : *events)
  {
    if (channel == "l2_data" && ev.value("product_id", std::string()) == product_)
      on_book(ev, msg_tx);
    else if (channel == "market_trades")
      on_trades(ev);
  }
  return true;
}

void CoinbaseSync::on_book(const nlohmann::json &ev, uint64_t msg_tx)
{
  const auto type = ev.value("type", std::string());
  const bool snap = type == "snapshot";
  if (!snap && !synced_)
    return;   // updates before the snapshot: the snapshot will cover them

  // The event's time: the newest update in it, or the message's own stamp.
  uint64_t tx = 0;
  for (const auto &u : ev.at("updates"))
    tx = std::max(tx, parse_time(u.value("event_time", std::string())));
  if (!tx)
    tx = msg_tx;

  if (snap)
    window_.clear();
  for (const auto &u : ev.at("updates"))
    window_.set(u.at("side").get<std::string>() == "bid", num(u.at("price_level")),
                num(u.at("new_quantity")));

  if (snap)
  {
    if (!started_)
    {
      out_.start(tx);
      started_ = true;
    }
    window_.resync(tx);
    synced_ = true;
  }
  else
  {
    window_.flush(tx);
    ++applied_;
  }
  out_.end_event();
}

void CoinbaseSync::on_trades(const nlohmann::json &ev)
{
  if (!synced_ || ev.value("type", std::string()) != "update")
    return;
  std::vector<const nlohmann::json *> trades;
  for (const auto &t : ev.at("trades"))
    if (t.value("product_id", std::string()) == product_)
      trades.push_back(&t);
  std::sort(trades.begin(), trades.end(), [](const nlohmann::json *a, const nlohmann::json *b) {
    return std::stoll(a->at("trade_id").get<std::string>()) <
           std::stoll(b->at("trade_id").get<std::string>());
  });
  for (const auto *t : trades)
  {
    out_.trade(num(t->at("price")), num(t->at("size")), t->at("side").get<std::string>() == "BUY",
               parse_time(t->at("time").get<std::string>()));
    out_.end_event();
    ++trades_;
  }
}

}
