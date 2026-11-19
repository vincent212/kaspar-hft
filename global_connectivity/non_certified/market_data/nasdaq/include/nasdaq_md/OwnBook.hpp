#pragma once
// The handler's own order book: every displayed order of the feed, by Order
// Reference Number, with per-security price levels. It is what the dispatcher
// looks orders up in (ITCH modify messages carry only the reference, feed.md
// section 6), and what replay checks OB against.
//
// Prices are ITCH Price(4) integers, so a level's price is also OB's tick index
// when the asset's tick is 0.0001 (Instruments.hpp).
#include <algorithm>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nasdaq_md
{
struct OwnOrder
{
  uint16_t locate = 0;
  char side = 0;          // 'B' or 'S', as ITCH sends it
  uint32_t px = 0;        // Price(4)
  uint32_t shares = 0;    // remaining displayed shares
  uint64_t prio = 0;      // handler counter at entry: queue order within a level
  bool sent = false;      // its records go to a book (routed, and its price on the tick)
};

struct Level
{
  uint64_t shares = 0;
  uint32_t count = 0;
  bool operator==(const Level &) const = default;
};

class OwnBook
{
public:
  struct LocBook
  {
    std::map<uint32_t, Level, std::greater<uint32_t>> bids;
    std::map<uint32_t, Level> asks;
  };

  OwnBook() : books_(65536) {}

  // false if the reference is already on the book (the order is not added).
  bool add(uint64_t ref, const OwnOrder &o)
  {
    auto [it, fresh] = orders_.emplace(ref, o);
    if (!fresh)
      return false;
    level(o.locate, o.side, o.px, int64_t(o.shares), +1);
    return true;
  }

  OwnOrder *find(uint64_t ref)
  {
    auto it = orders_.find(ref);
    return it == orders_.end() ? nullptr : &it->second;
  }

  // Removes n shares (at most what remains); returns the shares left. At 0 the
  // order is erased.
  uint32_t reduce(uint64_t ref, uint32_t n)
  {
    auto it = orders_.find(ref);
    if (it == orders_.end())
      return 0;
    auto &o = it->second;
    const uint32_t take = n > o.shares ? o.shares : n;
    o.shares -= take;
    level(o.locate, o.side, o.px, -int64_t(take), o.shares == 0 ? -1 : 0);
    const uint32_t left = o.shares;
    if (left == 0)
      orders_.erase(it);
    return left;
  }

  void erase(uint64_t ref)
  {
    auto it = orders_.find(ref);
    if (it == orders_.end())
      return;
    const auto &o = it->second;
    level(o.locate, o.side, o.px, -int64_t(o.shares), -1);
    orders_.erase(it);
  }

  void clear()
  {
    orders_.clear();
    for (auto &b : books_)
      b.reset();
  }

  const LocBook *book(uint16_t locate) const { return books_[locate].get(); }
  size_t size() const { return orders_.size(); }
  const std::unordered_map<uint64_t, OwnOrder> &orders() const { return orders_; }

  // 0 when the side is empty.
  uint32_t best_bid(uint16_t locate) const
  {
    const auto *b = books_[locate].get();
    return b && !b->bids.empty() ? b->bids.begin()->first : 0;
  }
  uint32_t best_ask(uint16_t locate) const
  {
    const auto *b = books_[locate].get();
    return b && !b->asks.empty() ? b->asks.begin()->first : 0;
  }

  // The best n levels of one side, best first.
  std::vector<std::pair<uint32_t, Level>> top(uint16_t locate, bool bid, size_t n) const
  {
    std::vector<std::pair<uint32_t, Level>> r;
    const auto *b = books_[locate].get();
    if (!b)
      return r;
    if (bid)
      for (auto it = b->bids.begin(); it != b->bids.end() && r.size() < n; ++it)
        r.emplace_back(*it);
    else
      for (auto it = b->asks.begin(); it != b->asks.end() && r.size() < n; ++it)
        r.emplace_back(*it);
    return r;
  }

  // Every order, grouped by (locate, side, price), each group in queue order.
  // Used to build GLIMPSE spins and to compare queue order after a recovery.
  struct Key
  {
    uint16_t locate;
    char side;
    uint32_t px;
    auto operator<=>(const Key &) const = default;
  };
  std::map<Key, std::vector<std::pair<uint64_t, OwnOrder>>> queues() const
  {
    std::map<Key, std::vector<std::pair<uint64_t, OwnOrder>>> q;
    for (const auto &[ref, o] : orders_)
      q[Key{o.locate, o.side, o.px}].emplace_back(ref, o);
    for (auto &[k, v] : q)
      std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.second.prio < b.second.prio; });
    return q;
  }

private:
  std::unordered_map<uint64_t, OwnOrder> orders_;
  std::vector<std::unique_ptr<LocBook>> books_;

  void level(uint16_t locate, char side, uint32_t px, int64_t dshares, int dcount)
  {
    auto &b = books_[locate];
    if (!b)
      b = std::make_unique<LocBook>();
    auto apply = [&](auto &m) {
      auto &l = m[px];
      l.shares = uint64_t(int64_t(l.shares) + dshares);
      l.count = uint32_t(int(l.count) + dcount);
      if (l.count == 0)
        m.erase(px);
    };
    if (side == 'B')
      apply(b->bids);
    else
      apply(b->asks);
  }
};
} // namespace nasdaq_md
