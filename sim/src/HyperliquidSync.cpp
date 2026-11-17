/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/HyperliquidSync.hpp"

namespace sim
{

namespace
{
  double num(const nlohmann::json &v)
  {
    return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
  }

  uint64_t ms_to_ns(const nlohmann::json &v) { return v.get<uint64_t>() * 1000000ull; }
}

void HyperliquidSync::on_message(const nlohmann::json &msg)
{
  const auto channel = msg.value("channel", std::string());
  const auto data = msg.find("data");
  if (data == msg.end())
    return;
  if (channel == "l2Book")
    on_book(*data);
  else if (channel == "trades")
    on_trades(*data);
}

void HyperliquidSync::on_book(const nlohmann::json &data)
{
  if (data.value("coin", std::string()) != coin_)
    return;
  const uint64_t tx = ms_to_ns(data.at("time"));
  const auto &levels = data.at("levels");
  window_.clear();
  for (const auto &l : levels.at(0))
    window_.set(true, num(l.at("px")), num(l.at("sz")));
  for (const auto &l : levels.at(1))
    window_.set(false, num(l.at("px")), num(l.at("sz")));

  if (!synced_)
  {
    if (!started_)
    {
      out_.start(tx);
      started_ = true;
    }
    first_book_ns_ = tx;
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

void HyperliquidSync::on_trades(const nlohmann::json &data)
{
  if (!synced_)
    return;
  for (const auto &t : data)
  {
    if (t.value("coin", std::string()) != coin_)
      continue;
    const uint64_t tx = ms_to_ns(t.at("time"));
    if (tx < first_book_ns_)
      continue;
    if (t.contains("tid"))
    {
      const uint64_t tid = t.at("tid").get<uint64_t>();
      if (!seen_tids_.insert(tid).second)
        continue;
      seen_order_.push_back(tid);
      if (seen_order_.size() > kSeen)
      {
        seen_tids_.erase(seen_order_.front());
        seen_order_.pop_front();
      }
    }
    out_.trade(num(t.at("px")), num(t.at("sz")), t.at("side").get<std::string>() == "A", tx);
    out_.end_event();
    ++trades_;
  }
}

}
