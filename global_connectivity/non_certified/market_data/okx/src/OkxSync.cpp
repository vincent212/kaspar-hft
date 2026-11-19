/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_md/OkxSync.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace okx::md
{

namespace
{
  double num(const nlohmann::json &v)
  {
    return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
  }

  int64_t integer(const nlohmann::json &v)
  {
    return v.is_string() ? std::stoll(v.get<std::string>()) : v.get<int64_t>();
  }

  uint64_t ms_to_ns(const nlohmann::json &v) { return uint64_t(integer(v)) * 1000000ull; }

  std::array<uint32_t, 256> make_table()
  {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i)
    {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }
}

int32_t OkxSync::crc32(std::string_view s)
{
  static const auto table = make_table();
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s)
    c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
  return int32_t(c ^ 0xFFFFFFFFu);
}

OkxSync::OkxSync(std::string inst_id, sim::Mbp2Mbo &out, size_t window_depth)
    : inst_(std::move(inst_id)), out_(out), window_(out, window_depth)
{
}

void OkxSync::reset()
{
  invalidate();
  seq_ = -1;
  last_trade_id_ = -1;
}

void OkxSync::invalidate()
{
  if (!synced_)
    return;
  synced_ = false;
  if (on_gap_)
    on_gap_();
}

std::string OkxSync::checksum_string(size_t levels) const
{
  std::string s;
  auto b = bids_.rbegin();
  auto a = asks_.begin();
  for (size_t i = 0; i < levels; ++i)
  {
    if (b != bids_.rend())
    {
      if (!s.empty())
        s += ':';
      s += b->second.px + ':' + b->second.sz;
      ++b;
    }
    if (a != asks_.end())
    {
      if (!s.empty())
        s += ':';
      s += a->second.px + ':' + a->second.sz;
      ++a;
    }
  }
  return s;
}

OkxSync::Status OkxSync::on_message(const nlohmann::json &msg)
{
  if (msg.contains("event"))
    return Status::Ignored;
  auto arg = msg.find("arg");
  auto data = msg.find("data");
  if (arg == msg.end() || data == msg.end() || arg->value("instId", std::string()) != inst_)
    return Status::Ignored;
  const auto channel = arg->value("channel", std::string());
  Status st = Status::Ignored;
  if (channel == "books")
  {
    const bool snap = msg.value("action", std::string()) == "snapshot";
    for (const auto &d : *data)
    {
      st = on_books(d, snap);
      if (st == Status::Gap)
        return st;
    }
  }
  else if (channel == "trades")
  {
    if (!synced_)
      return Status::Ignored;
    ++st_.trade_msgs;
    for (const auto &d : *data)
      on_trade(d);
    st = Status::Ok;
  }
  return st;
}

void OkxSync::set(bool bid, const nlohmann::json &lvl)
{
  auto &side = bid ? bids_ : asks_;
  const std::string px = lvl.at(0).get<std::string>();
  const std::string sz = lvl.at(1).get<std::string>();
  const double p = std::stod(px), q = std::stod(sz);
  const int64_t t = out_.ticks(p);
  if (q <= 0)
    side.erase(t);
  else
    side[t] = Level{px, sz, q};
  window_.set(bid, p, q);
}

OkxSync::Status OkxSync::on_books(const nlohmann::json &d, bool snapshot)
{
  const int64_t seq = integer(d.at("seqId"));
  const int64_t prev = integer(d.at("prevSeqId"));
  const uint64_t ts_ms = uint64_t(integer(d.at("ts")));
  const uint64_t tx = ts_ms * 1000000ull;

  if (snapshot)
  {
    bids_.clear();
    asks_.clear();
    window_.clear();
  }
  else
  {
    if (!synced_)
      return Status::Ignored;
    if (prev != seq_)
    {
      ++st_.gaps;
      std::cerr << "OkxSync " << inst_ << ": prevSeqId " << prev << " after seqId " << seq_
                << "; resubscribing" << std::endl;
      invalidate();
      return Status::Gap;
    }
  }

  const auto &b = d.at("bids");
  const auto &a = d.at("asks");
  for (const auto &l : b)
    set(true, l);
  for (const auto &l : a)
    set(false, l);
  seq_ = seq;
  book_ts_ms_ = ts_ms;
  st_.max_bids = std::max(st_.max_bids, bids_.size());
  st_.max_asks = std::max(st_.max_asks, asks_.size());

  const int64_t cs = d.contains("checksum") ? integer(d.at("checksum")) : 0;
  if (cs == 0)
    ++st_.checksum_zero;
  else
  {
    ++st_.checksum_checked;
    const int32_t mine = checksum();
    if (mine != int32_t(cs))
    {
      ++st_.checksum_mismatch;
      ++st_.gaps;
      std::cerr << "OkxSync " << inst_ << ": checksum " << cs << ", computed " << mine << " at seqId "
                << seq << "; resubscribing" << std::endl;
      invalidate();
      return Status::Gap;
    }
  }

  if (!bids_.empty() && !asks_.empty() && bids_.rbegin()->first >= asks_.begin()->first)
    ++st_.crossed;

  if (snapshot)
  {
    ++st_.snapshots;
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
    ++st_.updates;
    if (b.empty() && a.empty() && seq == prev)
      ++st_.heartbeats;
    if (seq < prev)
      ++st_.seq_resets;
    window_.flush(tx);
  }
  last_book_tx_ = std::max(last_book_tx_, tx);
  out_.end_event();
  if (on_book)
    on_book(seq);
  return Status::Ok;
}

void OkxSync::on_trade(const nlohmann::json &d)
{
  const int64_t tid = integer(d.at("tradeId"));
  const int64_t count = d.contains("count") ? integer(d.at("count")) : 1;
  if (last_trade_id_ >= 0 && tid - count != last_trade_id_)
    ++st_.trade_id_gaps;
  last_trade_id_ = tid;

  const int64_t seq = d.contains("seqId") ? integer(d.at("seqId")) : seq_ + 1;
  const uint64_t ts = ms_to_ns(d.at("ts"));
  uint64_t tx;
  if (seq > seq_)
  {
    // Not yet in the book: stamped after the last book event, so Mbp2Mbo
    // takes its chunks from the front and the next update settles the level.
    tx = std::max(ts, last_book_tx_ + 1);
    ++st_.trades_before_book;
  }
  else
  {
    // Already in the last book applied: Mbp2Mbo replaces the chunks it
    // executes. 0 is at or below any depth time Mbp2Mbo holds; the record is
    // still stamped after the previous event (Mbp2Mbo::stamp).
    tx = 0;
    ++st_.trades_in_book;
  }
  // side is the taker's: a sell taker hit a resting bid.
  const bool buyer_is_maker = d.at("side").get<std::string>() == "sell";
  out_.trade(num(d.at("px")), num(d.at("sz")), buyer_is_maker, tx);
  out_.end_event();
  ++st_.trades;
}

}
