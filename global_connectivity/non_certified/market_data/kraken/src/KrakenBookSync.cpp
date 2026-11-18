/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_md/KrakenBookSync.hpp"

#include <algorithm>
#include <iostream>

#include "kraken_md/KrakenJson.hpp"

namespace kraken
{
  using json = nlohmann::json;

  KrakenBookSync::KrakenBookSync(std::string symbol, size_t depth, sim::Mbp2Mbo *out, size_t window)
      : symbol_(std::move(symbol)), depth_(depth), out_(out)
  {
    if (out_)
      window_ = std::make_unique<sim::LevelWindow>(*out_, window ? window : depth);
  }

  void KrakenBookSync::reset()
  {
    synced_ = false;
    last_trade_id_ = 0;
  }

  uint32_t KrakenBookSync::checksum(const Side &bids, const Side &asks)
  {
    std::string s;
    s.reserve(800);
    int n = 0;
    for (auto it = asks.begin(); it != asks.end() && n < 10; ++it, ++n)
      s += checksum_token(it->second.px) + checksum_token(it->second.qty);
    n = 0;
    for (auto it = bids.rbegin(); it != bids.rend() && n < 10; ++it, ++n)
      s += checksum_token(it->second.px) + checksum_token(it->second.qty);
    return crc32(s);
  }

  uint32_t KrakenBookSync::checksum() const { return checksum(bids_, asks_); }

  std::vector<std::pair<int64_t, int64_t>> KrakenBookSync::top(bool bid, size_t n) const
  {
    std::vector<std::pair<int64_t, int64_t>> v;
    if (bid)
      for (auto it = bids_.rbegin(); it != bids_.rend() && v.size() < n; ++it)
        v.emplace_back(it->first, it->second.qty_fp);
    else
      for (auto it = asks_.begin(); it != asks_.end() && v.size() < n; ++it)
        v.emplace_back(it->first, it->second.qty_fp);
    return v;
  }

  void KrakenBookSync::set_level(bool bid, const json &e)
  {
    Side &side = bid ? bids_ : asks_;
    const std::string px = num_text(e.at("price"));
    const std::string qty = num_text(e.at("qty"));
    int64_t pf, qf;
    if (!to_fixed(px, pf) || !to_fixed(qty, qf))
      throw std::invalid_argument("bad level " + px + " " + qty);
    if (qf == 0)
      side.erase(pf);
    else
      side[pf] = Level{px, qty, qf};
    ++st_.levels_applied;
    if (window_)
      window_->set(bid, fixed_to_double(pf), fixed_to_double(qf));
  }

  // Drop the levels below the subscribed depth: Kraken stops updating them
  // and sends no qty 0 for them (book-checksum-v2, "Maintaining the book").
  void KrakenBookSync::truncate(bool bid)
  {
    Side &side = bid ? bids_ : asks_;
    while (side.size() > depth_)
    {
      auto it = bid ? side.begin() : std::prev(side.end()); // the worst level
      if (window_)
        window_->set(bid, fixed_to_double(it->first), 0);
      side.erase(it);
      ++st_.truncated;
    }
  }

  KrakenBookSync::Result KrakenBookSync::on_book(const json &d, bool snapshot)
  {
    if (!snapshot && !synced_)
      return Result::Ok; // an update before the snapshot: cannot happen on v2, ignored
    const uint64_t tx = parse_rfc3339(d.value("timestamp", std::string()));
    if (tx)
      last_book_tx_ = tx;
    if (snapshot)
    {
      bids_.clear();
      asks_.clear();
      if (window_)
        window_->clear();
      ++st_.snapshots;
    }
    else
      ++st_.updates;
    if (auto it = d.find("bids"); it != d.end())
      for (const auto &e : *it)
        set_level(true, e);
    if (auto it = d.find("asks"); it != d.end())
      for (const auto &e : *it)
        set_level(false, e);
    truncate(true);
    truncate(false);

    if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first)
      ++st_.crossed;

    if (out_)
    {
      const uint64_t t = last_book_tx_;
      if (snapshot)
      {
        if (!started_)
        {
          out_->start(t);
          started_ = true;
        }
        window_->resync(t);
      }
      else
        window_->flush(t);
      out_->end_event();
    }
    synced_ = true;

    auto ck = d.find("checksum");
    if (ck == d.end() || !ck->is_number())
    {
      ++st_.checksum_missing;
      return Result::Ok;
    }
    const uint32_t want = ck->get<uint32_t>();
    if (checksum() == want)
    {
      ++st_.checksum_ok;
      return Result::Ok;
    }
    ++st_.checksum_bad;
    synced_ = false;
    return Result::ChecksumMismatch;
  }

  void KrakenBookSync::on_trade(const json &t)
  {
    const uint64_t id = t.value("trade_id", uint64_t(0));
    if (!id)
      ++st_.trade_id_missing;
    else
    {
      if (last_trade_id_ && id != last_trade_id_ + 1)
      {
        if (id > last_trade_id_)
          ++st_.trade_id_gaps;
        else
          return; // already applied
      }
      last_trade_id_ = id;
    }
    if (!synced_)
    {
      ++st_.trades_before_sync;
      return;
    }
    ++st_.trades;
    if (out_)
    {
      const bool taker_buy = t.at("side").get<std::string>() == "buy";
      // Mbp2Mbo wants buyer_is_maker: a taker sell hit a resting bid.
      out_->trade(fixed_to_double(fixed(t.at("price"))), fixed_to_double(fixed(t.at("qty"))), !taker_buy,
                  parse_rfc3339(t.value("timestamp", std::string())));
      out_->end_event();
    }
  }

  KrakenBookSync::Result KrakenBookSync::on_message(const json &msg)
  {
    try
    {
      const auto ch = msg.find("channel");
      if (ch == msg.end() || !ch->is_string())
        return Result::Ok; // method responses (subscribe, pong)
      const std::string &channel = ch->get_ref<const std::string &>();
      if (channel == "heartbeat")
      {
        ++st_.heartbeats;
        return Result::Ok;
      }
      const std::string type = msg.value("type", std::string());
      const auto data = msg.find("data");
      if (data == msg.end() || !data->is_array())
        return Result::Ok;
      if (channel == "book")
      {
        Result r = Result::Ok;
        for (const auto &d : *data)
          if (d.value("symbol", std::string()) == symbol_)
          {
            Result x = on_book(d, type == "snapshot");
            if (x != Result::Ok)
              r = x;
          }
        return r;
      }
      if (channel == "trade")
      {
        if (type != "update")
          return Result::Ok; // the snapshot replays the last 50 trades
        std::vector<const json *> trades;
        for (const auto &t : *data)
          if (t.value("symbol", std::string()) == symbol_)
            trades.push_back(&t);
        std::stable_sort(trades.begin(), trades.end(), [](const json *a, const json *b) {
          return a->value("trade_id", uint64_t(0)) < b->value("trade_id", uint64_t(0));
        });
        for (const auto *t : trades)
          on_trade(*t);
      }
      return Result::Ok;
    }
    catch (const std::exception &e)
    {
      ++st_.errors;
      std::cerr << "KrakenBookSync: " << e.what() << std::endl;
      synced_ = false;
      return Result::Error;
    }
  }
}
