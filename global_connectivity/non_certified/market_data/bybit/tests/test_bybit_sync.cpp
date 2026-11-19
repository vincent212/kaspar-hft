/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitSync: the documentation's examples, synthetic sequences for every
// alignment and recovery rule, and frames recorded from Bybit's public stream
// (tests/data, recorded with tools/bybit_record on 2026-11-18).

#include <gtest/gtest.h>

#include <cmath>
#include <map>

#include "Fixture.hpp"

using namespace bybit;
using namespace bybit::test;
using json = nlohmann::json;

namespace
{
  using Lv = std::vector<std::pair<std::string, std::string>>;

  json levels(const Lv &v)
  {
    json a = json::array();
    for (const auto &[p, q] : v)
      a.push_back({p, q});
    return a;
  }

  json full_delta(int64_t u, int64_t seq, const Lv &b, const Lv &a, uint64_t cts = 1000)
  {
    return {{"topic", "orderbook.full.BTCUSDT"}, {"type", "delta"}, {"ts", cts + 2},
            {"data", {{"s", "BTCUSDT"}, {"b", levels(b)}, {"a", levels(a)}, {"u", u}, {"seq", seq}}},
            {"cts", cts}};
  }

  json rest(int64_t u, int64_t seq, const Lv &b, const Lv &a, uint64_t cts = 1000)
  {
    return {{"s", "BTCUSDT"}, {"b", levels(b)}, {"a", levels(a)}, {"u", u}, {"seq", seq},
            {"ts", cts + 2}, {"cts", cts}};
  }

  json trade(const std::string &side, const std::string &px, const std::string &qty, uint64_t T,
             bool rpi = false, bool bt = false)
  {
    return {{"topic", "publicTrade.BTCUSDT"}, {"type", "snapshot"}, {"ts", T + 2},
            {"data", json::array({{{"T", T}, {"s", "BTCUSDT"}, {"S", side}, {"v", qty}, {"p", px},
                                   {"L", "PlusTick"}, {"i", "x"}, {"BT", bt}, {"RPI", rpi},
                                   {"seq", 1}}})}};
  }

  double qty(const BybitSync::Book &b, int64_t t)
  {
    auto it = b.find(t);
    return it == b.end() ? 0 : it->second;
  }
}

// --- documentation examples ---------------------------------------------------

// The orderbook.full response example (websocket/public/full-ob): five bid
// levels, no asks, u 278547, seq 1501100579, cts 1784103477730.
TEST(BybitSyncDoc, FullOrderbookDeltaExample)
{
  const json doc = json::parse(R"({
    "topic": "orderbook.full.MNTUSDT", "ts": 1784103477732, "type": "delta",
    "data": {"s": "MNTUSDT",
             "b": [["0.4758","2342.47"],["0.4757","1735.35"],["0.4755","952.38"],
                   ["0.4753","522.68"],["0.4752","387.21"]],
             "a": [], "u": 278547, "seq": 1501100579},
    "cts": 1784103477730})");
  Rig r(BybitSync::Mode::FULL, "MNTUSDT", 0.0001, 0.01, 1.0);
  r.s.on_message(doc);
  EXPECT_TRUE(r.s.buffering());
  EXPECT_TRUE(r.s.need_snapshot());
  // A REST snapshot at the delta before it, then the delta applies on top.
  json snap = {{"s", "MNTUSDT"}, {"b", json::array({{"0.4758", "1"}, {"0.4750", "5"}})},
               {"a", json::array({{"0.4760", "7"}})}, {"u", 278546}, {"seq", 1501100500},
               {"ts", 1784103477532}, {"cts", 1784103477530}};
  EXPECT_EQ(r.s.on_snapshot(snap), BybitSync::SnapResult::Applied);
  EXPECT_EQ(r.s.last_u(), 278547);
  EXPECT_EQ(r.s.last_seq(), 1501100579);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 4758), 2342.47);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 4752), 387.21);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 4750), 5);
  EXPECT_EQ(r.s.bids().size(), 6u);
  EXPECT_EQ(r.s.last_cts_ms(), 1784103477730u);
}

// The publicTrade response example (websocket/public/trade): S "Buy" is the
// taker, so the trade executes against the ask and prints aggressor 1.
TEST(BybitSyncDoc, PublicTradeExampleIsTakerSide)
{
  const json doc = json::parse(R"({
    "topic": "publicTrade.BTCUSDT", "type": "snapshot", "ts": 1672304486868,
    "data": [{"T": 1672304486865, "s": "BTCUSDT", "S": "Buy", "v": "0.001",
              "p": "16578.50", "L": "PlusTick", "i": "20f43950-d8dd-5b31-9112-a178eb6023af",
              "BT": false, "seq": 1783284617}]})");
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.5, 0.001, 0.001);
  r.s.on_message(full_delta(11, 100, {}, {}, 1672304486800));
  ASSERT_EQ(r.s.on_snapshot(rest(10, 90, {{"16578.00", "0.003"}}, {{"16578.50", "0.002"}},
                                 1672304486700)),
            BybitSync::SnapResult::Applied);
  r.recs.clear();
  r.s.on_message(doc);
  ASSERT_EQ(r.count<bfile::l3_mbp_trd_t>(), 1u);
  const auto &p = std::get<bfile::l3_mbp_trd_t>(r.recs.front());
  EXPECT_EQ(p.aggresorSide, 1);
  EXPECT_EQ(p.pxd, 33157.0); // 16578.50 / 0.5
  EXPECT_EQ(p.sz, 1);
  EXPECT_EQ(p.transactTime, 1672304486865000000ull);
  EXPECT_EQ(r.count<bfile::l3_mbo_trd_v2_t>(), 1u);
  EXPECT_EQ(r.m.chunks(false, 33157), 1u); // one of two ask chunks executed
  EXPECT_EQ(r.m.chunks(true, 33156), 3u);
}

// --- FULL mode alignment ------------------------------------------------------

TEST(BybitSyncFull, SnapshotAlignsOnUpdateId)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  r.s.on_message(full_delta(11, 1005, {{"100.0", "2"}}, {}));
  r.s.on_message(full_delta(12, 1009, {{"100.0", "3"}}, {{"100.5", "4"}}));
  EXPECT_FALSE(r.s.synced());
  // The snapshot is the book after u 11: deltas 10 and 11 are in it.
  EXPECT_EQ(r.s.on_snapshot(rest(11, 1005, {{"100.0", "2"}, {"99.9", "8"}}, {{"100.6", "1"}})),
            BybitSync::SnapResult::Applied);
  EXPECT_TRUE(r.s.synced());
  EXPECT_EQ(r.s.last_u(), 12);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 3);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 999), 8);
  EXPECT_DOUBLE_EQ(qty(r.s.asks(), 1005), 4);
  EXPECT_EQ(r.s.stats().deltas, 1u);
  EXPECT_EQ(r.s.stats().gaps, 0u);
}

TEST(BybitSyncFull, SnapshotOlderThanTheStreamIsRefused)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  EXPECT_EQ(r.s.on_snapshot(rest(8, 990, {{"100.0", "2"}}, {})), BybitSync::SnapResult::TooOld);
  EXPECT_FALSE(r.s.synced());
  EXPECT_TRUE(r.s.need_snapshot());
  // u 9 is exactly old enough: delta 10 follows it.
  EXPECT_EQ(r.s.on_snapshot(rest(9, 995, {{"100.0", "2"}}, {})), BybitSync::SnapResult::Applied);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 1);
}

TEST(BybitSyncFull, SnapshotWithAnotherSeqAtTheSameUIsRefused)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  r.s.on_message(full_delta(11, 1005, {{"100.0", "2"}}, {}));
  EXPECT_EQ(r.s.on_snapshot(rest(11, 999, {}, {})), BybitSync::SnapResult::SeqMismatch);
  EXPECT_EQ(r.s.stats().seq_mismatch, 1u);
  EXPECT_FALSE(r.s.synced());
}

TEST(BybitSyncFull, BufferRestartsOnABreakAndDropsSeqRegressions)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  r.s.on_message(full_delta(12, 1010, {{"100.0", "3"}}, {})); // break: buffer restarts at 12
  r.s.on_message(full_delta(13, 1005, {{"100.0", "9"}}, {})); // seq went back: dropped
  EXPECT_EQ(r.s.stats().seq_back, 1u);
  EXPECT_EQ(r.s.on_snapshot(rest(10, 1000, {{"100.0", "1"}}, {})), BybitSync::SnapResult::TooOld);
  EXPECT_EQ(r.s.on_snapshot(rest(11, 1003, {{"100.0", "2"}}, {})), BybitSync::SnapResult::Applied);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 3);
}

TEST(BybitSyncFull, RedeliveredDeltaWhileBufferingIsNotABreak)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  r.s.on_message(full_delta(11, 1001, {{"100.0", "2"}}, {}));
  r.s.on_message(full_delta(11, 1001, {{"100.0", "2"}}, {})); // delivered twice
  r.s.on_message(full_delta(12, 1002, {{"100.0", "3"}}, {}));
  EXPECT_EQ(r.s.stats().duplicates, 1u);
  EXPECT_TRUE(r.s.buffering());
  // The buffer still starts at 10: a snapshot at 9 aligns and 10..12 apply.
  EXPECT_EQ(r.s.on_snapshot(rest(9, 999, {{"99.0", "1"}}, {})), BybitSync::SnapResult::Applied);
  EXPECT_EQ(r.s.last_u(), 12);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 3);
  EXPECT_EQ(r.s.stats().deltas, 3u);
}

TEST(BybitSyncFull, GapSendsGapRecordAndWaitsForASnapshot)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {{"100.1", "1"}}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {}, {})), BybitSync::SnapResult::Applied);
  r.recs.clear();
  r.s.on_message(full_delta(12, 1010, {{"100.0", "5"}}, {})); // 11 missing
  EXPECT_EQ(r.s.stats().gaps, 1u);
  EXPECT_EQ(r.count<bfile::l3_gap_v2_t>(), 1u);
  EXPECT_TRUE(r.s.need_snapshot());
  EXPECT_TRUE(r.s.buffering());
  r.s.on_message(full_delta(13, 1011, {{"100.0", "6"}}, {}));
  // Trades while out of sync are dropped.
  r.s.on_message(trade("Sell", "100.0", "0.001", 2000));
  EXPECT_EQ(r.s.stats().trades_unsynced, 1u);
  ASSERT_EQ(r.s.on_snapshot(rest(12, 1010, {{"100.0", "5"}, {"99.0", "1"}}, {{"100.1", "1"}})),
            BybitSync::SnapResult::Applied);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 6);
  EXPECT_EQ(r.s.last_u(), 13);
}

TEST(BybitSyncFull, ReconnectSendsGapAndResynchronises)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {{"100.1", "1"}}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {}, {})), BybitSync::SnapResult::Applied);
  r.recs.clear();
  r.s.reset(); // the connection dropped
  EXPECT_EQ(r.count<bfile::l3_gap_v2_t>(), 1u);
  EXPECT_TRUE(r.s.need_snapshot());
  r.s.reset(); // a second failed attempt: no second GAP
  EXPECT_EQ(r.count<bfile::l3_gap_v2_t>(), 1u);
  r.s.on_message(full_delta(15, 1020, {{"100.0", "2"}}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(14, 1019, {{"100.0", "7"}}, {{"100.1", "1"}})), BybitSync::SnapResult::Applied);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 2);
}

TEST(BybitSyncFull, DuplicatesAreSkipped)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {}, {})), BybitSync::SnapResult::Applied);
  r.s.on_message(full_delta(11, 1001, {{"100.0", "2"}}, {}));
  r.s.on_message(full_delta(11, 1001, {{"100.0", "7"}}, {}));
  r.s.on_message(full_delta(10, 1000, {{"100.0", "7"}}, {}));
  EXPECT_EQ(r.s.stats().duplicates, 2u);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 2);
}

TEST(BybitSyncFull, UpdateIdOneRestartsTheSync)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {}, {})), BybitSync::SnapResult::Applied);
  r.recs.clear();
  r.s.on_message(full_delta(1, 5, {}, {})); // service restart or tick change
  EXPECT_EQ(r.s.stats().resets, 1u);
  EXPECT_EQ(r.count<bfile::l3_gap_v2_t>(), 1u);
  EXPECT_FALSE(r.s.synced());
  r.s.on_message(full_delta(2, 6, {{"100.0", "4"}}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(1, 5, {{"99.0", "1"}}, {{"101.0", "1"}})),
            BybitSync::SnapResult::Applied);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 4);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 990), 1);
}

// Delisting: u = 1 with no levels, and an empty REST book. Mbp2Mbo has no
// level to send as recovery, so a CHR clears OB.
TEST(BybitSyncFull, EmptyResetSendsAClear)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {{"100.0", "1"}}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {}, {{"101.0", "1"}})), BybitSync::SnapResult::Applied);
  r.s.on_message(full_delta(1, 5, {}, {}));
  r.recs.clear();
  ASSERT_EQ(r.s.on_snapshot(rest(1, 5, {}, {})), BybitSync::SnapResult::Applied);
  EXPECT_EQ(r.count<bfile::l3_chr_v2_t>(), 1u);
  EXPECT_TRUE(r.s.bids().empty());
  EXPECT_TRUE(r.s.asks().empty());
  EXPECT_EQ(r.m.best(true), 0);
  EXPECT_EQ(r.m.best(false), 0);
}

// --- book semantics -------------------------------------------------------------

TEST(BybitSyncFull, SizesAreAbsoluteAndZeroDeletes)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.5);
  r.s.on_message(full_delta(10, 1000, {}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {{"100.0", "1"}, {"99.9", "2"}}, {{"100.1", "1"}})),
            BybitSync::SnapResult::Applied);
  r.s.on_message(full_delta(11, 1001, {{"100.0", "1.5"}, {"99.9", "0"}, {"99.8", "0.4"}}, {}));
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 1000), 1.5);
  EXPECT_EQ(r.s.bids().count(999), 0u);
  EXPECT_DOUBLE_EQ(qty(r.s.bids(), 998), 0.4);
  // Mbp2Mbo: ceil(qty / chunk) chunks per level.
  EXPECT_EQ(r.m.chunks(true, 1000), 3u);
  EXPECT_EQ(r.m.chunks(true, 999), 0u);
  EXPECT_EQ(r.m.chunks(true, 998), 1u);
}

TEST(BybitSyncFull, TakerSellHitsTheBidAndRpiOrBlockTradesOnlyPrint)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(full_delta(10, 1000, {}, {}, 1000));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {{"100.0", "0.003"}}, {{"100.1", "0.003"}}, 900)),
            BybitSync::SnapResult::Applied);
  r.recs.clear();
  r.s.on_message(trade("Sell", "100.0", "0.002", 2000));
  EXPECT_EQ(r.m.chunks(true, 1000), 1u);
  EXPECT_EQ(r.count<bfile::l3_mbo_trd_v2_t>(), 2u);
  EXPECT_EQ(std::get<bfile::l3_mbp_trd_t>(r.recs.front()).aggresorSide, 2);
  r.recs.clear();
  r.s.on_message(trade("Buy", "100.1", "0.001", 2001, true));        // RPI
  r.s.on_message(trade("Buy", "100.1", "0.001", 2002, false, true)); // block trade
  EXPECT_EQ(r.count<bfile::l3_mbp_trd_t>(), 2u);
  EXPECT_EQ(r.count<bfile::l3_mbo_trd_v2_t>(), 0u);
  EXPECT_EQ(r.m.chunks(false, 1001), 3u);
  EXPECT_EQ(r.s.stats().prints_only, 2u);
}

TEST(BybitSyncFull, OnlyTheWindowReachesMbp2Mbo)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 1.0, 2);
  r.s.on_message(full_delta(10, 1000, {}, {}));
  ASSERT_EQ(r.s.on_snapshot(rest(9, 999, {{"100.0", "1"}, {"99.9", "1"}, {"99.8", "1"}},
                                 {{"100.1", "1"}, {"100.2", "1"}, {"100.3", "1"}})),
            BybitSync::SnapResult::Applied);
  EXPECT_EQ(r.s.bids().size(), 3u);
  EXPECT_EQ(r.m.chunks(true, 998), 0u);
  EXPECT_EQ(r.m.chunks(false, 1003), 0u);
  r.s.on_message(full_delta(11, 1001, {{"100.0", "0"}}, {})); // 99.8 enters the window
  EXPECT_EQ(r.m.chunks(true, 998), 1u);
  EXPECT_EQ(r.m.best(true), 999);
}

TEST(BybitSyncFull, OtherSymbolsAndControlFramesAreIgnored)
{
  Rig r(BybitSync::Mode::FULL, "BTCUSDT", 0.1, 0.001, 0.001);
  r.s.on_message(json::parse(R"({"success":true,"ret_msg":"subscribe","conn_id":"x","op":"subscribe"})"));
  r.s.on_message(json::parse(R"({"success":true,"ret_msg":"pong","conn_id":"x","op":"ping"})"));
  auto d = full_delta(10, 1, {{"1", "1"}}, {});
  d["topic"] = "orderbook.full.ETHUSDT";
  r.s.on_message(d);
  EXPECT_FALSE(r.s.buffering());
}

// --- recorded frames -------------------------------------------------------------

// Spot SOLBTC orderbook.full, 25 s, four REST snapshots. The first
// synchronises; each later one must equal the book at its u, level by level.
TEST(BybitSyncRecorded, SpotFullMatchesEveryRestSnapshotExactly)
{
  const auto frames = load("spot_SOLBTC_full.rec");
  ASSERT_FALSE(frames.empty());
  std::vector<json> snaps;
  for (const auto &f : frames)
    if (f.kind == 'R')
      snaps.push_back(f.j.at("result"));
  ASSERT_GE(snaps.size(), 3u);

  // Replay up to each later snapshot's u and compare.
  for (size_t k = 1; k < snaps.size(); ++k)
  {
    Rig r(BybitSync::Mode::FULL, "SOLBTC", 0.0000001, 0.0001, 1.0);
    const int64_t target = snaps[k].at("u").get<int64_t>();
    bool used = false;
    for (const auto &f : frames)
    {
      if (f.kind == 'R')
      {
        if (!used)
        {
          ASSERT_EQ(r.s.on_snapshot(f.j.at("result")), BybitSync::SnapResult::Applied);
          used = true;
        }
        continue;
      }
      if (r.s.synced() && r.s.last_u() >= target)
        break;
      r.s.on_message(f.j);
    }
    if (r.s.last_u() != target)
    {
      // The last snapshot is newer than the last recorded delta.
      EXPECT_EQ(k, snaps.size() - 1);
      continue;
    }
    std::map<int64_t, double> b, a;
    for (const auto &l : snaps[k].at("b"))
      b[r.s.ticks(BybitSync::num(l[0]))] = BybitSync::num(l[1]);
    for (const auto &l : snaps[k].at("a"))
      a[r.s.ticks(BybitSync::num(l[0]))] = BybitSync::num(l[1]);
    // Compare over the snapshot's range (spot REST stops at 10,000 levels).
    const int64_t blo = b.begin()->first, ahi = a.rbegin()->first;
    size_t n = 0, bad = 0;
    for (const auto &[t, q] : b)
      n++, bad += std::fabs(qty(r.s.bids(), t) - q) > 1e-12;
    for (const auto &[t, q] : a)
      n++, bad += std::fabs(qty(r.s.asks(), t) - q) > 1e-12;
    for (const auto &[t, q] : r.s.bids())
      bad += t >= blo && !b.count(t);
    for (const auto &[t, q] : r.s.asks())
      bad += t <= ahi && !a.count(t);
    EXPECT_GT(n, 1000u);
    EXPECT_EQ(bad, 0u) << "snapshot u=" << target;
    EXPECT_EQ(r.s.stats().gaps, 0u);
    EXPECT_EQ(r.s.stats().crossed, 0u);
  }
}

// Linear BTCUSDT orderbook.50 with trades (DEPTH mode): a snapshot, then
// deltas with contiguous u, a 50-level book that never crosses.
TEST(BybitSyncRecorded, LinearDepth50Replays)
{
  const auto frames = load("linear_BTCUSDT_ob50.rec");
  ASSERT_FALSE(frames.empty());
  Rig r(BybitSync::Mode::DEPTH, "BTCUSDT", 0.1, 0.001, 0.1, 50);
  for (const auto &f : frames)
    ASSERT_TRUE(r.s.on_message(f.j));
  const auto &st = r.s.stats();
  EXPECT_EQ(st.snapshots, 1u);
  EXPECT_GT(st.deltas, 100u);
  EXPECT_EQ(st.gaps, 0u);
  EXPECT_EQ(st.crossed, 0u);
  EXPECT_LE(st.max_levels_bid, 50u);
  EXPECT_LE(st.max_levels_ask, 50u);
  EXPECT_GT(st.trades + st.prints_only, 0u);
  EXPECT_EQ(r.count<bfile::l3_mbp_trd_t>(), st.trades + st.prints_only);
  // Mbp2Mbo holds the same best prices as the sync.
  EXPECT_EQ(r.m.best(true), r.s.bids().rbegin()->first);
  EXPECT_EQ(r.m.best(false), r.s.asks().begin()->first);
}

// The same recording with one delta removed: a gap, reported to the caller
// (resubscribe) and as a GAP record; the next snapshot resynchronises.
TEST(BybitSyncRecorded, DepthGapInjection)
{
  auto frames = load("linear_BTCUSDT_ob50.rec");
  Rig r(BybitSync::Mode::DEPTH, "BTCUSDT", 0.1, 0.001, 0.1, 50);
  size_t deltas = 0;
  bool gap_seen = false;
  json snapshot;
  for (const auto &f : frames)
  {
    const auto topic = f.j.value("topic", std::string());
    if (topic.rfind("orderbook", 0) == 0 && f.j.value("type", std::string()) == "snapshot")
      snapshot = f.j;
    if (topic.rfind("orderbook", 0) == 0 && f.j.value("type", std::string()) == "delta" &&
        ++deltas == 20)
      continue; // dropped
    if (!r.s.on_message(f.j))
    {
      gap_seen = true;
      break;
    }
  }
  ASSERT_TRUE(gap_seen);
  EXPECT_EQ(r.s.stats().gaps, 1u);
  EXPECT_EQ(r.count<bfile::l3_gap_v2_t>(), 1u);
  EXPECT_FALSE(r.s.synced());
  ASSERT_TRUE(r.s.on_message(snapshot)); // a resubscription starts with a snapshot
  EXPECT_TRUE(r.s.synced());
}
