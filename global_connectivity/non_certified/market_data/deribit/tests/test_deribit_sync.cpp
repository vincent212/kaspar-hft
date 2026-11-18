/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitBookSync: the JSON-RPC book and trades, from the documentation's
// examples and from frames recorded from the live public feed
// (tests/data/btc_perpetual_100ms.frames, 2026-11-18, book.BTC-PERPETUAL.100ms
// and trades.BTC-PERPETUAL.100ms, unauthenticated).

#include <gtest/gtest.h>

#include <vector>

#include "deribit_md/DeribitBookSync.hpp"
#include "deribit_md/DeribitWs.hpp"
#include "deribit_md/FrameRecorder.hpp"
#include "enum/e_names.hpp"
#include "sim/Mbp2Mbo.hpp"

using nlohmann::json;
using deribit::DeribitBookSync;

namespace
{
  json note(const std::string &channel, json data)
  {
    return {{"jsonrpc", "2.0"}, {"method", "subscription"},
            {"params", {{"channel", channel}, {"data", std::move(data)}}}};
  }

  // The documentation's book example (book.(instrument_name).(interval),
  // AsyncAPI "Subscription Notification Data"). Its prices are on a 0.01 grid.
  json doc_snapshot()
  {
    return note("book.BTC-PERPETUAL.100ms", json::parse(R"({
      "type": "snapshot", "timestamp": 1554373962454, "instrument_name": "BTC-PERPETUAL",
      "change_id": 297217,
      "bids": [["new", 5042.34, 30], ["new", 5041.94, 20]],
      "asks": [["new", 5042.64, 40], ["new", 5043.3, 40]]})"));
  }

  json change(int64_t prev, int64_t id, json bids, json asks, uint64_t ts = 1554373962500)
  {
    return note("book.BTC-PERPETUAL.100ms",
                {{"type", "change"}, {"timestamp", ts}, {"instrument_name", "BTC-PERPETUAL"},
                 {"prev_change_id", prev}, {"change_id", id}, {"bids", bids}, {"asks", asks}});
  }

  struct Recorded
  {
    std::vector<bfile::l3_t> recs;
    sim::Mbp2Mbo m{sim::Mbp2Mbo::Config{7, char(en::x::DERIBIT), 0.01, 10},
                   [this](const bfile::l3_t &r) { recs.push_back(r); }};
  };
}

TEST(DeribitBookSync, DocumentationSnapshotBuildsTheBook)
{
  DeribitBookSync s("BTC-PERPETUAL", 0.01, nullptr, 50);
  EXPECT_TRUE(s.on_message(doc_snapshot()));
  ASSERT_TRUE(s.synced());
  EXPECT_EQ(s.change_id(), 297217);
  ASSERT_EQ(s.bids().size(), 2u);
  ASSERT_EQ(s.asks().size(), 2u);
  EXPECT_EQ(s.bids().rbegin()->first, 504234);
  EXPECT_DOUBLE_EQ(s.bids().rbegin()->second, 30);
  EXPECT_EQ(s.asks().begin()->first, 504264);
  EXPECT_EQ(s.stats().snapshots, 1u);
}

TEST(DeribitBookSync, ChangesAreAbsoluteAndDeleteRemoves)
{
  DeribitBookSync s("BTC-PERPETUAL", 0.01, nullptr, 50);
  s.on_message(doc_snapshot());
  EXPECT_TRUE(s.on_message(change(297217, 297218, json::parse(R"([["change", 5042.34, 10], ["delete", 5041.94, 0]])"),
                                  json::parse(R"([["new", 5042.5, 70]])"))));
  EXPECT_DOUBLE_EQ(s.bids().at(504234), 10);   // absolute, not 30 - 20
  EXPECT_EQ(s.bids().count(504194), 0u);
  EXPECT_DOUBLE_EQ(s.asks().at(504250), 70);
  EXPECT_EQ(s.stats().bad_actions, 0u);
  EXPECT_EQ(s.change_id(), 297218);
}

TEST(DeribitBookSync, BrokenChangeIdChainIsAGap)
{
  DeribitBookSync s("BTC-PERPETUAL", 0.01, nullptr, 50);
  s.on_message(doc_snapshot());
  EXPECT_FALSE(s.on_message(change(297219, 297220, json::array(), json::array())));
  EXPECT_EQ(s.stats().gaps, 1u);
  EXPECT_FALSE(s.synced());
  // Until the next snapshot, changes are ignored.
  EXPECT_TRUE(s.on_message(change(297220, 297221, json::parse(R"([["new", 1, 1]])"), json::array())));
  EXPECT_EQ(s.bids().count(100), 0u);
  EXPECT_TRUE(s.on_message(doc_snapshot()));
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(s.stats().snapshots, 2u);
}

TEST(DeribitBookSync, InconsistentActionsAreCounted)
{
  DeribitBookSync s("BTC-PERPETUAL", 0.01, nullptr, 50);
  s.on_message(doc_snapshot());
  s.on_message(change(297217, 297218, json::parse(R"([["new", 5042.34, 5], ["change", 5000, 1], ["delete", 4999, 0]])"),
                      json::array()));
  EXPECT_EQ(s.stats().bad_actions, 3u);
}

TEST(DeribitBookSync, CrossedBookIsCounted)
{
  DeribitBookSync s("BTC-PERPETUAL", 0.01, nullptr, 50);
  s.on_message(doc_snapshot());
  s.on_message(change(297217, 297218, json::parse(R"([["new", 5042.64, 5]])"), json::array()));
  EXPECT_EQ(s.stats().crossed, 1u);
}

// trades.(instrument_name).(interval) documentation example: direction is the
// taker's. The "sell" trade at 8950 hit a bid; the "buy" at 8960 lifted an ask.
TEST(DeribitBookSync, TradeDirectionIsTheTakers)
{
  Recorded r;
  DeribitBookSync s("BTC-PERPETUAL", 0.01, &r.m, 50);
  s.on_message(note("book.BTC-PERPETUAL.100ms", json::parse(R"({
      "type": "snapshot", "timestamp": 1590484512000, "instrument_name": "BTC-PERPETUAL",
      "change_id": 1, "bids": [["new", 8950, 100]], "asks": [["new", 8960, 100]]})")));
  const size_t bid_before = r.m.chunks(true, 895000), ask_before = r.m.chunks(false, 896000);
  ASSERT_EQ(bid_before, 10u);
  r.recs.clear();
  s.on_message(note("trades.BTC-PERPETUAL.100ms", json::parse(R"([
      {"trade_seq": 30289443, "trade_id": "48079270", "timestamp": 1590484513004, "tick_direction": 0,
       "price": 8960, "mark_price": 8948.9, "instrument_name": "BTC-PERPETUAL", "index_price": 8955.88,
       "direction": "buy", "amount": 20, "contracts": 2, "liquidation": "T"},
      {"trade_seq": 30289442, "trade_id": "48079269", "timestamp": 1590484512188, "tick_direction": 2,
       "price": 8950, "mark_price": 8948.9, "instrument_name": "BTC-PERPETUAL", "index_price": 8955.88,
       "direction": "sell", "amount": 10, "contracts": 1}])")));
  EXPECT_EQ(s.stats().trades, 2u);
  // 10 USD at the bid (one chunk), 20 USD at the ask (two chunks).
  EXPECT_EQ(r.m.chunks(true, 895000), bid_before - 1);
  EXPECT_EQ(r.m.chunks(false, 896000), ask_before - 2);
  // Applied oldest first (trade_seq), each an execution then a delete.
  size_t execs = 0;
  for (const auto &x : r.recs)
    execs += std::holds_alternative<bfile::l3_mbo_trd_v2_t>(x);
  EXPECT_EQ(execs, 3u);
}

TEST(DeribitBookSync, HistoryFindsTheBookAtAChangeId)
{
  DeribitBookSync s("BTC-PERPETUAL", 0.01, nullptr, 50);
  s.keep_history(5, 10);
  s.on_message(doc_snapshot());
  s.on_message(change(297217, 297230, json::parse(R"([["change", 5042.34, 1]])"), json::array()));
  ASSERT_NE(s.at_change_id(297217), nullptr);
  EXPECT_DOUBLE_EQ(s.at_change_id(297217)->bids[0].second, 30);
  EXPECT_DOUBLE_EQ(s.at_change_id(297230)->bids[0].second, 1);
  auto [before, after] = s.around(297220);
  ASSERT_TRUE(before && after);
  EXPECT_EQ(before->change_id, 297217);
  EXPECT_EQ(after->change_id, 297230);
}

TEST(DeribitWs, HeartbeatTestRequestIsAnswered)
{
  auto r = deribit::ws::heartbeat_reply(json::parse(R"({"jsonrpc":"2.0","method":"heartbeat","params":{"type":"test_request"}})"), 9);
  ASSERT_FALSE(r.is_null());
  EXPECT_EQ(r["method"], "public/test");
  EXPECT_TRUE(deribit::ws::heartbeat_reply(json::parse(R"({"method":"heartbeat","params":{"type":"heartbeat"}})"), 9).is_null());
  EXPECT_EQ(deribit::ws::channels("BTC-PERPETUAL", "100ms")[0], "book.BTC-PERPETUAL.100ms");
}

// The recorded live frames (two subscriptions: a first one that ends after its
// snapshot, then one minute of a second): an unbroken change_id chain, no
// inconsistent action, no crossed book, and Mbp2Mbo's window equal to the
// venue book's best levels after every notification.
TEST(DeribitBookSync, RecordedLiveFramesReplayCleanly)
{
  deribit::FrameReader rd(std::string(DERIBIT_TEST_DATA) + "/btc_perpetual_100ms.frames");
  ASSERT_TRUE(rd.ok());
  std::vector<bfile::l3_t> recs;
  sim::Mbp2Mbo m(sim::Mbp2Mbo::Config{7, char(en::x::DERIBIT), 0.5, 1000},
                 [&](const bfile::l3_t &r) { recs.push_back(r); });
  DeribitBookSync s("BTC-PERPETUAL", 0.5, &m, 50);
  uint64_t ns, frames = 0, window_checks = 0;
  std::string text;
  while (rd.next(ns, text))
  {
    ++frames;
    const auto msg = json::parse(text);
    ASSERT_TRUE(s.on_message(msg)) << "gap at frame " << frames;
    if (msg.value("method", "") == "subscription" &&
        msg["params"]["channel"].get<std::string>().rfind("book.", 0) == 0)
    {
      const auto t = s.top(50);
      for (const auto &[px, q] : t.bids)
        EXPECT_EQ(m.chunks(true, px), m.target(q));
      for (const auto &[px, q] : t.asks)
        EXPECT_EQ(m.chunks(false, px), m.target(q));
      ++window_checks;
    }
  }
  const auto &st = s.stats();
  EXPECT_EQ(frames, 249u);
  EXPECT_EQ(st.snapshots, 2u);
  EXPECT_EQ(st.notifications, 181u);
  EXPECT_EQ(st.gaps, 0u);
  EXPECT_EQ(st.bad_actions, 0u);
  EXPECT_EQ(st.unknown_action, 0u);
  EXPECT_EQ(st.crossed, 0u);
  EXPECT_GT(st.trades, 0u);
  EXPECT_EQ(window_checks, 183u);
  EXPECT_FALSE(recs.empty());
}
