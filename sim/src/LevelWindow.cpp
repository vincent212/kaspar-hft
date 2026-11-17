/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/LevelWindow.hpp"

#include <vector>

namespace sim
{

void LevelWindow::set(bool bid, double px, double qty)
{
  auto &side = bid ? bids_ : asks_;
  const int64_t t = out_.ticks(px);
  if (qty <= 0)
    side.erase(t);
  else
    side[t] = {px, qty};
}

// The best `depth_` levels: the highest bids, the lowest asks.
LevelWindow::Side LevelWindow::top(bool bid) const
{
  Side w;
  if (bid)
  {
    for (auto it = bids_.rbegin(); it != bids_.rend() && w.size() < depth_; ++it)
      w.insert(*it);
  }
  else
  {
    for (auto it = asks_.begin(); it != asks_.end() && w.size() < depth_; ++it)
      w.insert(*it);
  }
  return w;
}

void LevelWindow::flush_side(bool bid, uint64_t tx)
{
  Side now = top(bid);
  Side &sent = bid ? sent_b_ : sent_a_;
  for (const auto &[t, pq] : sent)
    if (!now.count(t))
      out_.level(bid, pq.first, 0, tx);
  for (const auto &[t, pq] : now)
  {
    auto it = sent.find(t);
    if (it == sent.end() || it->second.second != pq.second)
      out_.level(bid, pq.first, pq.second, tx);
  }
  sent.swap(now);
}

void LevelWindow::flush(uint64_t tx)
{
  flush_side(true, tx);
  flush_side(false, tx);
}

void LevelWindow::resync(uint64_t tx)
{
  sent_b_ = top(true);
  sent_a_ = top(false);
  std::vector<std::pair<double, double>> b, a;
  for (const auto &[t, pq] : sent_b_)
    b.push_back(pq);
  for (const auto &[t, pq] : sent_a_)
    a.push_back(pq);
  out_.snapshot(b, a, tx);
}

}
