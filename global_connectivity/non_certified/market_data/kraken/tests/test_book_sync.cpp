/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenBookSync: the documentation's snapshot and checksum, recorded frames
// from the live public stream, and synthetic sequences for each rule.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "enum/e_names.hpp"
#include "kraken_md/FrameRecorder.hpp"
#include "kraken_md/KrakenBookSync.hpp"
#include "kraken_md/KrakenJson.hpp"

using namespace kraken;
using json = nlohmann::json;
using R = KrakenBookSync::Result;

namespace
{
  // book-checksum-v2, "Example": the snapshot, expected checksum 3310070434.
  const char *kDocSnapshot = R"({
  "channel": "book", "type": "snapshot",
  "data": [{
    "symbol": "BTC/USD",
    "bids": [
      {"price": "45283.5", "qty": "0.10000000"}, {"price": "45283.4", "qty": "1.54582015"},
      {"price": "45282.1", "qty": "0.10000000"}, {"price": "45281.0", "qty": "0.10000000"},
      {"price": "45280.3", "qty": "1.54592586"}, {"price": "45279.0", "qty": "0.07990000"},
      {"price": "45277.6", "qty": "0.03310103"}, {"price": "45277.5", "qty": "0.30000000"},
      {"price": "45277.3", "qty": "1.54602737"}, {"price": "45276.6", "qty": "0.15445238"}],
    "asks": [
      {"price": "45285.2", "qty": "0.00100000"}, {"price": "45286.4", "qty": "1.54571953"},
      {"price": "45286.6", "qty": "1.54571109"}, {"price": "45289.6", "qty": "1.54560911"},
      {"price": "45290.2", "qty": "0.15890660"}, {"price": "45291.8", "qty": "1.54553491"},
      {"price": "45294.7", "qty": "0.04454749"}, {"price": "45296.1", "qty": "0.35380000"},
      {"price": "45297.5", "qty": "0.09945542"}, {"price": "45299.5", "qty": "0.18772827"}],
    "checksum": 3310070434
  }]})";

  json msg(const std::string &s) { return parse_raw(s); }



  struct Capture
  {
    std::vector<bfile::l3_t> recs;
    sim::Mbp2Mbo m{sim::Mbp2Mbo::Config{7, char(en::x::KRAKEN), 0.1, 0.001},
                   [this](const bfile::l3_t &r) { recs.push_back(r); }};
  };
}

TEST(KrakenBookSync, DocumentationSnapshotChecksum)
{
  KrakenBookSync s("BTC/USD", 10);
  EXPECT_EQ(s.on_message(msg(kDocSnapshot)), R::Ok);
  EXPECT_TRUE(s.synced());
  EXPECT_EQ(s.stats().checksum_ok, 1u);
  EXPECT_EQ(s.checksum(), 3310070434u);
  auto b = s.top(true, 1), a = s.top(false, 1);
  EXPECT_EQ(b[0].first, 452835000000000LL);
  EXPECT_EQ(a[0].first, 452852000000000LL);
}

TEST(KrakenBookSync, ChecksumMismatchIsReported)
{
  KrakenBookSync s("BTC/USD", 10);
  std::string bad = kDocSnapshot;
  bad.replace(bad.find("3310070434"), 10, "3310070435");
  EXPECT_EQ(s.on_message(msg(bad)), R::ChecksumMismatch);
  EXPECT_FALSE(s.synced());
  EXPECT_EQ(s.stats().checksum_bad, 1u);
}

TEST(KrakenBookSync, UpdatesApplyInOrderAndZeroRemoves)
{
  KrakenBookSync s("BTC/USD", 10);
  ASSERT_EQ(s.on_message(msg(kDocSnapshot)), R::Ok);
  // Two updates of one level in one message: the second wins. Then remove
  // the best bid. Checksum: computed from the book we expect.
  KrakenBookSync ref("BTC/USD", 10);
  ref.on_message(msg(kDocSnapshot));
  std::string upd = R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",
     "bids":[{"price":45283.5,"qty":0.0}],
     "asks":[{"price":45285.2,"qty":1.00000000},{"price":45285.2,"qty":0.50000000}],
     "checksum":CK,"timestamp":"2026-11-18T18:24:44.265032Z"}]})";
  // Expected book: bids without 45283.5, ask 45285.2 = 0.50000000.
  auto bids = s.bids(), asks = s.asks();
  int64_t p;
  to_fixed("45283.5", p);
  bids.erase(p);
  to_fixed("45285.2", p);
  asks[p].qty = "0.50000000";
  const uint32_t ck = KrakenBookSync::checksum(bids, asks);
  upd.replace(upd.find("CK"), 2, std::to_string(ck));
  EXPECT_EQ(s.on_message(msg(upd)), R::Ok);
  EXPECT_EQ(s.bids().size(), 9u);
  EXPECT_EQ(s.top(false, 1)[0].second, 5'000'000'000LL);
  EXPECT_EQ(s.stats().checksum_ok, 2u);
}

TEST(KrakenBookSync, TruncatesToTheSubscribedDepth)
{
  // Depth 10: a new best bid pushes the 10th bid out; Kraken sends no qty 0
  // for it. The checksum covers the top 10, which the truncation leaves.
  KrakenBookSync s("BTC/USD", 10);
  ASSERT_EQ(s.on_message(msg(kDocSnapshot)), R::Ok);
  auto bids = s.bids();
  int64_t p, worst;
  to_fixed("45284.0", p);
  to_fixed("45276.6", worst);
  bids[p] = {"45284.0", "0.20000000", 2'000'000'000LL};
  bids.erase(worst);
  std::string upd = R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD",
     "bids":[{"price":45284.0,"qty":0.20000000}],"asks":[],"checksum":)" +
                    std::to_string(KrakenBookSync::checksum(bids, s.asks())) + "}]}";
  EXPECT_EQ(s.on_message(msg(upd)), R::Ok);
  EXPECT_EQ(s.bids().size(), 10u);
  EXPECT_EQ(s.bids().count(worst), 0u);
  EXPECT_EQ(s.stats().truncated, 1u);
}

TEST(KrakenBookSync, OtherSymbolsAndMethodResponsesAreIgnored)
{
  KrakenBookSync s("BTC/USD", 10);
  EXPECT_EQ(s.on_message(msg(R"({"method":"subscribe","result":{"channel":"book"},"success":true})")), R::Ok);
  EXPECT_EQ(s.on_message(msg(R"({"channel":"heartbeat"})")), R::Ok);
  EXPECT_EQ(s.on_message(msg(R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[],"asks":[],"checksum":1}]})")),
            R::Ok); // before the snapshot: ignored
  EXPECT_FALSE(s.synced());
  std::string eth = kDocSnapshot;
  eth.replace(eth.find("BTC/USD"), 7, "ETH/USD");
  EXPECT_EQ(s.on_message(msg(eth)), R::Ok);
  EXPECT_FALSE(s.synced());
  EXPECT_EQ(s.stats().heartbeats, 1u);
}

TEST(KrakenBookSync, MalformedLevelIsAnError)
{
  KrakenBookSync s("BTC/USD", 10);
  std::string bad = kDocSnapshot;
  bad.replace(bad.find("\"45283.5\""), 9, "\"abc\"");
  EXPECT_EQ(s.on_message(msg(bad)), R::Error);
  EXPECT_FALSE(s.synced());
}

TEST(KrakenBookSync, TradeSideIsTheTakersAndTradeIdsAreChecked)
{
  Capture c;
  KrakenBookSync s("BTC/USD", 10, &c.m, 10);
  ASSERT_EQ(s.on_message(msg(kDocSnapshot)), R::Ok);
  // Best bid 45283.5 holds 0.1 = 100 chunks of 0.001.
  ASSERT_EQ(c.m.chunks(true, c.m.ticks(45283.5)), 100u);
  // A taker sell of 0.002 at the bid executes 2 chunks from the bid.
  s.on_message(msg(R"({"channel":"trade","type":"update","data":[{"symbol":"BTC/USD","side":"sell","price":45283.5,"qty":0.00200000,"ord_type":"market","trade_id":10,"timestamp":"2026-11-18T18:27:54.074804Z"}]})"));
  EXPECT_EQ(c.m.chunks(true, c.m.ticks(45283.5)), 98u);
  EXPECT_EQ(c.m.chunks(false, c.m.ticks(45285.2)), 1u);
  // A taker buy at the ask takes from the ask. trade_id 12 after 10: one gap.
  s.on_message(msg(R"({"channel":"trade","type":"update","data":[{"symbol":"BTC/USD","side":"buy","price":45285.2,"qty":0.00100000,"ord_type":"limit","trade_id":12,"timestamp":"2026-11-18T18:27:54.174804Z"}]})"));
  EXPECT_EQ(c.m.chunks(false, c.m.ticks(45285.2)), 0u);
  EXPECT_EQ(s.stats().trades, 2u);
  EXPECT_EQ(s.stats().trade_id_gaps, 1u);
  // A repeat of trade 12 is not applied twice.
  s.on_message(msg(R"({"channel":"trade","type":"update","data":[{"symbol":"BTC/USD","side":"buy","price":45285.2,"qty":0.00100000,"trade_id":12,"timestamp":"2026-11-18T18:27:54.174804Z"}]})"));
  EXPECT_EQ(s.stats().trades, 2u);
  // The trade snapshot (last 50 trades) is never applied.
  s.on_message(msg(R"({"channel":"trade","type":"snapshot","data":[{"symbol":"BTC/USD","side":"buy","price":45285.2,"qty":5.0,"trade_id":3,"timestamp":"2026-11-18T18:27:50Z"}]})"));
  EXPECT_EQ(s.stats().trades, 2u);
}

TEST(KrakenBookSync, SnapshotReachesMbp2MboAsRecoveryRecords)
{
  Capture c;
  KrakenBookSync s("BTC/USD", 10, &c.m, 10);
  ASSERT_EQ(s.on_message(msg(kDocSnapshot)), R::Ok);
  size_t rec = 0, sst = 0;
  for (const auto &r : c.recs)
  {
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&r))
      rec += m->recovery ? 1 : 0;
    if (std::holds_alternative<bfile::l3_sst_t>(r))
      ++sst;
  }
  EXPECT_EQ(sst, 1u);
  EXPECT_GT(rec, 0u);
  EXPECT_EQ(c.m.best(true), c.m.ticks(45283.5));
  EXPECT_EQ(c.m.best(false), c.m.ticks(45285.2));
}

// Frames recorded from wss://ws.kraken.com/v2 (book depth 1000 and trade,
// BTC/USD, 2026-11-18): every checksum must match.
TEST(KrakenBookSync, RecordedLiveFramesAllChecksumsMatch)
{
  FrameReader in("data/btcusd_book1000_trade.txt");
  ASSERT_TRUE(in.ok()) << "run from the tests directory";
  Capture c;
  KrakenBookSync s("BTC/USD", 1000, &c.m, 1000);
  uint64_t t;
  std::string f;
  size_t frames = 0;
  while (in.next(t, f))
  {
    ++frames;
    ASSERT_EQ(s.on_message(parse_raw(f)), R::Ok) << "frame " << frames << ": " << f.substr(0, 200);
  }
  const auto &st = s.stats();
  EXPECT_EQ(st.snapshots, 1u);
  EXPECT_GT(st.updates, 1500u);
  EXPECT_EQ(st.checksum_ok, st.updates + st.snapshots);
  EXPECT_EQ(st.checksum_bad, 0u);
  EXPECT_EQ(st.crossed, 0u);
  EXPECT_EQ(s.bids().size(), 1000u);
  EXPECT_EQ(s.asks().size(), 1000u);
  EXPECT_EQ(st.trade_id_gaps, 0u);
  EXPECT_GT(st.trades, 0u);
  EXPECT_EQ(c.m.best(true), c.m.ticks(fixed_to_double(s.top(true, 1)[0].first)));
}
