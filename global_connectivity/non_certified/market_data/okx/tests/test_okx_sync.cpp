/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxSync from the OKX V5 documentation's examples and from frames recorded
// from the live public stream (tests/data, okx_record format).

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "okx_md/OkxSync.hpp"

using okx::md::OkxSync;
using json = nlohmann::json;

namespace
{
  constexpr const char *kRecording = "tests/data/okx_btc_spot_swap_20261118.rec";

  // The "Push Data Example: Full Snapshot" of the books channel (docs-v5,
  // "WS / Order book channel").
  const char *kDocSnapshot = R"({"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot","data":[{
    "asks":[["8476.98","415","0","13"],["8477","7","0","2"],["8477.34","85","0","1"],["8477.56","1","0","1"],
            ["8505.84","8","0","1"],["8506.37","85","0","1"],["8506.49","2","0","1"],["8506.96","100","0","2"]],
    "bids":[["8476.97","256","0","12"],["8475.55","101","0","1"],["8475.54","100","0","1"],["8475.3","1","0","1"],
            ["8447.32","6","0","1"],["8447.02","246","0","1"],["8446.83","24","0","1"],["8446","95","0","3"]],
    "ts":"1597026383085","checksum":0,"prevSeqId":-1,"seqId":123456}]})";

  json update(int64_t prev, int64_t seq, json bids, json asks, int64_t checksum = 0,
              const char *inst = "BTC-USDT", uint64_t ts = 1597026383185)
  {
    json d = {{"asks", asks}, {"bids", bids}, {"ts", std::to_string(ts)}, {"checksum", checksum},
              {"prevSeqId", prev}, {"seqId", seq}};
    json m = {{"arg", {{"channel", "books"}, {"instId", inst}}}, {"action", "update"}};
    m["data"] = json::array({d});
    return m;
  }

  json trade(int64_t trade_id, int64_t count, const char *px, const char *sz, const char *side, int64_t seq,
             uint64_t ts = 1597026383200, const char *inst = "BTC-USDT")
  {
    json d = {{"instId", inst}, {"tradeId", std::to_string(trade_id)}, {"px", px}, {"sz", sz}, {"side", side},
              {"ts", std::to_string(ts)}, {"count", std::to_string(count)}, {"source", "0"}, {"seqId", seq}};
    json m = {{"arg", {{"channel", "trades"}, {"instId", inst}}}};
    m["data"] = json::array({d});
    return m;
  }

  struct Rig
  {
    std::vector<bfile::l3_t> recs;
    sim::Mbp2Mbo m;
    OkxSync s;
    explicit Rig(const char *inst = "BTC-USDT", double tick = 0.01, double chunk = 1, size_t window = 400)
        : m(sim::Mbp2Mbo::Config{1, char(en::x::OKX), tick, chunk}, [this](const bfile::l3_t &r) { recs.push_back(r); }),
          s(inst, m, window)
    {
    }
    size_t count_mbo(uint8_t action, bool recovery_only = false) const
    {
      size_t n = 0;
      for (const auto &r : recs)
        if (auto *x = std::get_if<bfile::l3_mbo_v2_t>(&r))
          if (x->orderUpdateAction == action && (!recovery_only || x->recovery))
            ++n;
      return n;
    }
    size_t count_exec() const
    {
      size_t n = 0;
      for (const auto &r : recs)
        n += std::holds_alternative<bfile::l3_mbo_trd_v2_t>(r);
      return n;
    }
  };
}

// ---- checksum (docs-v5 2025 edition, "Calculate Checksum"; deprecated 2026-06-23) ----

TEST(OkxChecksum, StringFormatMatchesDocumentationExamples)
{
  // Example 1: more than 25 levels; bid and ask alternate.
  Rig r("BTC-USDT", 0.1);
  r.s.on_message(json::parse(R"({"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot","data":[{
    "bids":[["3366.1","7","0","3"],["3366","6","3","4"]],"asks":[["3366.8","9","10","3"],["3368","8","3","4"]],
    "ts":"1","checksum":0,"prevSeqId":-1,"seqId":1}]})"));
  EXPECT_EQ(r.s.checksum_string(), "3366.1:7:3366.8:9:3366:6:3368:8");

  // Example 2: fewer bids than asks; the missing bids are skipped.
  Rig r2("BTC-USDT", 0.1);
  r2.s.on_message(json::parse(R"({"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot","data":[{
    "bids":[["3366.1","7","0","3"]],"asks":[["3366.8","9","10","3"],["3368","8","3","4"],["3372","8","3","4"]],
    "ts":"1","checksum":0,"prevSeqId":-1,"seqId":1}]})"));
  EXPECT_EQ(r2.s.checksum_string(), "3366.1:7:3366.8:9:3368:8:3372:8");
}

TEST(OkxChecksum, Crc32IsZlibsAsSignedInt)
{
  EXPECT_EQ(OkxSync::crc32("123456789"), int32_t(0xCBF43926u)); // the CRC-32 check value
  // Python zlib.crc32(b"3366.1:7:3366.8:9:3366:6:3368:8") = 2413953002 -> signed
  EXPECT_EQ(OkxSync::crc32("3366.1:7:3366.8:9:3366:6:3368:8"), -1881014294);
}

TEST(OkxChecksum, UsesOnlyTop25AndStringsAsReceived)
{
  Rig r("BTC-USDT", 0.1);
  json bids = json::array(), asks = json::array();
  for (int i = 0; i < 30; ++i)
  {
    bids.push_back({std::to_string(100 - i) + ".0", "1.50", "0", "1"});
    asks.push_back({std::to_string(200 + i) + ".0", "2", "0", "1"});
  }
  json d = {{"bids", bids}, {"asks", asks}, {"ts", "1"}, {"checksum", 0}, {"prevSeqId", -1}, {"seqId", 1}};
  json msg = {{"arg", {{"channel", "books"}, {"instId", "BTC-USDT"}}}, {"action", "snapshot"}};
  msg["data"] = json::array({d});
  r.s.on_message(msg);
  const auto s = r.s.checksum_string();
  EXPECT_EQ(s.substr(0, 20), "100.0:1.50:200.0:2:9");
  EXPECT_NE(s.find("76.0:1.50:224.0:2"), std::string::npos);  // 25th level
  EXPECT_EQ(s.find("75.0"), std::string::npos);                // 26th level left out
}

TEST(OkxChecksum, NonZeroChecksumIsVerifiedAndAMismatchIsAGap)
{
  Rig r;
  r.s.on_message(json::parse(kDocSnapshot));
  // A correct non-zero checksum over the book after the update.
  Rig shadow;
  shadow.s.on_message(json::parse(kDocSnapshot));
  shadow.s.on_message(update(123456, 123457, json::array({{"8476.97", "200", "0", "10"}}), json::array()));
  const int32_t good = shadow.s.checksum();
  ASSERT_NE(good, 0);
  EXPECT_EQ(r.s.on_message(update(123456, 123457, json::array({{"8476.97", "200", "0", "10"}}), json::array(), good)),
            OkxSync::Status::Ok);
  EXPECT_EQ(r.s.stats().checksum_checked, 1u);
  EXPECT_EQ(r.s.on_message(update(123457, 123458, json::array({{"8476.97", "199", "0", "10"}}), json::array(), good)),
            OkxSync::Status::Gap);
  EXPECT_EQ(r.s.stats().checksum_mismatch, 1u);
  EXPECT_FALSE(r.s.synced());
}

// ---- books: documentation examples ------------------------------------------------------

TEST(OkxBooks, SnapshotThenDocumentedUpdateRules)
{
  Rig r;
  EXPECT_EQ(r.s.on_message(json::parse(kDocSnapshot)), OkxSync::Status::Ok);
  EXPECT_TRUE(r.s.synced());
  EXPECT_EQ(r.s.seq_id(), 123456);
  EXPECT_EQ(r.s.bids().size(), 8u);
  EXPECT_EQ(r.s.asks().size(), 8u);
  // Snapshot: SST(17) first, then recovery adds; 256 + ... chunks of size 1.
  ASSERT_TRUE(std::holds_alternative<bfile::l3_sst_t>(r.recs.front()));
  EXPECT_EQ(std::get<bfile::l3_sst_t>(r.recs.front()).tradingStatus, 17);
  EXPECT_EQ(r.count_mbo(0, true), size_t(256 + 101 + 100 + 1 + 6 + 246 + 24 + 95 + 415 + 7 + 85 + 1 + 8 + 85 + 2 + 100));

  // Rule 1: same price, size changes -> replace (absolute, not a delta).
  r.recs.clear();
  EXPECT_EQ(r.s.on_message(update(123456, 123457, json::array({{"8476.97", "250", "0", "12"}}), json::array())),
            OkxSync::Status::Ok);
  EXPECT_DOUBLE_EQ(r.s.bids().rbegin()->second.qty, 250);
  EXPECT_EQ(r.count_mbo(2), 6u); // 256 -> 250: six chunks leave the back
  // size 0 deletes the level.
  r.recs.clear();
  r.s.on_message(update(123457, 123458, json::array(), json::array({{"8477", "0", "0", "0"}})));
  EXPECT_EQ(r.s.asks().size(), 7u);
  EXPECT_EQ(r.count_mbo(2), 7u);
  // Rule 2: a new price is inserted in order.
  r.s.on_message(update(123458, 123459, json::array({{"8476.5", "3", "0", "1"}}), json::array()));
  auto it = r.s.bids().rbegin();
  ++it;
  EXPECT_EQ(it->second.px, "8476.5");
  EXPECT_EQ(r.s.stats().updates, 3u);
  EXPECT_EQ(r.s.stats().gaps, 0u);
}

TEST(OkxBooks, SequenceExamplesFromTheDocumentation)
{
  // "Snapshot prevSeqId=-1 seqId=10; update 10->15; no update 15->15;
  //  sequence reset 15->3; normal 3->5."
  Rig r;
  auto snap = json::parse(kDocSnapshot);
  snap["data"][0]["seqId"] = 10;
  ASSERT_EQ(r.s.on_message(snap), OkxSync::Status::Ok);
  EXPECT_EQ(r.s.on_message(update(10, 15, json::array({{"8476.97", "1", "0", "1"}}), json::array())), OkxSync::Status::Ok);
  EXPECT_EQ(r.s.on_message(update(15, 15, json::array(), json::array())), OkxSync::Status::Ok);
  EXPECT_EQ(r.s.on_message(update(15, 3, json::array({{"8476.97", "2", "0", "1"}}), json::array())), OkxSync::Status::Ok);
  EXPECT_EQ(r.s.on_message(update(3, 5, json::array({{"8476.97", "3", "0", "1"}}), json::array())), OkxSync::Status::Ok);
  EXPECT_EQ(r.s.stats().heartbeats, 1u);
  EXPECT_EQ(r.s.stats().seq_resets, 1u);
  EXPECT_EQ(r.s.stats().gaps, 0u);
  EXPECT_EQ(r.s.seq_id(), 5);
}

TEST(OkxBooks, GapStopsTheBookUntilANewSnapshot)
{
  Rig r;
  r.s.on_message(json::parse(kDocSnapshot));
  EXPECT_EQ(r.s.on_message(update(123455, 123457, json::array({{"8476.97", "1", "0", "1"}}), json::array())),
            OkxSync::Status::Gap);
  EXPECT_FALSE(r.s.synced());
  EXPECT_EQ(r.s.stats().gaps, 1u);
  // Further updates and trades are ignored until a snapshot arrives.
  r.recs.clear();
  EXPECT_EQ(r.s.on_message(update(123457, 123458, json::array({{"8476.97", "1", "0", "1"}}), json::array())),
            OkxSync::Status::Ignored);
  EXPECT_EQ(r.s.on_message(trade(5, 1, "8476.97", "1", "sell", 123459)), OkxSync::Status::Ignored);
  EXPECT_TRUE(r.recs.empty());
  // The new snapshot reaches OB as recovery records, priced like the first.
  auto snap = json::parse(kDocSnapshot);
  snap["data"][0]["seqId"] = 200000;
  EXPECT_EQ(r.s.on_message(snap), OkxSync::Status::Ok);
  EXPECT_TRUE(r.s.synced());
  EXPECT_GT(r.count_mbo(0, true), 0u);
  EXPECT_EQ(r.count_mbo(0, false), r.count_mbo(0, true)); // every add is a recovery add
}

TEST(OkxBooks, OtherInstrumentsChannelsAndEventsAreIgnored)
{
  Rig r;
  r.s.on_message(json::parse(kDocSnapshot));
  EXPECT_EQ(r.s.on_message(update(1, 2, json::array({{"1", "1", "0", "1"}}), json::array(), 0, "ETH-USDT")),
            OkxSync::Status::Ignored);
  EXPECT_EQ(r.s.on_message(json::parse(R"({"event":"subscribe","arg":{"channel":"books","instId":"BTC-USDT"},"connId":"a4d3ae55"})")),
            OkxSync::Status::Ignored);
  EXPECT_EQ(r.s.on_message(json::parse(R"({"arg":{"channel":"books5","instId":"BTC-USDT"},"data":[]})")),
            OkxSync::Status::Ignored);
  EXPECT_EQ(r.s.seq_id(), 123456);
}

// ---- trades ------------------------------------------------------------------------------

TEST(OkxTrades, SideIsTheTakersAndATradeAheadOfTheBookExecutesFromTheFront)
{
  Rig r;
  r.s.on_message(json::parse(kDocSnapshot));
  const auto front_bid = r.m.queue(true, r.m.ticks(8476.97))->front();
  r.recs.clear();
  // Taker sell: the resting bid is hit. seqId above the book's: not yet in it.
  EXPECT_EQ(r.s.on_message(trade(130639474, 3, "8476.97", "3", "sell", 123460)), OkxSync::Status::Ok);
  EXPECT_EQ(r.count_exec(), 3u);
  auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&r.recs[0]);
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->orderID, front_bid);
  EXPECT_EQ(r.m.chunks(true, r.m.ticks(8476.97)), 253u);
  EXPECT_EQ(r.s.stats().trades_before_book, 1u);
  // The update that contains the trade leaves the level at 253: nothing more.
  r.recs.clear();
  r.s.on_message(update(123456, 123461, json::array({{"8476.97", "253", "0", "12"}}), json::array()));
  EXPECT_EQ(r.count_mbo(2) + r.count_mbo(0), 0u);
  // Taker buy: the ask is lifted.
  r.recs.clear();
  r.s.on_message(trade(130639475, 1, "8476.98", "2", "buy", 123470));
  EXPECT_EQ(r.count_exec(), 2u);
  EXPECT_EQ(r.m.chunks(false, r.m.ticks(8476.98)), 413u);
  EXPECT_EQ(r.s.stats().trade_id_gaps, 0u);
}

TEST(OkxTrades, ATradeAlreadyInTheBookKeepsTheLevelSize)
{
  Rig r;
  r.s.on_message(json::parse(kDocSnapshot));
  r.s.on_message(update(123456, 123500, json::array({{"8476.97", "250", "0", "12"}}), json::array()));
  r.recs.clear();
  // seqId 123490 <= 123500: the update already took the 6 off the level.
  r.s.on_message(trade(10, 1, "8476.97", "6", "sell", 123490));
  EXPECT_EQ(r.count_exec(), 6u);
  EXPECT_EQ(r.count_mbo(0), 6u); // re-added at the back
  EXPECT_EQ(r.m.chunks(true, r.m.ticks(8476.97)), 250u);
  EXPECT_EQ(r.s.stats().trades_in_book, 1u);
}

TEST(OkxTrades, TradeIdContinuityUsesCount)
{
  Rig r;
  r.s.on_message(json::parse(kDocSnapshot));
  r.s.on_message(trade(100, 1, "8476.97", "1", "sell", 123457));
  r.s.on_message(trade(103, 3, "8476.97", "1", "sell", 123458)); // 101..103
  EXPECT_EQ(r.s.stats().trade_id_gaps, 0u);
  r.s.on_message(trade(110, 2, "8476.97", "1", "sell", 123459)); // 104..108 missing
  EXPECT_EQ(r.s.stats().trade_id_gaps, 1u);
}

// ---- recorded frames ---------------------------------------------------------------------

namespace
{
  struct Line
  {
    char kind;
    std::string inst, body;
  };

  std::vector<Line> load()
  {
    std::vector<Line> out;
    std::ifstream f(kRecording);
    std::string l;
    while (std::getline(f, l))
    {
      if (l.size() < 3)
        continue;
      const size_t sp = l.find(' ', 2);
      Line x{l[0], "", l.substr(sp + 1)};
      if (x.kind == 'R')
      {
        const size_t sp2 = x.body.find(' ');
        x.inst = x.body.substr(0, sp2);
        x.body = x.body.substr(sp2 + 1);
      }
      out.push_back(std::move(x));
    }
    return out;
  }

  // Replays the recording for one instrument; for every REST snapshot whose
  // seqId the stream also delivered, compares the whole book at that seqId.
  void replay(const char *inst, double chunk, size_t &exact, size_t &levels)
  {
    Rig r(inst, 0.1, chunk, 50);
    std::map<int64_t, std::pair<std::vector<std::pair<int64_t, double>>, std::vector<std::pair<int64_t, double>>>> hist;
    r.s.on_book = [&](int64_t seq) {
      auto &h = hist[seq];
      h.first.clear();
      h.second.clear();
      for (auto it = r.s.bids().rbegin(); it != r.s.bids().rend(); ++it)
        h.first.emplace_back(it->first, it->second.qty);
      for (const auto &[t, lv] : r.s.asks())
        h.second.emplace_back(t, lv.qty);
    };
    std::vector<Line> rest;
    const auto lines = load();
    ASSERT_FALSE(lines.empty()) << "cannot read " << kRecording;
    for (const auto &l : lines)
    {
      if (l.kind == 'W')
      {
        if (l.body == "pong")
          continue;
        ASSERT_NE(r.s.on_message(json::parse(l.body)), OkxSync::Status::Gap);
      }
      else if (l.kind == 'R' && l.inst == inst)
        rest.push_back(l);
    }
    const auto &st = r.s.stats();
    EXPECT_EQ(st.snapshots, 1u);
    EXPECT_GT(st.updates, 50u);
    EXPECT_EQ(st.gaps, 0u);
    EXPECT_EQ(st.checksum_checked, 0u);                 // OKX sends 0 since 2026-06-23
    EXPECT_EQ(st.checksum_zero, st.snapshots + st.updates);
    EXPECT_EQ(st.crossed, 0u);
    EXPECT_EQ(st.max_bids, 400u);
    EXPECT_EQ(st.max_asks, 400u);
    EXPECT_EQ(st.trade_id_gaps, 0u);
    EXPECT_GT(st.trades, 0u);
    for (const auto &l : rest)
    {
      const auto d = json::parse(l.body)["data"][0];
      auto h = hist.find(d["seqId"].get<int64_t>());
      if (h == hist.end())
        continue;
      ++exact;
      std::vector<std::pair<int64_t, double>> b, a;
      for (const auto &x : d["bids"])
        b.emplace_back(r.m.ticks(std::stod(x[0].get<std::string>())), std::stod(x[1].get<std::string>()));
      for (const auto &x : d["asks"])
        a.emplace_back(r.m.ticks(std::stod(x[0].get<std::string>())), std::stod(x[1].get<std::string>()));
      EXPECT_EQ(b, h->second.first) << inst << " bids at seqId " << h->first;
      EXPECT_EQ(a, h->second.second) << inst << " asks at seqId " << h->first;
      levels += b.size() + a.size();
    }
  }
}

TEST(OkxRecorded, SpotReplaysWithoutGapsAndMatchesRestAtTheSameSeqId)
{
  size_t exact = 0, levels = 0;
  replay("BTC-USDT", 0.01, exact, levels);
  EXPECT_GE(exact, 1u);
  EXPECT_EQ(levels, exact * 800);
}

TEST(OkxRecorded, SwapReplaysWithoutGapsAndMatchesRestAtTheSameSeqId)
{
  size_t exact = 0, levels = 0;
  replay("BTC-USDT-SWAP", 1.0, exact, levels);
  EXPECT_GE(exact, 3u);
  EXPECT_EQ(levels, exact * 800);
}

TEST(OkxRecorded, InjectedGapAndDuplicateAreDetected)
{
  Rig r("BTC-USDT", 0.1, 0.01, 50);
  int books = 0;
  bool dup_done = false, gap_done = false;
  json last_update;
  for (const auto &l : load())
  {
    if (l.kind != 'W' || l.body == "pong")
      continue;
    auto m = json::parse(l.body);
    if (m.contains("event") || m["arg"]["instId"] != "BTC-USDT" || m["arg"]["channel"] != "books")
      continue;
    ++books;
    if (books == 20 && !dup_done)
    {
      // A duplicate of the previous update: its prevSeqId no longer matches.
      EXPECT_EQ(r.s.on_message(last_update), OkxSync::Status::Gap);
      dup_done = true;
      // The recovery: a snapshot comes from a new subscription; here the
      // next frames are ignored until one would arrive.
      EXPECT_EQ(r.s.on_message(m), OkxSync::Status::Ignored);
      continue;
    }
    if (books > 20 && !gap_done)
    {
      gap_done = true;
      EXPECT_FALSE(r.s.synced());
    }
    r.s.on_message(m);
    if (m.value("action", "") == "update")
      last_update = m;
  }
  EXPECT_TRUE(dup_done);
  EXPECT_EQ(r.s.stats().gaps, 1u);
}
