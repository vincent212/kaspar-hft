/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenL3Sync: the level3 checksum guide's snapshot, and synthetic
// sequences. Not checked against the live level3 channel (it needs an API
// key); these are the only tests of the L3 path.

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "enum/e_names.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "kraken_md/KrakenL3Sync.hpp"

using namespace kraken;
using json = nlohmann::json;
using R = KrakenL3Sync::Result;

namespace
{
  struct Fixture : public ::testing::Test
  {
    std::vector<bfile::l3_t> recs;
    KrakenL3Sync::Config cfg{7, char(en::x::KRAKEN), 0.1, 1e-8, 10};
    std::unique_ptr<KrakenL3Sync> s;

    void make(size_t depth = 10)
    {
      cfg.depth = depth;
      s = std::make_unique<KrakenL3Sync>(cfg, "BTC/USD", [this](const bfile::l3_t &r) { recs.push_back(r); });
    }

    // Checksum from the expected queue: asks low to high, bids high to low,
    // order by order; "px qty" pairs as text.
    static uint32_t ck(const std::vector<std::pair<std::string, std::string>> &asks,
                       const std::vector<std::pair<std::string, std::string>> &bids)
    {
      std::string s;
      for (auto &[p, q] : asks)
        s += checksum_token(p) + checksum_token(q);
      for (auto &[p, q] : bids)
        s += checksum_token(p) + checksum_token(q);
      return crc32(s);
    }

    static std::string ord(const char *ev, const char *id, const char *px, const char *qty)
    {
      std::string e = ev[0] ? std::string(R"("event":")") + ev + "\"," : std::string();
      return "{" + e + R"("order_id":")" + id + R"(","limit_price":)" + px + R"(,"order_qty":)" + qty +
             R"(,"timestamp":"2026-11-18T10:00:00.000000001Z"})";
    }

    R send(const char *type, const std::string &bids, const std::string &asks, uint32_t checksum,
           const char *ts = "2026-11-18T10:00:00.5Z")
    {
      std::string m = std::string(R"({"channel":"level3","type":")") + type +
                      R"(","data":[{"symbol":"BTC/USD","timestamp":")" + ts + R"(","checksum":)" +
                      std::to_string(checksum) + R"(,"bids":[)" + bids + R"(],"asks":[)" + asks + "]}]}";
      return s->on_message(parse_raw(m));
    }

    // Base book: bid 100.0 [A 1.0, B 2.0], bid 99.0 [C 1.0]; ask 101.0 [D 1.5].
    void base()
    {
      make();
      ASSERT_EQ(send("snapshot",
                     ord("", "A", "100.0", "1.00000000") + "," + ord("", "B", "100.0", "2.00000000") + "," +
                         ord("", "C", "99.0", "1.00000000"),
                     ord("", "D", "101.0", "1.50000000"),
                     ck({{"101.0", "1.50000000"}},
                        {{"100.0", "1.00000000"}, {"100.0", "2.00000000"}, {"99.0", "1.00000000"}})),
                R::Ok);
      recs.clear();
    }

    template <class T>
    std::vector<T> of()
    {
      std::vector<T> v;
      for (auto &r : recs)
        if (auto *x = std::get_if<T>(&r))
          v.push_back(*x);
      return v;
    }
  };
}

TEST_F(Fixture, ChecksumGuideSnapshot)
{
  make(10);
  std::ifstream f("data/l3_checksum_guide_snapshot.json");
  ASSERT_TRUE(f) << "run from the tests directory";
  std::stringstream ss;
  ss << f.rdbuf();
  EXPECT_EQ(s->on_message(parse_raw(ss.str())), R::Ok);
  EXPECT_EQ(s->stats().checksum_ok, 1u);
  EXPECT_EQ(s->checksum(), 1063832831u);
  EXPECT_EQ(s->orders(), 35u);
  // One trading-status record, then 35 recovery adds with distinct ids.
  auto sst = of<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 1u);
  EXPECT_EQ(sst[0].tradingStatus, 17);
  auto adds = of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(adds.size(), 35u);
  std::set<uint64_t> ids;
  for (auto &a : adds)
  {
    EXPECT_TRUE(a.recovery);
    EXPECT_EQ(a.orderUpdateAction, 0);
    ids.insert(a.orderID);
  }
  EXPECT_EQ(ids.size(), 35u);
  // OTCFZG-YOE2Q-LQKNM3: bid 44939.4, 0.88968699 BTC = 88968699 units of 1e-8.
  EXPECT_EQ(adds[0].side, '0');
  EXPECT_DOUBLE_EQ(adds[0].pxd, 449394.0);
  EXPECT_EQ(adds[0].displayQty, 88968699u);
  EXPECT_TRUE(of<bfile::l3_mbo_v2_t>().back().endOfEvent);
  EXPECT_EQ(of<bfile::l3_eob_t>().size(), 2u); // after the status and after the snapshot
}

TEST_F(Fixture, ChecksumGuideSnapshotWithAQueueSwapFails)
{
  make(10);
  std::ifstream f("data/l3_checksum_guide_snapshot.json");
  std::stringstream ss;
  ss << f.rdbuf();
  auto j = parse_raw(ss.str());
  auto &b = j["data"][0]["bids"];
  std::swap(b[0], b[1]); // same level, other queue order: the L3 checksum sees it
  EXPECT_EQ(s->on_message(j), R::ChecksumMismatch);
}

TEST_F(Fixture, AddJoinsTheBackOfTheQueue)
{
  base();
  EXPECT_EQ(send("update", ord("add", "E", "100.0", "0.50000000"), "",
                 ck({{"101.0", "1.50000000"}},
                    {{"100.0", "1.00000000"}, {"100.0", "2.00000000"}, {"100.0", "0.50000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  auto m = of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].orderUpdateAction, 0);
  EXPECT_FALSE(m[0].recovery);
  EXPECT_TRUE(m[0].endOfEvent);
  EXPECT_EQ(m[0].orderID, s->kid("E"));
}

TEST_F(Fixture, ModifyDownKeepsThePlaceUpOrPriceMovesToTheBack)
{
  base();
  // A down to 0.4: keeps the front.
  EXPECT_EQ(send("update", ord("modify", "A", "100.0", "0.40000000"), "",
                 ck({{"101.0", "1.50000000"}}, {{"100.0", "0.40000000"}, {"100.0", "2.00000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  // A up to 3.0: behind B.
  EXPECT_EQ(send("update", ord("modify", "A", "100.0", "3.00000000"), "",
                 ck({{"101.0", "1.50000000"}}, {{"100.0", "2.00000000"}, {"100.0", "3.00000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  // B to 99.0: behind C.
  EXPECT_EQ(send("update", ord("modify", "B", "99.0", "2.00000000"), "",
                 ck({{"101.0", "1.50000000"}}, {{"100.0", "3.00000000"}, {"99.0", "1.00000000"}, {"99.0", "2.00000000"}})),
            R::Ok);
  EXPECT_EQ(s->stats().moved_to_back, 2u);
  auto m = of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 3u);
  for (auto &x : m)
    EXPECT_EQ(x.orderUpdateAction, 1);
  EXPECT_DOUBLE_EQ(m[2].pxd, 990.0);
  EXPECT_EQ(m[2].displayQty, 200000000u);
}

TEST_F(Fixture, DeleteAndUnknownOrders)
{
  base();
  EXPECT_EQ(send("update", "", ord("delete", "D", "101.0", "1.50000000"),
                 ck({}, {{"100.0", "1.00000000"}, {"100.0", "2.00000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  EXPECT_EQ(s->kid("D"), 0u);
  EXPECT_TRUE(s->asks().empty());
  EXPECT_EQ(send("update", "", ord("delete", "ZZ", "101.0", "1.0"),
                 ck({}, {{"100.0", "1.00000000"}, {"100.0", "2.00000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  EXPECT_EQ(s->stats().unknown_order, 1u);
  auto m = of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].orderUpdateAction, 2);
}

TEST_F(Fixture, TruncationDeletesOrdersPushedOutOfDepth)
{
  make(2);
  ASSERT_EQ(send("snapshot", ord("", "A", "100.0", "1.0") + "," + ord("", "C", "99.0", "1.0"), "",
                 ck({}, {{"100.0", "1.0"}, {"99.0", "1.0"}})),
            R::Ok);
  recs.clear();
  // A new best bid at 100.5: 99.0 falls out of depth 2, with no delete sent.
  EXPECT_EQ(send("update", ord("add", "N", "100.5", "1.0"), "", ck({}, {{"100.5", "1.0"}, {"100.0", "1.0"}})), R::Ok);
  EXPECT_EQ(s->stats().truncated, 1u);
  EXPECT_EQ(s->kid("C"), 0u);
  auto m = of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 2u);
  EXPECT_EQ(m[1].orderUpdateAction, 2);
  EXPECT_DOUBLE_EQ(m[1].pxd, 990.0);
  // C comes back into scope as an add when N leaves.
  EXPECT_EQ(send("update", ord("delete", "N", "100.5", "1.0") + "," + ord("add", "C", "99.0", "1.0"), "",
                 ck({}, {{"100.0", "1.0"}, {"99.0", "1.0"}})),
            R::Ok);
  EXPECT_NE(s->kid("C"), 0u);
}

TEST_F(Fixture, TradeBeforeTheBookChangeBecomesAnExecution)
{
  base();
  // Taker sell 0.4 at 100.0 hits A (front of the bid queue).
  s->trade("100.0", "0.40000000", false, parse_rfc3339("2026-11-18T10:00:00.4Z"));
  auto p = of<bfile::l3_mbp_trd_t>();
  ASSERT_EQ(p.size(), 1u);
  EXPECT_EQ(p[0].aggresorSide, 2);
  EXPECT_DOUBLE_EQ(p[0].pxd, 1000.0);
  recs.clear();
  EXPECT_EQ(send("update", ord("modify", "A", "100.0", "0.60000000"), "",
                 ck({{"101.0", "1.50000000"}}, {{"100.0", "0.60000000"}, {"100.0", "2.00000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  auto x = of<bfile::l3_mbo_trd_v2_t>();
  ASSERT_EQ(x.size(), 1u);
  EXPECT_EQ(x[0].orderID, s->kid("A"));
  EXPECT_EQ(x[0].lastQty, 40000000u);
  ASSERT_TRUE(std::holds_alternative<bfile::l3_mbo_trd_v2_t>(recs[0])); // execution first
  // The next reduction at 100.0, with the budget spent, is a cancel.
  recs.clear();
  EXPECT_EQ(send("update", ord("delete", "B", "100.0", "2.00000000"), "",
                 ck({{"101.0", "1.50000000"}}, {{"100.0", "0.60000000"}, {"99.0", "1.00000000"}})),
            R::Ok);
  EXPECT_TRUE(of<bfile::l3_mbo_trd_v2_t>().empty());
}

TEST_F(Fixture, NewSnapshotReplacesTheBookAndIds)
{
  base();
  const uint64_t a = s->kid("A");
  ASSERT_EQ(send("snapshot", ord("", "A", "100.0", "1.0"), "", ck({}, {{"100.0", "1.0"}})), R::Ok);
  EXPECT_EQ(s->orders(), 1u);
  EXPECT_NE(s->kid("A"), a); // new handler id: the map was cleared
  for (auto &m : of<bfile::l3_mbo_v2_t>())
    EXPECT_TRUE(m.recovery);
}

TEST_F(Fixture, TransactTimeStrictlyIncreasesBetweenEvents)
{
  base();
  // Two messages with one timestamp: the second event is stamped 1 ns later.
  ASSERT_EQ(send("update", ord("add", "E", "100.0", "0.5"), "",
                 ck({{"101.0", "1.50000000"}},
                    {{"100.0", "1.00000000"}, {"100.0", "2.00000000"}, {"100.0", "0.5"}, {"99.0", "1.00000000"}})),
            R::Ok);
  ASSERT_EQ(send("update", ord("add", "F", "100.0", "0.5"), "",
                 ck({{"101.0", "1.50000000"}},
                    {{"100.0", "1.00000000"}, {"100.0", "2.00000000"}, {"100.0", "0.5"}, {"100.0", "0.5"}, {"99.0", "1.00000000"}})),
            R::Ok);
  auto m = of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 2u);
  EXPECT_LT(m[0].transactTime, m[1].transactTime);
}
