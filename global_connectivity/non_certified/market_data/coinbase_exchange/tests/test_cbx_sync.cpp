/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * CoinbaseExchangeSync from the documentation's examples
 * (docs.cdp.coinbase.com/exchange/websocket-feed/channels, retrieved
 * 2026-11-18) and the sync rules of feed.md section 5. No network.
 *
 * The examples on that page are independent snippets (three of them share
 * sequence 10, and their order ids do not connect), so the tests below keep
 * each example's fields and give them consecutive sequence numbers and
 * connected ids.
 */

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "Verifier.hpp"
#include "coinbase_exchange/CoinbaseExchangeSync.hpp"
#include "coinbase_exchange/act/CoinbaseExchangeFeed.hpp"
#include "enum/e_names.hpp"

using namespace coinbase_exchange;
using nlohmann::json;

namespace
{

const char *A = "d50ec984-77a8-460a-b958-66f114b0de9b";
const char *B = "ac928c66-ca53-498f-9c13-a110027a60e8";
const char *C = "132fb6ae-456b-4654-b4e0-d681ac05cea1";
const char *D = "c3f16063-77b1-408f-a743-88b7bc20cdcd";

ProductSpec btc()
{
  std::string err;
  auto s = ProductSpec::make("BTC-USD", 7, char(en::x::COINBASEX), "0.01", "0.00000001", "0.000001", err);
  EXPECT_TRUE(s) << err;
  return *s;
}

class CbxSyncTest : public ::testing::Test
{
protected:
  std::vector<bfile::l3_t> out;
  CoinbaseExchangeSync::Config cfg{btc()};
  std::unique_ptr<CoinbaseExchangeSync> s;

  void SetUp() override { make(); }
  void make()
  {
    s = std::make_unique<CoinbaseExchangeSync>(cfg, [this](const bfile::l3_t &r) { out.push_back(r); });
  }

  json snapshot(int64_t seq, std::vector<std::vector<std::string>> bids = {},
                std::vector<std::vector<std::string>> asks = {})
  {
    json j{{"sequence", seq}, {"auction_mode", false}, {"time", "2014-11-07T08:19:20.000000Z"}};
    j["bids"] = json::array();
    j["asks"] = json::array();
    for (auto &b : bids)
      j["bids"].push_back(b);
    for (auto &a : asks)
      j["asks"].push_back(a);
    return j;
  }
  void msg(json m, uint64_t recv = 1) { s->on_message(m, recv); }

  json open(int64_t seq, const char *id, const char *side, const char *px, const char *sz)
  {
    return {{"type", "open"},    {"time", "2014-11-07T08:19:27.028459Z"},
            {"product_id", "BTC-USD"}, {"sequence", seq},
            {"order_id", id},     {"price", px},
            {"remaining_size", sz}, {"side", side}};
  }
  json done(int64_t seq, const char *id, const char *side, const char *px, const char *rem, const char *reason)
  {
    return {{"type", "done"},  {"time", "2014-11-07T08:19:27.028459Z"},
            {"product_id", "BTC-USD"}, {"sequence", seq},
            {"price", px},     {"order_id", id},
            {"reason", reason}, {"side", side},
            {"remaining_size", rem}};
  }
  json match(int64_t seq, const char *maker, const char *taker, const char *side, const char *px, const char *sz)
  {
    return {{"type", "match"},       {"trade_id", 10},
            {"sequence", seq},       {"maker_order_id", maker},
            {"taker_order_id", taker}, {"time", "2014-11-07T08:19:27.028459Z"},
            {"product_id", "BTC-USD"}, {"size", sz},
            {"price", px},           {"side", side}};
  }
  json received(int64_t seq, const char *id, const char *side, const char *px, const char *sz)
  {
    return {{"type", "received"}, {"time", "2014-11-07T08:19:27.028459Z"},
            {"product_id", "BTC-USD"}, {"sequence", seq},
            {"order_id", id},     {"size", sz},
            {"price", px},        {"side", side},
            {"order_type", "limit"}};
  }

  template <class T> std::vector<T> all() const
  {
    std::vector<T> v;
    for (const auto &r : out)
      if (auto p = std::get_if<T>(&r))
        v.push_back(*p);
    return v;
  }
  std::vector<bfile::l3_mbo_v2_t> mbo(int action, bool recovery = false) const
  {
    std::vector<bfile::l3_mbo_v2_t> v;
    for (const auto &m : all<bfile::l3_mbo_v2_t>())
      if (m.orderUpdateAction == action && m.recovery == recovery)
        v.push_back(m);
    return v;
  }
  void sync_empty(int64_t seq = 100)
  {
    ASSERT_EQ(s->on_snapshot(snapshot(seq), 1), CoinbaseExchangeSync::SnapResult::Applied);
    out.clear();
  }
};

// ---- numbers -----------------------------------------------------------------------------

TEST(CbxNum, ScaledIsExact)
{
  EXPECT_EQ(scaled("63186.98", 2).value(), 6318698);
  EXPECT_EQ(scaled("0.00016074", 8).value(), 16074);
  EXPECT_EQ(scaled("1", 8).value(), 100000000);
  EXPECT_EQ(scaled("1.10", 2).value(), 110);
  EXPECT_EQ(scaled("0.03000000", 2).value(), 3) << "trailing zeros beyond the decimals are fine";
  EXPECT_FALSE(scaled("0.001", 2)) << "a non-zero digit beyond the increment is refused, not rounded";
  EXPECT_FALSE(scaled("-1", 2));
  EXPECT_FALSE(scaled("1e5", 2));
  EXPECT_FALSE(scaled("", 2));
  EXPECT_EQ(format_scaled(16074, 8), "0.00016074");
  EXPECT_EQ(format_scaled(6318698, 2), "63186.98");
  EXPECT_EQ(format_scaled(5, 0), "5");
  EXPECT_EQ(decimals_of("0.01"), 2);
  EXPECT_EQ(decimals_of("0.00000001"), 8);
  EXPECT_EQ(decimals_of("1"), 0);
  EXPECT_EQ(decimals_of("0.010"), 2);
}

TEST(CbxNum, Uuid)
{
  auto u = parse_uuid(A);
  ASSERT_TRUE(u);
  EXPECT_EQ(u->hi, 0xd50ec98477a8460aull);
  EXPECT_EQ(order_id64(*u), 0x550ec98477a8460aull) << "top bit cleared";
  EXPECT_EQ(u->lo, 0xb95866f114b0de9bull);
  EXPECT_EQ(to_string(*u), A);
  EXPECT_FALSE(parse_uuid("d50ec974-76a2-454b-66f135b1ea8c")) << "the client_oid in the docs' received example is 32 hex digits";
  EXPECT_TRUE(is_uuid_v4_lower("1985ca2d-61ef-49f1-bfce-6c39d8462914")) << "the ClOrdID example in the FIX docs";
  EXPECT_FALSE(is_uuid_v4_lower("1985CA2D-61EF-49F1-BFCE-6C39D8462914")) << "upper case is refused";
  EXPECT_FALSE(is_uuid_v4_lower("1985ca2d-61ef-19f1-bfce-6c39d8462914")) << "version 1";
  EXPECT_FALSE(is_uuid_v4_lower("1985ca2d-61ef-49f1-3fce-6c39d8462914")) << "variant 0";
  auto v = make_uuid_v4(~0ull, ~0ull);
  EXPECT_TRUE(is_uuid_v4_lower(to_string(v)));
}

TEST(CbxNum, Times)
{
  EXPECT_EQ(parse_iso_time("2014-11-07T08:19:27.028459Z"), 1415348367028459000ull);
  EXPECT_EQ(parse_iso_time("2026-11-18T17:25:47.637647565Z") % 1000000000ull, 637647565ull);
  EXPECT_EQ(parse_iso_time("2014-11-07T08:19:28Z"), 1415348368000000000ull);
  EXPECT_EQ(parse_iso_time("garbage"), 0u);
  EXPECT_EQ(fix_time_ms(1415348367028459000ull), "20141107-08:19:27.028");
  EXPECT_EQ(parse_fix_time("20141107-08:19:27.028"), 1415348367028000000ull);
}

TEST(CbxNum, ProductSpecDisplayRoundsUpAndSaturates)
{
  auto p = btc();
  EXPECT_EQ(p.ticks("63186.98").value(), 6318698);
  EXPECT_FALSE(p.ticks("63186.985"));
  EXPECT_EQ(p.display(*p.size("0.00016074")), 161u) << "16074e-8 BTC in 1e-6 units, rounded up";
  EXPECT_EQ(p.display(*p.size("0.000001")), 1u);
  EXPECT_EQ(p.display(*p.size("0.00000001")), 1u) << "no resting order shows as 0";
  EXPECT_EQ(p.display(*p.size("9.999999")), 9999999u);
  bool sat = false;
  EXPECT_EQ(p.display(*p.size("9.999999"), &sat), 9999999u);
  EXPECT_FALSE(sat);
  EXPECT_EQ(p.display(*p.size("3359.2"), &sat), 9999999u) << "OB refuses 10,000,000 units or more";
  EXPECT_TRUE(sat);
  std::string err;
  EXPECT_FALSE(ProductSpec::make("X", 1, 0, "0.01", "0.00000001", "0.000000001", err)) << "unit finer than the increment";
  EXPECT_FALSE(ProductSpec::make("X", 1, 0, "0.01", "0.00000002", "0.00000003", err))
      << "unit not a multiple of base_increment";
  EXPECT_TRUE(ProductSpec::make("X", 1, 0, "0.01", "0.00000002", "0.00000006", err)) << err;
  EXPECT_EQ(ProductSpec::make("X", 1, 0, "0.01", "0.00000002", "0.00000006", err)->qty_unit, 6);
}

// ---- snapshot alignment (channels.md, full channel steps 1-6) --------------------------

TEST_F(CbxSyncTest, SnapshotGoesOutAsRecoveryAddsInBookOrder)
{
  ASSERT_EQ(s->on_snapshot(snapshot(100, {{"502.10", "1.34", A}, {"502.10", "0.5", B}}, {{"502.11", "2", C}}), 1),
            CoinbaseExchangeSync::SnapResult::Applied);
  auto sst = all<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 1u);
  EXPECT_EQ(sst[0].tradingStatus, 17);
  auto adds = mbo(0, true);
  ASSERT_EQ(adds.size(), 3u);
  EXPECT_EQ(adds[0].orderID, order_id64(*parse_uuid(A)));
  EXPECT_EQ(adds[1].orderID, order_id64(*parse_uuid(B)));
  EXPECT_EQ(adds[0].pxd, 50210.0);
  EXPECT_EQ(adds[0].displayQty, 1340000u);
  EXPECT_EQ(adds[0].side, '0');
  EXPECT_EQ(adds[2].side, '1');
  EXPECT_LT(adds[0].priority, adds[1].priority) << "the snapshot's order is the queue order";
  EXPECT_FALSE(adds[0].endOfEvent);
  EXPECT_TRUE(adds[2].endOfEvent);
  EXPECT_EQ(adds[0].transactTime, adds[2].transactTime);
  EXPECT_EQ(s->last_seq(), 100);
  EXPECT_TRUE(s->synced());
  EXPECT_EQ(s->queue(true, 50210), (std::vector<Uuid>{*parse_uuid(A), *parse_uuid(B)}));
}

TEST_F(CbxSyncTest, BufferedMessagesAtOrBelowTheSnapshotAreDiscarded)
{
  // stream first: 99, 100, 101, 102 are buffered while the snapshot is fetched
  msg(open(99, D, "buy", "100.00", "1"));
  msg(open(100, C, "buy", "100.00", "1"));
  msg(open(101, A, "sell", "200.20", "1.00"));
  msg(open(102, B, "sell", "200.20", "2"));
  EXPECT_TRUE(out.empty()) << "nothing goes out before the snapshot";
  ASSERT_EQ(s->on_snapshot(snapshot(100, {{"100.00", "1", D}, {"100.00", "1", C}}), 1),
            CoinbaseExchangeSync::SnapResult::Applied);
  EXPECT_EQ(mbo(0, true).size(), 2u) << "99 and 100 are in the snapshot";
  EXPECT_EQ(mbo(0).size(), 2u) << "101 and 102 are applied after it";
  EXPECT_EQ(s->last_seq(), 102);
  EXPECT_EQ(s->level(false, 20020).count, 2u);
}

TEST_F(CbxSyncTest, SnapshotOlderThanTheBufferIsRefused)
{
  msg(open(105, A, "sell", "200.20", "1"));
  msg(open(106, B, "sell", "200.20", "1"));
  EXPECT_EQ(s->on_snapshot(snapshot(100), 1), CoinbaseExchangeSync::SnapResult::TooOld)
      << "101..104 are neither in the snapshot nor buffered";
  EXPECT_TRUE(out.empty());
  EXPECT_TRUE(s->need_snapshot());
  // a newer snapshot covers them
  EXPECT_EQ(s->on_snapshot(snapshot(104), 1), CoinbaseExchangeSync::SnapResult::Applied);
  EXPECT_EQ(s->last_seq(), 106);
  EXPECT_EQ(s->level(false, 20020).count, 2u) << "the buffer was kept across the refused snapshot";
}

TEST_F(CbxSyncTest, SnapshotNewerThanTheWholeBufferDropsIt)
{
  msg(open(101, A, "sell", "200.20", "1"));
  EXPECT_EQ(s->on_snapshot(snapshot(150), 1), CoinbaseExchangeSync::SnapResult::Applied);
  EXPECT_EQ(s->last_seq(), 150);
  EXPECT_EQ(s->orders(), 0u);
  msg(open(140, B, "sell", "200.20", "1"));
  EXPECT_EQ(s->orders(), 0u) << "the stream is behind the snapshot: discarded";
  msg(open(151, B, "sell", "200.20", "1"));
  EXPECT_EQ(s->orders(), 1u);
}

TEST_F(CbxSyncTest, MalformedSnapshot)
{
  EXPECT_EQ(s->on_snapshot(json{{"bids", json::array()}}, 1), CoinbaseExchangeSync::SnapResult::Bad);
  EXPECT_EQ(s->on_snapshot(snapshot(10, {{"1.001", "1", A}}), 1), CoinbaseExchangeSync::SnapResult::Bad)
      << "a price off the increment";
  EXPECT_EQ(s->on_snapshot(snapshot(10, {{"1.00", "1", "not-a-uuid"}}), 1), CoinbaseExchangeSync::SnapResult::Bad);
  EXPECT_TRUE(out.empty());
}

// ---- message types (the documentation's examples) ------------------------------------------

TEST_F(CbxSyncTest, ReceivedChangesNothing)
{
  sync_empty(9);
  msg(received(10, A, "buy", "502.1", "1.34"));
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(s->orders(), 0u);
  EXPECT_EQ(s->last_seq(), 10);
  EXPECT_EQ(s->stats().received, 1u);
}

TEST_F(CbxSyncTest, ReceivedMarketOrderWithFunds)
{
  sync_empty(11);
  msg({{"type", "received"}, {"time", "2014-11-09T08:19:27.028459Z"}, {"product_id", "BTC-USD"},
       {"sequence", 12}, {"order_id", "dddec984-77a8-460a-b958-66f114b0de9b"}, {"funds", "3000.234"},
       {"side", "buy"}, {"order_type", "market"}});
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(s->last_seq(), 12);
}

TEST_F(CbxSyncTest, OpenAddsTheOrder)
{
  sync_empty(9);
  msg(open(10, A, "sell", "200.2", "1.00"), 77);
  auto adds = mbo(0);
  ASSERT_EQ(adds.size(), 1u);
  EXPECT_EQ(adds[0].pxd, 20020.0);
  EXPECT_EQ(adds[0].displayQty, 1000000u);
  EXPECT_EQ(adds[0].side, '1');
  EXPECT_TRUE(adds[0].endOfEvent);
  EXPECT_FALSE(adds[0].recovery);
  EXPECT_EQ(adds[0].securityID, 7);
  EXPECT_EQ(adds[0].venue, char(en::x::COINBASEX));
  EXPECT_EQ(adds[0].handlerendtim, 77u);
  EXPECT_EQ(adds[0].sendingTime, 1415348367028459000ull);
  auto eop = all<bfile::l3_eop_t>();
  ASSERT_EQ(eop.size(), 1u);
  EXPECT_EQ(eop[0].msgSeqNum, 10u);
  EXPECT_EQ(all<bfile::l3_eob_t>().size(), 1u);
}

TEST_F(CbxSyncTest, DoneRemovesAnOrderOnTheBookAndIgnoresOthers)
{
  sync_empty(9);
  msg(open(10, A, "sell", "200.2", "1.00"));
  out.clear();
  msg(done(11, A, "sell", "200.2", "0", "filled"));
  ASSERT_EQ(mbo(2).size(), 1u);
  EXPECT_EQ(mbo(2)[0].orderID, order_id64(*parse_uuid(A)));
  EXPECT_EQ(s->orders(), 0u);
  out.clear();
  msg(done(12, B, "sell", "200.2", "0", "canceled"));
  EXPECT_TRUE(out.empty()) << "done for an order that never opened: ignored";
  EXPECT_EQ(s->stats().ignored_done, 1u);
}

TEST_F(CbxSyncTest, MatchExecutesTheMakerThenChangesIt)
{
  sync_empty(49);
  msg(open(50, B, "sell", "400.23", "8.234412")); // below the 9,999,999-unit cap
  out.clear();
  msg(match(51, B, C, "sell", "400.23", "5.23512"));
  // print, execution on the maker, change to the remaining size
  ASSERT_EQ(out.size(), 5u);
  auto p = std::get<bfile::l3_mbp_trd_t>(out[0]);
  EXPECT_EQ(p.pxd, 40023.0);
  EXPECT_EQ(p.aggresorSide, 1) << "maker side sell: the aggressor bought";
  EXPECT_EQ(p.sz, 5235120);
  auto x = std::get<bfile::l3_mbo_trd_v2_t>(out[1]);
  EXPECT_EQ(x.orderID, order_id64(*parse_uuid(B)));
  EXPECT_EQ(x.lastQty, 8234412 - 2999292) << "display units before minus after";
  auto c = std::get<bfile::l3_mbo_v2_t>(out[2]);
  EXPECT_EQ(c.orderUpdateAction, 1);
  EXPECT_EQ(c.displayQty, 2999292u) << "8.234412 - 5.23512 = 2.999292";
  EXPECT_TRUE(c.endOfEvent);
  EXPECT_FALSE(x.endOfEvent);
  EXPECT_EQ(p.transactTime, x.transactTime);
  EXPECT_EQ(x.transactTime, c.transactTime);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eop_t>(out[3]));
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eob_t>(out[4]));
  EXPECT_EQ(s->level(false, 40023).size, 299929200);
}

TEST_F(CbxSyncTest, MatchThatFillsTheMakerDeletesIt_TheDoneIsIgnored)
{
  sync_empty(49);
  msg(open(50, B, "buy", "400.23", "5.23512"));
  out.clear();
  msg(match(51, B, C, "buy", "400.23", "5.23512"));
  EXPECT_EQ(all<bfile::l3_mbp_trd_t>()[0].aggresorSide, 2) << "maker bought: the aggressor sold";
  auto x = all<bfile::l3_mbo_trd_v2_t>();
  ASSERT_EQ(x.size(), 1u);
  EXPECT_EQ(x[0].lastQty, 5235120);
  ASSERT_EQ(mbo(2).size(), 1u);
  EXPECT_EQ(s->orders(), 0u);
  out.clear();
  msg(done(52, B, "buy", "400.23", "0", "filled"));
  EXPECT_TRUE(out.empty());
}

TEST_F(CbxSyncTest, MatchBelowOneDisplayUnitPrintsButDoesNotExecute)
{
  sync_empty(49);
  msg(open(50, B, "sell", "400.23", "0.00016074")); // 161 units
  out.clear();
  msg(match(51, B, C, "sell", "400.23", "0.00000003")); // 16071 -> still 161 units
  EXPECT_EQ(all<bfile::l3_mbp_trd_t>().size(), 1u);
  EXPECT_TRUE(all<bfile::l3_mbo_trd_v2_t>().empty());
  EXPECT_EQ(s->stats().zero_execs, 1u);
  EXPECT_EQ(s->find(*parse_uuid(B))->size, 16071);
}

TEST_F(CbxSyncTest, MatchOnAnUnknownMakerResyncs)
{
  sync_empty(49);
  msg(match(50, B, C, "sell", "400.23", "1"));
  EXPECT_EQ(s->stats().unknown_maker, 1u);
  EXPECT_EQ(all<bfile::l3_gap_v2_t>().size(), 1u);
  EXPECT_TRUE(all<bfile::l3_mbp_trd_t>().empty()) << "nothing of the bad event goes out";
  EXPECT_TRUE(s->need_snapshot());
}

TEST_F(CbxSyncTest, OverfillResyncs)
{
  sync_empty(49);
  msg(open(50, B, "sell", "400.23", "1"));
  out.clear();
  msg(match(51, B, C, "sell", "400.23", "2"));
  EXPECT_EQ(s->stats().overfill, 1u);
  EXPECT_TRUE(s->need_snapshot());
}

TEST_F(CbxSyncTest, ChangeFromSelfTradePrevention)
{
  sync_empty(79);
  msg(open(80, B, "sell", "400.23", "12.234412")); // the docs' old_size; above the cap, saturated
  EXPECT_EQ(s->stats().saturated, 1u);
  out.clear();
  msg({{"type", "change"}, {"reason", "STP"}, {"time", "2014-11-07T08:19:27.028459Z"}, {"sequence", 81},
       {"order_id", B}, {"side", "sell"}, {"product_id", "BTC-USD"}, {"old_size", "12.234412"},
       {"new_size", "5.23512"}, {"price", "400.23"}});
  auto c = mbo(1);
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].displayQty, 5235120u);
  EXPECT_EQ(c[0].pxd, 40023.0);
}

TEST_F(CbxSyncTest, ChangeFromModifyOrder)
{
  cfg.spec = *ProductSpec::make("ETH-USD", 8, char(en::x::COINBASEX), "0.01", "0.00000001", "0.00001",
                                *std::make_unique<std::string>());
  make();
  ASSERT_EQ(s->on_snapshot(snapshot(24752, {{"7.00", "80", D}}), 1), CoinbaseExchangeSync::SnapResult::Applied);
  out.clear();
  s->on_message({{"type", "change"}, {"reason", "modify_order"}, {"time", "2022-06-06T22:55:43.433114Z"},
                 {"sequence", 24753}, {"order_id", D}, {"side", "buy"}, {"product_id", "ETH-USD"},
                 {"old_size", "80"}, {"new_size", "80"}, {"old_price", "7"}, {"new_price", "6"}},
                1);
  auto c = mbo(1);
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].pxd, 600.0);
  EXPECT_EQ(c[0].displayQty, 8000000u);
  EXPECT_EQ(s->level(true, 700).count, 0u);
  EXPECT_EQ(s->level(true, 600).count, 1u);
}

TEST_F(CbxSyncTest, ChangeForAReceivedOrderIsIgnored)
{
  sync_empty(79);
  msg(received(80, B, "sell", "400.23", "12"));
  msg({{"type", "change"}, {"reason", "STP"}, {"time", "2014-11-07T08:19:27.028459Z"}, {"sequence", 81},
       {"order_id", B}, {"side", "sell"}, {"product_id", "BTC-USD"}, {"old_size", "12"},
       {"new_size", "5"}, {"price", "400.23"}});
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(s->stats().ignored_change, 1u);
  // an STP change on a market order has price null
  msg({{"type", "change"}, {"reason", "STP"}, {"time", "2014-11-07T08:19:27.028459Z"}, {"sequence", 82},
       {"order_id", C}, {"side", "buy"}, {"product_id", "BTC-USD"}, {"old_size", "2"},
       {"new_size", "1"}, {"price", nullptr}});
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(s->last_seq(), 82);
}

// Coinbase: a size decrease keeps priority; an increase or a price change
// loses it (FIX OrderCancelReplaceRequest, order-entry-messages5). The sync's
// queue follows the same rule as OB.
TEST_F(CbxSyncTest, PriorityOnChange)
{
  ASSERT_EQ(s->on_snapshot(snapshot(10, {{"100.00", "1", A}, {"100.00", "1", B}, {"100.00", "1", C}}), 1),
            CoinbaseExchangeSync::SnapResult::Applied);
  auto chg = [&](int64_t seq, const char *id, const char *px, const char *sz) {
    msg({{"type", "change"}, {"reason", "modify_order"}, {"time", "2022-06-06T22:55:43.433114Z"},
         {"sequence", seq}, {"order_id", id}, {"side", "buy"}, {"product_id", "BTC-USD"},
         {"new_size", sz}, {"new_price", px}});
  };
  auto ids = [&](std::vector<const char *> v) {
    std::vector<Uuid> u;
    for (auto x : v)
      u.push_back(*parse_uuid(x));
    return u;
  };
  chg(11, A, "100.00", "0.5");
  EXPECT_EQ(s->queue(true, 10000), ids({A, B, C})) << "smaller: keeps its place";
  chg(12, A, "100.00", "2");
  EXPECT_EQ(s->queue(true, 10000), ids({B, C, A})) << "larger: to the back";
  chg(13, B, "100.01", "1");
  chg(14, B, "100.00", "1");
  EXPECT_EQ(s->queue(true, 10000), ids({C, A, B})) << "a price change and back: to the back";
}

// ---- sequencing -----------------------------------------------------------------------------

TEST_F(CbxSyncTest, DuplicatesAreIgnored)
{
  sync_empty(9);
  msg(open(10, A, "sell", "200.2", "1"));
  msg(open(10, A, "sell", "200.2", "1"));
  msg(open(9, B, "sell", "200.2", "1"));
  EXPECT_EQ(s->stats().duplicates, 2u);
  EXPECT_EQ(s->orders(), 1u);
}

TEST_F(CbxSyncTest, OutOfOrderWithinTheWindowIsApplied_InOrder)
{
  sync_empty(9);
  msg(open(11, B, "sell", "200.2", "1"));
  EXPECT_EQ(s->orders(), 0u) << "held: 10 is missing";
  msg(open(10, A, "sell", "200.2", "1"));
  EXPECT_EQ(s->orders(), 2u);
  EXPECT_EQ(s->last_seq(), 11);
  EXPECT_EQ(s->stats().reordered, 1u);
  EXPECT_EQ(s->queue(false, 20020), (std::vector<Uuid>{*parse_uuid(A), *parse_uuid(B)}));
  EXPECT_TRUE(all<bfile::l3_gap_v2_t>().empty());
}

TEST_F(CbxSyncTest, GapBeyondTheWindowResyncsFromANewSnapshot)
{
  cfg.reorder_window = 2;
  make();
  sync_empty(9);
  msg(open(10, A, "sell", "200.2", "1"));
  // 11 is lost
  msg(open(12, B, "sell", "200.2", "1"));
  msg(open(13, C, "sell", "200.3", "1"));
  EXPECT_TRUE(all<bfile::l3_gap_v2_t>().empty()) << "still inside the window";
  msg(open(14, D, "sell", "200.4", "1"));
  EXPECT_EQ(all<bfile::l3_gap_v2_t>().size(), 1u);
  EXPECT_TRUE(s->need_snapshot());
  EXPECT_FALSE(s->synced());
  msg(done(15, D, "sell", "200.4", "1", "canceled")); // buffered
  out.clear();
  // the venue's snapshot at 13 has A, the lost order (11, id C below at 200.25... any), B and C
  ASSERT_EQ(s->on_snapshot(snapshot(13, {}, {{"200.20", "1", A}, {"200.20", "1", B}, {"200.30", "1", C}}), 1),
            CoinbaseExchangeSync::SnapResult::Applied);
  EXPECT_EQ(mbo(0, true).size(), 3u);
  EXPECT_EQ(mbo(0).size(), 1u) << "14 applied from the buffer";
  EXPECT_EQ(mbo(2).size(), 1u) << "15 applied from the buffer";
  EXPECT_EQ(s->last_seq(), 15);
  EXPECT_EQ(s->stats().resyncs, 1u);
  EXPECT_EQ(all<bfile::l3_chr_v2_t>().size(), 1u) << "a resync clears OB's book first";
  EXPECT_TRUE(std::holds_alternative<bfile::l3_chr_v2_t>(out[0]));
  // transactTime keeps increasing across the recovery
  uint64_t prev = 0;
  for (const auto &m : all<bfile::l3_mbo_v2_t>())
  {
    EXPECT_GE(m.transactTime, prev);
    prev = m.transactTime;
  }
  EXPECT_GT(mbo(0)[0].transactTime, mbo(0, true)[0].transactTime)
      << "OB rejects data stamped at or before a recovery";
}

TEST_F(CbxSyncTest, TransactTimeStrictlyIncreasesBetweenEventsWithOneTimestamp)
{
  sync_empty(9);
  msg(open(10, A, "sell", "200.2", "1"));
  msg(open(11, B, "sell", "200.2", "1"));
  auto adds = mbo(0);
  ASSERT_EQ(adds.size(), 2u);
  EXPECT_LT(adds[0].transactTime, adds[1].transactTime);
}

TEST_F(CbxSyncTest, DisconnectSendsAGapAndWaitsForASnapshot)
{
  sync_empty(9);
  s->on_disconnect(5);
  EXPECT_EQ(all<bfile::l3_gap_v2_t>().size(), 1u);
  EXPECT_TRUE(s->need_snapshot());
  msg(open(10, A, "sell", "200.2", "1"));
  EXPECT_EQ(s->orders(), 0u) << "buffered for the next snapshot";
}

TEST_F(CbxSyncTest, OtherProductsAreIgnored)
{
  sync_empty(9);
  auto m = open(10, A, "sell", "200.2", "1");
  m["product_id"] = "ETH-USD";
  msg(m);
  EXPECT_EQ(s->orders(), 0u);
  EXPECT_EQ(s->last_seq(), 9);
}

TEST_F(CbxSyncTest, HeartbeatAheadOfTheStreamIsCounted)
{
  sync_empty(89);
  msg({{"type", "heartbeat"}, {"sequence", 90}, {"last_trade_id", 20}, {"product_id", "BTC-USD"},
       {"time", "2014-11-07T08:19:28.464459Z"}});
  EXPECT_EQ(s->stats().hb_ahead, 1u);
  msg({{"type", "heartbeat"}, {"sequence", 89}, {"last_trade_id", 20}, {"product_id", "BTC-USD"},
       {"time", "2014-11-07T08:19:29.464459Z"}});
  EXPECT_EQ(s->stats().hb_ahead, 1u);
  EXPECT_TRUE(all<bfile::l3_gap_v2_t>().empty());
}

// The heartbeat says the venue is past the stream: the message after the last
// applied is missing. Still missing after heartbeat_gap_ns, it is a gap; the
// clock restarts whenever the stream advances.
TEST_F(CbxSyncTest, HeartbeatAheadOfTheStreamForTooLongIsAGap)
{
  const uint64_t sec = 1000000000ull;
  sync_empty(89);
  auto hb = [&](int64_t seq, uint64_t recv) {
    msg({{"type", "heartbeat"}, {"sequence", seq}, {"last_trade_id", 20}, {"product_id", "BTC-USD"},
         {"time", "2014-11-07T08:19:28.464459Z"}},
        recv);
  };
  hb(90, 1 * sec);
  hb(91, 2 * sec);
  hb(92, 4 * sec); // 3 s since the first: not yet
  EXPECT_TRUE(all<bfile::l3_gap_v2_t>().empty());
  EXPECT_TRUE(s->synced());
  msg(open(90, A, "sell", "200.2", "1"), 4 * sec + 1); // the stream advances: 91 is the missing one now
  hb(93, 7 * sec);
  EXPECT_TRUE(all<bfile::l3_gap_v2_t>().empty()) << "91 has been missing for under 3 s";
  hb(94, 10 * sec + 1);
  EXPECT_EQ(all<bfile::l3_gap_v2_t>().size(), 1u);
  EXPECT_EQ(s->stats().gaps, 1u);
  EXPECT_FALSE(s->synced());
  EXPECT_TRUE(s->need_snapshot());
  EXPECT_EQ(s->stats().hb_ahead, 5u);
}

TEST_F(CbxSyncTest, ActivateHasNoSequenceAndChangesNothing)
{
  sync_empty(9);
  msg({{"type", "activate"}, {"product_id", "BTC-USD"}, {"timestamp", "1483736448.299000"}, {"user_id", "12"},
       {"profile_id", "30000727-d308-cf50-7b1c-c06deb1934fc"}, {"order_id", "7b52009b-64fd-0a2a-49e6-d8a939753077"},
       {"stop_type", "entry"}, {"side", "buy"}, {"stop_price", "80"}, {"size", "2"}, {"funds", "50"},
       {"private", true}});
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(s->last_seq(), 9);
}

// ---- the level3 channel (compact arrays after a schema message) ---------------------------

TEST_F(CbxSyncTest, Level3CompactForm)
{
  msg(json::parse(R"({"type":"level3","schema":{
      "change":["type","product_id","sequence","order_id","price","size","time"],
      "done":["type","product_id","sequence","order_id","time"],
      "match":["type","product_id","sequence","maker_order_id","taker_order_id","price","size","time"],
      "noop":["type","product_id","sequence","time"],
      "open":["type","product_id","sequence","order_id","side","price","size","time"]}})"));
  sync_empty(57560479455);
  // the documentation's open example
  msg(json::parse(R"(["open","BTC-USD","57560479456","12aca6e0-7400-418a-9e59-c0020a3bf8cc","buy","27268.09","0.02","2023-03-28T23:24:03.185394Z"])"));
  ASSERT_EQ(mbo(0).size(), 1u);
  EXPECT_EQ(mbo(0)[0].pxd, 2726809.0);
  EXPECT_EQ(mbo(0)[0].displayQty, 20000u);
  EXPECT_EQ(mbo(0)[0].side, '0');
  msg(json::parse(R"(["change","BTC-USD","57560479457","12aca6e0-7400-418a-9e59-c0020a3bf8cc","27268.09","0.01","2023-03-28T23:24:03.185394Z"])"));
  EXPECT_EQ(mbo(1).size(), 1u);
  EXPECT_EQ(mbo(1)[0].displayQty, 10000u);
  msg(json::parse(R"(["noop","BTC-USD","57560479458","2023-03-28T23:24:03.185394Z"])"));
  msg(json::parse(R"(["match","BTC-USD","57560479459","12aca6e0-7400-418a-9e59-c0020a3bf8cc","132fb6ae-456b-4654-b4e0-d681ac05cea1","27268.09","0.004","2023-03-28T23:24:03.185394Z"])"));
  EXPECT_EQ(all<bfile::l3_mbo_trd_v2_t>().size(), 1u);
  msg(json::parse(R"(["done","BTC-USD","57560479460","12aca6e0-7400-418a-9e59-c0020a3bf8cc","2023-03-28T23:24:03.185394Z"])"));
  EXPECT_EQ(mbo(2).size(), 1u);
  EXPECT_EQ(s->last_seq(), 57560479460);
  EXPECT_EQ(s->stats().gaps, 0u);
}

TEST_F(CbxSyncTest, Level3WithoutSchemaIsNotGuessed)
{
  sync_empty(1);
  msg(json::parse(R"(["open","BTC-USD","2","12aca6e0-7400-418a-9e59-c0020a3bf8cc","buy","27268.09","0.02","2023-03-28T23:24:03.185394Z"])"));
  EXPECT_EQ(s->orders(), 0u);
  EXPECT_EQ(s->stats().bad_message, 1u);
}

// ---- trading status -------------------------------------------------------------------------

TEST(CbxStatus, Mapping)
{
  auto p = json::parse(R"({"id":"BTC-USD","status":"online","trading_disabled":false,"cancel_only":false,
                           "limit_only":false,"post_only":false,"auction_mode":false})");
  EXPECT_EQ(CoinbaseExchangeSync::trading_status(p), 17);
  p["auction_mode"] = true;
  EXPECT_EQ(CoinbaseExchangeSync::trading_status(p), 21);
  p["auction_mode"] = false;
  p["cancel_only"] = true;
  EXPECT_EQ(CoinbaseExchangeSync::trading_status(p), 2);
  p["cancel_only"] = false;
  p["limit_only"] = true;
  EXPECT_EQ(CoinbaseExchangeSync::trading_status(p), 17);
  p["status"] = "delisted";
  EXPECT_EQ(CoinbaseExchangeSync::trading_status(p), 4);
}

TEST_F(CbxSyncTest, StatusChannelSendsSstOnChange)
{
  sync_empty(1);
  auto st = json::parse(R"({"type":"status","products":[{"id":"BTC-USD","status":"online","cancel_only":true},
                                                         {"id":"ETH-USD","status":"online"}]})");
  msg(st);
  auto sst = all<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 1u);
  EXPECT_EQ(sst[0].tradingStatus, 2);
  msg(st);
  EXPECT_EQ(all<bfile::l3_sst_t>().size(), 1u) << "no change, no record";
}

// ---- partial stream (the public matches channel) --------------------------------------------

TEST_F(CbxSyncTest, PartialStreamAcceptsJumpsAndCountsUnknownMakers)
{
  cfg.check_gaps = false;
  make();
  ASSERT_EQ(s->on_snapshot(snapshot(100, {}, {{"400.23", "1", B}}), 1), CoinbaseExchangeSync::SnapResult::Applied);
  msg(match(95, B, C, "sell", "400.23", "0.5"));
  EXPECT_EQ(s->find(*parse_uuid(B))->size, 100000000) << "at or below the snapshot: discarded";
  msg(match(150, B, C, "sell", "400.23", "0.25"));
  EXPECT_EQ(s->find(*parse_uuid(B))->size, 75000000);
  msg(match(160, A, C, "sell", "400.23", "0.25"));
  EXPECT_EQ(s->stats().unknown_maker, 1u);
  EXPECT_TRUE(s->synced()) << "partial: no resync";
  EXPECT_TRUE(all<bfile::l3_gap_v2_t>().empty());
}

} // namespace

// ---- the feed's L3 handler ------------------------------------------------------------------

// A level-3 book the sync did not ask for (a periodic fetch, or one in flight
// when the sync caught up) is not applied: it would rebuild a book in sync and
// could move its sequence backwards.
TEST(CbxL3Handler, SnapshotNotAskedForIsIgnored)
{
  std::vector<bfile::l3_t> out;
  auto h = make_l3_handler(CoinbaseExchangeSync::Config{btc()}, [&](const bfile::l3_t &r) { out.push_back(r); });
  auto snap = [](int64_t seq) {
    return json{{"sequence", seq}, {"time", "2026-11-18T17:00:00Z"}, {"bids", json::array()}, {"asks", json::array()}};
  };
  EXPECT_TRUE(h->need_snapshot());
  h->on_rest(3, snap(100), 0, 1);
  EXPECT_FALSE(h->need_snapshot());
  EXPECT_NE(h->status_line().find(" seq 100 "), std::string::npos) << h->status_line();
  const size_t n = out.size();
  h->on_rest(3, snap(50), 0, 2);
  EXPECT_EQ(out.size(), n) << "an older book, not asked for: nothing to OB";
  EXPECT_NE(h->status_line().find(" seq 100 "), std::string::npos) << h->status_line();
  h->on_frame(json{{"type", "open"}, {"time", "2026-11-18T17:00:01Z"}, {"product_id", "BTC-USD"}, {"sequence", 101},
                   {"order_id", A}, {"price", "100.00"}, {"remaining_size", "1"}, {"side", "sell"}},
              3);
  EXPECT_NE(h->status_line().find(" seq 101 "), std::string::npos) << "still in sync: 101 follows 100";
  h->on_disconnect(4);
  EXPECT_TRUE(h->need_snapshot());
  h->on_rest(3, snap(200), 0, 5);
  EXPECT_FALSE(h->need_snapshot()) << "asked for: applied";
}

// ---- the step-3 checker, on a synthetic stream --------------------------------------------

// The level3 channel's schema arrives once per connection; the second sync
// needs it to read the compact arrays in the ring, also after the ring has
// rolled past it.
TEST(CbxL3Checker, Level3SchemaIsReplayedIntoTheSecondSync)
{
  auto spec = btc();
  coinbase_exchange::verify::L3Checker c(spec, 3); // the ring holds 3 messages
  auto snap = [](int64_t seq, std::vector<std::vector<std::string>> asks) {
    json j{{"sequence", seq}, {"time", "2026-11-18T17:00:00Z"}, {"bids", json::array()}};
    j["asks"] = json::array();
    for (auto &a : asks)
      j["asks"].push_back(a);
    return j;
  };
  auto open = [](int64_t seq, const char *id, const char *sz) {
    return json::array({"open", "BTC-USD", std::to_string(seq), id, "sell", "100.00", sz, "2026-11-18T17:00:01Z"});
  };
  c.on_frame(json::parse(R"({"type":"level3","schema":{
      "open":["type","product_id","sequence","order_id","side","price","size","time"]}})"),
             1);
  c.on_rest(3, snap(10, {}), 0, 2);
  ASSERT_TRUE(c.sync().synced());
  c.on_frame(open(11, A, "1"), 3);
  c.on_frame(open(12, B, "2"), 4);
  c.on_frame(open(13, C, "3"), 5);
  c.on_frame(open(14, D, "4"), 6); // the ring now holds 12, 13, 14
  ASSERT_EQ(c.sync().orders(), 4u);
  c.on_rest(3, snap(11, {{"100.00", "1", A}}), 0, 7);
  EXPECT_EQ(c.totals().skipped, 0u) << "the ring's arrays were read with the schema";
  EXPECT_EQ(c.totals().checks, 1u);
  EXPECT_EQ(c.totals().equal, 1u);
  // a new connection sends a new schema (here with another column order): it
  // replaces the old one
  c.on_disconnect(8);
  c.on_frame(json::parse(R"({"type":"level3","schema":{
      "open":["type","product_id","sequence","order_id","price","size","side","time"]}})"),
             9);
  auto open2 = [](int64_t seq, const char *id, const char *sz) {
    return json::array({"open", "BTC-USD", std::to_string(seq), id, "100.00", sz, "sell", "2026-11-18T17:00:01Z"});
  };
  c.on_frame(open2(20, A, "1"), 10); // buffered until the snapshot
  c.on_rest(3, snap(19, {}), 0, 11);
  ASSERT_TRUE(c.sync().synced());
  c.on_frame(open2(21, B, "1"), 12);
  c.on_frame(open2(22, C, "1"), 13);
  c.on_frame(open2(23, D, "1"), 14); // the ring holds 21, 22, 23
  ASSERT_EQ(c.sync().orders(), 4u);
  c.on_rest(3, snap(20, {{"100.00", "1", A}}), 0, 15);
  EXPECT_EQ(c.totals().skipped, 0u);
  EXPECT_EQ(c.totals().checks, 2u);
  EXPECT_EQ(c.totals().equal, 2u);
}


TEST(CbxL3Checker, EqualBooksAtTheSameSequenceAndADifferenceIsFound)
{
  auto spec = btc();
  coinbase_exchange::verify::L3Checker c(spec);
  auto snap = [](int64_t seq, std::vector<std::vector<std::string>> asks) {
    json j{{"sequence", seq}, {"time", "2026-11-18T17:00:00Z"}, {"bids", json::array()}};
    j["asks"] = json::array();
    for (auto &a : asks)
      j["asks"].push_back(a);
    return j;
  };
  auto open = [](int64_t seq, const char *id, const char *px, const char *sz) {
    return json{{"type", "open"}, {"time", "2026-11-18T17:00:01Z"}, {"product_id", "BTC-USD"}, {"sequence", seq},
                {"order_id", id}, {"price", px}, {"remaining_size", sz}, {"side", "sell"}};
  };
  auto match = [](int64_t seq, const char *maker, const char *sz) {
    return json{{"type", "match"}, {"trade_id", seq}, {"sequence", seq}, {"maker_order_id", maker},
                {"taker_order_id", C}, {"time", "2026-11-18T17:00:02Z"}, {"product_id", "BTC-USD"},
                {"size", sz}, {"price", "100.00"}, {"side", "sell"}};
  };
  c.on_frame(open(11, A, "100.00", "1"), 1); // buffered: before the first snapshot
  c.on_rest(3, snap(10, {}), 0, 2);
  ASSERT_TRUE(c.sync().synced());
  c.on_frame(open(12, B, "100.00", "2"), 3);
  c.on_frame(match(13, A, "0.4"), 4);
  // the venue's book at 12: A 1, B 2; the stream is at 13
  c.on_rest(3, snap(12, {{"100.00", "1", A}, {"100.00", "2", B}}), 0, 5);
  EXPECT_EQ(c.totals().checks, 1u);
  EXPECT_EQ(c.totals().equal, 1u);
  // a book that disagrees: B at 1.5 instead of 2
  c.on_rest(3, snap(12, {{"100.00", "1", A}, {"100.00", "1.5", B}}), 0, 6);
  EXPECT_EQ(c.totals().checks, 2u);
  EXPECT_EQ(c.totals().equal, 1u);
  EXPECT_GT(c.totals().order_diffs, 0u);
  // a snapshot ahead of the stream cannot be compared
  c.on_rest(3, snap(20, {}), 0, 7);
  EXPECT_EQ(c.totals().skipped, 1u);
}
