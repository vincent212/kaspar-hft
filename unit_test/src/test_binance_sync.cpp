/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * BinanceSync -- Binance's local-book procedure, driven with messages shaped
 * exactly as the spot and USD-M futures streams send them. No network.
 */

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "sim/BinanceSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace {

using nlohmann::json;
using Market = sim::BinanceSync::Market;

constexpr uint64_t kNow = 1791811800000000000ULL;

// [[price, qty], ...] as Binance sends it. A helper, because a braced
// {{"100.00", "0.002"}} is an OBJECT to nlohmann::json, not an array of pairs.
json lv(std::vector<std::pair<std::string, std::string>> v) {
  json a = json::array();
  for (auto &[p, q] : v) a.push_back(json::array({p, q}));
  return a;
}

json depth(int64_t U, int64_t u, json b, json a, int64_t pu = -1) {
  json ev = {{"e", "depthUpdate"}, {"E", 1791811800123}, {"s", "BTCUSDT"},
             {"U", U}, {"u", u}, {"b", b}, {"a", a}};
  if (pu >= 0) ev["pu"] = pu;
  return ev;
}

json snapshot(int64_t L, json b, json a) {
  return {{"lastUpdateId", L}, {"bids", b}, {"asks", a}};
}

class BinanceSyncTest : public ::testing::Test {
protected:
  std::vector<bfile::l3_t> out;
  sim::Mbp2Mbo m{{7, char(en::x::BINANCE), 0.01, 0.001},
                 [this](const bfile::l3_t &r) { out.push_back(r); }};

  size_t recovery_adds() const {
    size_t n = 0;
    for (const auto &r : out)
      if (auto *p = std::get_if<bfile::l3_mbo_v2_t>(&r); p && p->recovery) ++n;
    return n;
  }
};

TEST_F(BinanceSyncTest, NothingIsAppliedBeforeTheSnapshot) {
  sim::BinanceSync s(Market::SPOT, m);
  s.on_message(depth(101, 105, lv({{"100.00", "0.002"}}), lv({})));
  EXPECT_TRUE(s.need_snapshot());
  EXPECT_FALSE(s.synced());
  EXPECT_TRUE(out.empty());
}

TEST_F(BinanceSyncTest, TheSnapshotStartsTheMarketAndLoadsTheBookAsRecovery) {
  sim::BinanceSync s(Market::SPOT, m);
  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({{"100.01", "0.001"}})), kNow);

  ASSERT_FALSE(out.empty());
  auto sst = std::get_if<bfile::l3_sst_t>(&out[0]);
  ASSERT_NE(sst, nullptr) << "the first record says the market is matching";
  EXPECT_EQ(sst->tradingStatus, 17);
  EXPECT_EQ(recovery_adds(), 3u) << "2 + 1 chunks";
  EXPECT_TRUE(s.synced());
}

// Spot: drop u <= L, the first applied straddles L+1.
TEST_F(BinanceSyncTest, SpotBufferedEventsOlderThanTheSnapshotAreDropped) {
  sim::BinanceSync s(Market::SPOT, m);
  s.on_message(depth(90, 100, lv({{"99.00", "0.005"}}), lv({})));    // u <= L: in the snapshot
  s.on_message(depth(101, 105, lv({{"100.00", "0.004"}}), lv({})));  // straddles 101
  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({{"100.05", "0.001"}})), kNow);

  EXPECT_TRUE(s.synced());
  EXPECT_EQ(s.events_applied(), 1u);
  EXPECT_EQ(m.chunks(true, 9900), 0u) << "the old event must not be replayed";
  EXPECT_EQ(m.chunks(true, 10000), 4u) << "the straddling one is";
}

TEST_F(BinanceSyncTest, SpotASnapshotOlderThanTheStreamIsRefetched) {
  sim::BinanceSync s(Market::SPOT, m);
  s.on_message(depth(150, 155, lv({{"100.00", "0.004"}}), lv({})));
  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({})), kNow);

  EXPECT_TRUE(s.need_snapshot()) << "updates 101..149 are in neither";
  EXPECT_FALSE(s.synced());
  EXPECT_EQ(recovery_adds(), 0u);
}

TEST_F(BinanceSyncTest, SpotAGapForcesAResync) {
  sim::BinanceSync s(Market::SPOT, m);
  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({{"100.05", "0.001"}})), kNow);
  s.on_message(depth(101, 105, lv({{"100.00", "0.003"}}), lv({})));
  ASSERT_TRUE(s.synced());

  s.on_message(depth(107, 110, lv({{"100.00", "0.009"}}), lv({})));   // 106 missing

  EXPECT_FALSE(s.synced());
  EXPECT_TRUE(s.need_snapshot());
  EXPECT_EQ(s.gaps(), 1u);
  EXPECT_EQ(m.chunks(true, 10000), 3u) << "the event after the gap is not applied";

  out.clear();
  s.on_snapshot(snapshot(108, lv({{"100.00", "0.009"}}), lv({{"100.05", "0.001"}})), kNow + 1);
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(recovery_adds(), 10u) << "the new snapshot goes to OB as recovery";
}

// Futures: drop u < L, the first applied has U <= L <= u, then pu == prev u.
TEST_F(BinanceSyncTest, FuturesSequencesOnPu) {
  sim::BinanceSync s(Market::FUTURES, m);
  s.on_message(depth(95, 100, lv({{"100.00", "0.004"}}), lv({}), 94));
  s.on_message(depth(101, 103, lv({{"100.00", "0.006"}}), lv({}), 100));
  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({{"100.05", "0.001"}})), kNow);

  EXPECT_TRUE(s.synced());
  EXPECT_EQ(s.events_applied(), 2u) << "u == L is NOT older than the snapshot on futures";
  EXPECT_EQ(m.chunks(true, 10000), 6u);

  s.on_message(depth(104, 104, lv({{"100.00", "0.001"}}), lv({}), 103));
  EXPECT_EQ(m.chunks(true, 10000), 1u);

  s.on_message(depth(106, 107, lv({{"100.00", "0.002"}}), lv({}), 105));   // pu 105 != 104
  EXPECT_FALSE(s.synced());
  EXPECT_EQ(s.gaps(), 1u);
}

TEST_F(BinanceSyncTest, TradesApplyOnlyWhileInSync) {
  sim::BinanceSync s(Market::SPOT, m);
  json t = {{"e", "trade"}, {"E", 1791811800200}, {"s", "BTCUSDT"}, {"t", 1},
            {"p", "100.05"}, {"q", "0.001"}, {"T", 1791811800199}, {"m", false}};
  s.on_message(t);
  EXPECT_EQ(s.trades_applied(), 0u);

  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({{"100.05", "0.002"}})), kNow);
  s.on_message(t);
  EXPECT_EQ(s.trades_applied(), 1u);
  EXPECT_EQ(m.chunks(false, 10005), 1u) << "a buyer lifted one chunk of the offer";
}

TEST_F(BinanceSyncTest, FuturesAggTradeIsATrade) {
  sim::BinanceSync s(Market::FUTURES, m);
  s.on_snapshot(snapshot(100, lv({{"100.00", "0.002"}}), lv({{"100.05", "0.002"}})), kNow);
  json t = {{"e", "aggTrade"}, {"E", 1791811800200}, {"s", "BTCUSDT"}, {"a", 5},
            {"p", "100.00"}, {"q", "0.002"}, {"f", 10}, {"l", 11},
            {"T", 1791811800199}, {"m", true}};
  s.on_message(t);
  EXPECT_EQ(m.chunks(true, 10000), 0u) << "m true: the buyer was the maker, the bid was hit";
}

}  // namespace
