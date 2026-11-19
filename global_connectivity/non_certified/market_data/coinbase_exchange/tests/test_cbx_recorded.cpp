/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Tests from frames recorded from Coinbase Exchange's public endpoints on
 * 2026-11-18 (tests/data, cut from the 42-minute recording README.md
 * describes):
 *
 *   btc_l3_slice.json   a slice of the REST level-3 book A of BTC-USD (the
 *                       best 25 levels per side and every order a match
 *                       touched), the public `match` frames between A and the
 *                       next level-3 book B, and each slice order's size in
 *                       B (null: not in B)
 *   eth_l2_batch.json   a level2_batch snapshot of ETH-USD (best 200 levels
 *                       per side), the next 100 l2update frames and the
 *                       matches and heartbeats between them, and the top 10
 *                       levels after them as an independent Python
 *                       reconstruction computed them
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include "coinbase_exchange/CoinbaseExchangeL2Sync.hpp"
#include "coinbase_exchange/CoinbaseExchangeSync.hpp"
#include "enum/e_names.hpp"

using namespace coinbase_exchange;
using nlohmann::json;

namespace
{
  json load(const std::string &name)
  {
    const char *k = std::getenv("KSPRPROJ");
    std::ifstream f(std::string(k ? k : "") +
                    "/global_connectivity/non_certified/market_data/coinbase_exchange/tests/data/" + name);
    return json::parse(f);
  }
  ProductSpec spec(const std::string &p, const char *unit)
  {
    std::string err;
    auto s = ProductSpec::make(p, 1, char(en::x::COINBASEX), "0.01", "0.00000001", unit, err);
    EXPECT_TRUE(s) << err;
    return *s;
  }
}

// Snapshot A, then the public matches (a partial stream: no opens, dones or
// changes), against snapshot B: every order a match filled is gone from B;
// every order a match reduced is in B at exactly the size predicted, or gone
// (cancelled since); matches at or below A's sequence are discarded.
TEST(CbxRecorded, L3SnapshotPlusMatchesAgainstTheNextSnapshot)
{
  auto d = load("btc_l3_slice.json");
  CoinbaseExchangeSync::Config c{spec("BTC-USD", "0.00001")};
  c.check_gaps = false;
  std::vector<bfile::l3_t> out;
  CoinbaseExchangeSync s(c, [&](const bfile::l3_t &r) { out.push_back(r); });
  ASSERT_EQ(s.on_snapshot(d["snapshot_a"], 1), CoinbaseExchangeSync::SnapResult::Applied);
  const auto a_orders = s.orders();
  std::map<std::string, int64_t> a_size;
  for (const char *side : {"bids", "asks"})
    for (const auto &r : d["snapshot_a"][side])
      a_size[r[2].get<std::string>()] = *scaled(r[1].get<std::string>(), 8);

  const int64_t SA = d["snapshot_a"]["sequence"].get<int64_t>();
  size_t discarded = 0;
  for (const auto &f : d["matches"])
  {
    auto m = json::parse(f.get<std::string>());
    if (m["sequence"].get<int64_t>() <= SA)
      ++discarded;
    s.on_message(m, 2);
  }
  EXPECT_GT(discarded, 0u) << "the slice has matches already in snapshot A";
  EXPECT_EQ(s.stats().duplicates, discarded) << "at or below the snapshot: discarded, not applied";
  EXPECT_EQ(s.stats().unknown_maker, 0u) << "every maker kept is in the slice";

  size_t filled = 0, reduced_ok = 0, reduced_gone = 0, untouched_same = 0;
  for (const auto &[uuid, bsize] : d["b_size_of_slice_orders"].items())
  {
    const auto *o = s.find(*parse_uuid(uuid));
    if (!o)
    {
      ++filled;
      EXPECT_TRUE(bsize.is_null()) << uuid << ": filled by the matches, yet in B";
      continue;
    }
    if (bsize.is_null())
    {
      ++reduced_gone;
      continue;
    }
    int64_t b = *scaled(bsize.get<std::string>(), 8);
    if (o->size != a_size[uuid])
    {
      EXPECT_EQ(o->size, b) << uuid << ": reduced by matches";
      ++reduced_ok;
    }
    else if (o->size == b)
      ++untouched_same;
  }
  EXPECT_GT(filled, 0u);
  EXPECT_GT(reduced_ok + untouched_same, 0u);
  EXPECT_EQ(a_orders - filled, s.orders());
  RecordProperty("filled", int(filled));
  RecordProperty("reduced_as_predicted", int(reduced_ok));
}

// level2_batch through CoinbaseExchangeL2Sync: the book after 100 recorded
// batches equals the Python reconstruction, and every match is applied.
TEST(CbxRecorded, Level2BatchMatchesAnIndependentReconstruction)
{
  auto d = load("eth_l2_batch.json");
  auto p = spec("ETH-USD", "0.0001");
  std::vector<bfile::l3_t> out;
  sim::Mbp2Mbo m({1, char(en::x::COINBASEX), 0.01, 0.1}, [&](const bfile::l3_t &r) { out.push_back(r); });
  CoinbaseExchangeL2Sync s(p, m, 50);
  uint64_t t = 1;
  for (const auto &f : d["frames"])
    s.on_message(json::parse(f.get<std::string>()), t++);
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(s.stats().updates, d["updates"].get<uint64_t>());
  EXPECT_EQ(s.stats().trades, d["matches"].get<uint64_t>());
  EXPECT_EQ(s.stats().trades_missing, 0u) << "trade ids consecutive in the recording";
  EXPECT_EQ(s.stats().crossed_updates, 0u);
  auto top = [&](bool bid) {
    std::vector<std::pair<int64_t, int64_t>> v;
    const auto &side = s.side(bid);
    if (bid)
      for (auto it = side.rbegin(); it != side.rend() && v.size() < 10; ++it)
        v.emplace_back(*it);
    else
      for (auto it = side.begin(); it != side.end() && v.size() < 10; ++it)
        v.emplace_back(*it);
    return v;
  };
  auto want = [&](const json &rows) {
    std::vector<std::pair<int64_t, int64_t>> v;
    for (const auto &r : rows)
      v.emplace_back(*p.ticks(r[0].get<std::string>()), *p.size(r[1].get<std::string>()));
    return v;
  };
  EXPECT_EQ(top(true), want(d["expect_top10_bids"]));
  EXPECT_EQ(top(false), want(d["expect_top10_asks"]));
  // and Mbp2Mbo holds the window: the best bid and ask agree
  EXPECT_EQ(m.best(true), top(true)[0].first);
  EXPECT_EQ(m.best(false), top(false)[0].first);
}
