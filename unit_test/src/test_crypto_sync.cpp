/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * LevelWindow, CoinbaseSync and HyperliquidSync, driven with messages shaped
 * as Coinbase Advanced Trade and Hyperliquid send them (captured 2026-11-17).
 * No network.
 */

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "sim/CoinbaseSync.hpp"
#include "sim/HyperliquidSync.hpp"
#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace {

using nlohmann::json;

class CryptoSyncTest : public ::testing::Test {
protected:
  std::vector<bfile::l3_t> out;
  // tick 0.01, chunk 0.01
  sim::Mbp2Mbo m{{7, char(en::x::COINBASE), 0.01, 0.01},
                 [this](const bfile::l3_t &r) { out.push_back(r); }};

  size_t count_mbo(int action, bool recovery = false) const {
    size_t n = 0;
    for (const auto &r : out)
      if (auto *p = std::get_if<bfile::l3_mbo_v2_t>(&r);
          p && p->orderUpdateAction == action && p->recovery == recovery)
        ++n;
    return n;
  }
  size_t execs() const {
    size_t n = 0;
    for (const auto &r : out)
      if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(r)) ++n;
    return n;
  }
};

// ---------------------------------------------------------------- LevelWindow

TEST_F(CryptoSyncTest, WindowKeepsOnlyTheBestLevels) {
  sim::LevelWindow w(m, 2);
  w.set(true, 100.00, 0.01);
  w.set(true, 99.99, 0.02);
  w.set(true, 99.98, 0.03);   // third bid: outside a window of 2
  w.set(false, 100.01, 0.01);
  w.resync(1);
  m.end_event();

  EXPECT_EQ(m.chunks(true, 10000), 1u);
  EXPECT_EQ(m.chunks(true, 9999), 2u);
  EXPECT_EQ(m.chunks(true, 9998), 0u) << "below the window";
  EXPECT_EQ(count_mbo(0, true), 4u) << "the window goes out as recovery";
}

TEST_F(CryptoSyncTest, WindowFlushSendsOnlyWhatChanged) {
  sim::LevelWindow w(m, 2);
  w.set(true, 100.00, 0.01);
  w.set(true, 99.99, 0.02);
  w.set(true, 99.98, 0.03);
  w.resync(1);
  m.end_event();
  out.clear();

  w.set(true, 99.99, 0.02);   // same size: nothing to send
  w.flush(2);
  m.end_event();
  EXPECT_EQ(count_mbo(0) + count_mbo(2), 0u);

  w.set(true, 100.00, 0);     // the best bid goes: 99.98 enters the window
  w.flush(3);
  m.end_event();
  EXPECT_EQ(m.chunks(true, 10000), 0u);
  EXPECT_EQ(m.chunks(true, 9998), 3u) << "a level entering the window is added";
  EXPECT_EQ(count_mbo(2), 1u);
  EXPECT_EQ(count_mbo(0), 3u);
}

TEST_F(CryptoSyncTest, WindowLevelLeavingIsRemoved) {
  sim::LevelWindow w(m, 1);
  w.set(false, 100.01, 0.02);
  w.resync(1);
  m.end_event();
  w.set(false, 100.00, 0.01);   // a better ask pushes 100.01 out of a window of 1
  w.flush(2);
  m.end_event();
  EXPECT_EQ(m.chunks(false, 10000), 1u);
  EXPECT_EQ(m.chunks(false, 10001), 0u);
}

// --------------------------------------------------------------- CoinbaseSync

json cb_l2(int64_t seq, const std::string &type, json updates) {
  return {{"channel", "l2_data"}, {"timestamp", "2026-11-17T13:59:47.160806Z"},
          {"sequence_num", seq},
          {"events", json::array({{{"type", type}, {"product_id", "BTC-USD"}, {"updates", updates}}})}};
}

json cb_upd(const std::string &side, const std::string &px, const std::string &q,
            const std::string &t = "2026-11-17T13:59:46.893523Z") {
  return {{"side", side}, {"event_time", t}, {"price_level", px}, {"new_quantity", q}};
}

json cb_trades(int64_t seq, const std::string &type, json trades) {
  return {{"channel", "market_trades"}, {"timestamp", "2026-11-17T13:59:48.0Z"},
          {"sequence_num", seq}, {"events", json::array({{{"type", type}, {"trades", trades}}})}};
}

json cb_trade(const std::string &id, const std::string &px, const std::string &sz,
              const std::string &side, const std::string &t = "2026-11-17T13:59:48.261708Z") {
  return {{"product_id", "BTC-USD"}, {"trade_id", id}, {"price", px},
          {"size", sz}, {"time", t}, {"side", side}};
}

TEST(CoinbaseTime, ParsesMicrosAndNanos) {
  EXPECT_EQ(sim::CoinbaseSync::parse_time("2026-11-17T13:59:46.893523Z"), 1794923986893523000ull);
  EXPECT_EQ(sim::CoinbaseSync::parse_time("2026-11-17T13:59:47.194271922Z"), 1794923987194271922ull);
  EXPECT_EQ(sim::CoinbaseSync::parse_time("2026-11-17T13:59:47Z"), 1794923987000000000ull);
  EXPECT_EQ(sim::CoinbaseSync::parse_time("garbage"), 0ull);
}

TEST_F(CryptoSyncTest, CoinbaseSnapshotStartsTheBook) {
  sim::CoinbaseSync s("BTC-USD", m, 1000);
  EXPECT_TRUE(s.on_message(cb_l2(0, "snapshot",
      json::array({cb_upd("bid", "65049.01", "0.02"), cb_upd("offer", "65049.02", "0.03")}))));
  ASSERT_FALSE(out.empty());
  EXPECT_NE(std::get_if<bfile::l3_sst_t>(&out[0]), nullptr);
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(m.chunks(true, 6504901), 2u);
  EXPECT_EQ(m.chunks(false, 6504902), 3u);
  EXPECT_EQ(count_mbo(0, true), 5u);
}

TEST_F(CryptoSyncTest, CoinbaseUpdatesBeforeTheSnapshotAreIgnored) {
  sim::CoinbaseSync s("BTC-USD", m, 1000);
  EXPECT_TRUE(s.on_message(cb_l2(0, "update", json::array({cb_upd("bid", "65049.01", "0.02")}))));
  EXPECT_FALSE(s.synced());
  EXPECT_TRUE(out.empty());
}

TEST_F(CryptoSyncTest, CoinbaseUpdateIsAbsoluteAndZeroRemoves) {
  sim::CoinbaseSync s("BTC-USD", m, 1000);
  s.on_message(cb_l2(0, "snapshot", json::array({cb_upd("bid", "65049.01", "0.02")})));
  s.on_message(cb_l2(1, "update", json::array({cb_upd("bid", "65049.01", "0.05"),
                                               cb_upd("offer", "65049.05", "0.01")})));
  EXPECT_EQ(m.chunks(true, 6504901), 5u);
  EXPECT_EQ(m.chunks(false, 6504905), 1u);
  s.on_message(cb_l2(2, "update", json::array({cb_upd("bid", "65049.01", "0")})));
  EXPECT_EQ(m.chunks(true, 6504901), 0u);
  EXPECT_EQ(s.events_applied(), 2u);
}

TEST_F(CryptoSyncTest, CoinbaseSequenceGapAsksForAReconnect) {
  sim::CoinbaseSync s("BTC-USD", m, 1000);
  s.on_message(cb_l2(0, "snapshot", json::array({cb_upd("bid", "65049.01", "0.02")})));
  EXPECT_TRUE(s.on_message(cb_l2(1, "update", json::array())));
  EXPECT_FALSE(s.on_message(cb_l2(3, "update", json::array())));   // 2 missing
  EXPECT_FALSE(s.synced());
  EXPECT_EQ(s.gaps(), 1u);

  s.reset();
  out.clear();
  EXPECT_TRUE(s.on_message(cb_l2(0, "snapshot", json::array({cb_upd("bid", "65049.01", "0.04")}))));
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(count_mbo(0, true), 4u) << "the new snapshot goes to OB as recovery";
}

// Coinbase's market_trades side is the maker's: SELL prints at the ask.
TEST_F(CryptoSyncTest, CoinbaseTradeSideIsTheMakers) {
  sim::CoinbaseSync s("BTC-USD", m, 1000);
  s.on_message(cb_l2(0, "snapshot", json::array({cb_upd("bid", "65049.01", "0.03"),
                                                 cb_upd("offer", "65049.02", "0.03")})));
  out.clear();
  s.on_message(cb_trades(1, "update", json::array({cb_trade("10", "65049.02", "0.01", "SELL")})));
  EXPECT_EQ(execs(), 1u);
  EXPECT_EQ(m.chunks(false, 6504902), 2u) << "SELL: the resting ask was hit";
  EXPECT_EQ(m.chunks(true, 6504901), 3u);

  s.on_message(cb_trades(2, "update", json::array({cb_trade("11", "65049.01", "0.01", "BUY")})));
  EXPECT_EQ(m.chunks(true, 6504901), 2u) << "BUY: the resting bid was hit";
  EXPECT_EQ(s.trades_applied(), 2u);
}

TEST_F(CryptoSyncTest, CoinbaseTradeSnapshotIsSkippedAndTradesApplyOldestFirst) {
  sim::CoinbaseSync s("BTC-USD", m, 1000);
  s.on_message(cb_l2(0, "snapshot", json::array({cb_upd("offer", "65049.02", "0.05")})));
  s.on_message(cb_trades(1, "snapshot", json::array({cb_trade("5", "65049.02", "0.01", "SELL")})));
  EXPECT_EQ(s.trades_applied(), 0u) << "the subscribe replay is history";

  // Newest first on the wire, as Coinbase sends them.
  out.clear();
  s.on_message(cb_trades(2, "update", json::array({
      cb_trade("12", "65049.02", "0.01", "SELL", "2026-11-17T13:59:48.300000Z"),
      cb_trade("11", "65049.02", "0.01", "SELL", "2026-11-17T13:59:48.200000Z")})));
  ASSERT_EQ(execs(), 2u);
  uint64_t prev = 0;
  for (const auto &r : out)
    if (auto *x = std::get_if<bfile::l3_mbo_trd_v2_t>(&r)) {
      EXPECT_GT(x->transactTime, prev) << "trade 11 before trade 12";
      prev = x->transactTime;
    }
}

// ------------------------------------------------------------ HyperliquidSync

json hl_book(uint64_t ms, json bids, json asks) {
  return {{"channel", "l2Book"},
          {"data", {{"coin", "BTC"}, {"time", ms}, {"levels", json::array({bids, asks})}}}};
}
json hl_lv(const std::string &px, const std::string &sz) {
  return {{"px", px}, {"sz", sz}, {"n", 1}};
}
json hl_trades(json trades) { return {{"channel", "trades"}, {"data", trades}}; }
json hl_trade(uint64_t ms, const std::string &side, const std::string &px, const std::string &sz) {
  return {{"coin", "BTC"}, {"side", side}, {"px", px}, {"sz", sz}, {"time", ms}, {"tid", 1}};
}

class HyperliquidTest : public ::testing::Test {
protected:
  std::vector<bfile::l3_t> out;
  // tick 1, chunk 0.01
  sim::Mbp2Mbo m{{3, char(en::x::HYPERLIQUID), 1.0, 0.01},
                 [this](const bfile::l3_t &r) { out.push_back(r); }};
  size_t execs() const {
    size_t n = 0;
    for (const auto &r : out)
      if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(r)) ++n;
    return n;
  }
};

TEST_F(HyperliquidTest, FirstBookIsTheSnapshotLaterBooksAreDiffs) {
  sim::HyperliquidSync s("BTC", m);
  s.on_message(hl_book(1000, json::array({hl_lv("65094.0", "0.03"), hl_lv("65093.0", "0.02")}),
                       json::array({hl_lv("65095.0", "0.01")})));
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(m.chunks(true, 65094), 3u);
  EXPECT_EQ(m.chunks(true, 65093), 2u);

  const auto q = *m.queue(true, 65094);
  s.on_message(hl_book(1500, json::array({hl_lv("65094.0", "0.03"), hl_lv("65092.0", "0.01")}),
                       json::array({hl_lv("65095.0", "0.01")})));
  EXPECT_EQ(*m.queue(true, 65094), q) << "an unchanged level keeps its queue";
  EXPECT_EQ(m.chunks(true, 65093), 0u) << "a level missing from the book is gone";
  EXPECT_EQ(m.chunks(true, 65092), 1u);
  EXPECT_EQ(s.events_applied(), 1u);
}

// Hyperliquid's trade side is the taker's: "A" sold into the bid.
TEST_F(HyperliquidTest, TradeSideIsTheTakers) {
  sim::HyperliquidSync s("BTC", m);
  s.on_message(hl_book(1000, json::array({hl_lv("65094.0", "0.03")}),
                       json::array({hl_lv("65095.0", "0.03")})));
  s.on_message(hl_trades(json::array({hl_trade(1100, "A", "65094.0", "0.01")})));
  EXPECT_EQ(m.chunks(true, 65094), 2u) << "A: the resting bid was hit";
  s.on_message(hl_trades(json::array({hl_trade(1200, "B", "65095.0", "0.02")})));
  EXPECT_EQ(m.chunks(false, 65095), 1u) << "B: the resting ask was hit";
  EXPECT_EQ(execs(), 3u);
}

TEST_F(HyperliquidTest, TradesReplayedOnSubscribeAreSkipped) {
  sim::HyperliquidSync s("BTC", m);
  s.on_message(hl_trades(json::array({hl_trade(900, "A", "65094.0", "0.01")})));   // before any book
  s.on_message(hl_book(1000, json::array({hl_lv("65094.0", "0.03")}), json::array()));
  s.on_message(hl_trades(json::array({hl_trade(950, "A", "65094.0", "0.01")})));   // older than the book
  EXPECT_EQ(s.trades_applied(), 0u);
  EXPECT_EQ(m.chunks(true, 65094), 3u);
}

TEST_F(HyperliquidTest, OtherCoinsAreIgnored) {
  sim::HyperliquidSync s("BTC", m);
  json eth = hl_book(1000, json::array({hl_lv("1827.0", "1")}), json::array());
  eth["data"]["coin"] = "ETH";
  s.on_message(eth);
  EXPECT_FALSE(s.synced());
  EXPECT_TRUE(out.empty());
}

}  // namespace
