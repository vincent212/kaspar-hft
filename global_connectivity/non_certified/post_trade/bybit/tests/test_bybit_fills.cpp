/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitFills from the documentation's examples: the `execution` stream
// example (v5/websocket/private/execution) and the GET /v5/execution/list
// response example (v5/order/execution).

#include <gtest/gtest.h>

#include "bybit_post_trade/BybitFills.hpp"
#include "post_trade/TradeLedger.hpp"

using namespace bybit_pt;
using post_trade::msg::TradeCapture;

namespace
{
  // v5/websocket/private/execution, "Stream Example", data[0].
  const char *kStream = R"({
    "category": "linear", "symbol": "BTCUSDT", "closedSize": "0.5", "execFee": "26.3725275",
    "execId": "0ab1bdf7-4219-438b-b30a-32ec863018f7", "execPrice": "95900.1", "execQty": "0.5",
    "execType": "Trade", "execValue": "47950.05", "feeRate": "0.00055", "tradeIv": "", "markIv": "",
    "blockTradeId": "", "markPrice": "95901.48", "indexPrice": "", "underlyingPrice": "",
    "leavesQty": "0", "orderId": "9aac161b-8ed6-450d-9cab-c5cc67c21784", "orderLinkId": "",
    "orderPrice": "94942.5", "orderQty": "0.5", "orderType": "Market", "stopOrderType": "UNKNOWN",
    "side": "Sell", "execTime": "1746270400353", "isLeverage": "0", "isMaker": false,
    "seq": 140612148849382, "marketUnit": "", "execPnl": "0.05", "createType": "CreateByUser",
    "extraFees": [{"feeCoin":"USDT","feeType":"GST","subFeeType":"IND_GST","feeRate":"0.0000675","fee":"0.006403779"}],
    "feeCurrency": "USDT"})";

  // v5/order/execution, "Response Example", result.list[0] (no category).
  const char *kRest = R"({
    "symbol": "ETHPERP", "orderType": "Market", "underlyingPrice": "", "orderLinkId": "",
    "side": "Buy", "indexPrice": "", "orderId": "8c065341-7b52-4ca9-ac2c-37e31ac55c94",
    "stopOrderType": "UNKNOWN", "leavesQty": "0", "execTime": "1672282722429", "feeCurrency": "",
    "isMaker": false, "execFee": "0.071409", "feeRate": "0.0006",
    "execId": "e0cbe81d-0f18-5866-9415-cf319b5dab3b", "tradeIv": "", "blockTradeId": "",
    "markPrice": "1183.54", "execPrice": "1190.15", "markIv": "", "orderQty": "0.1",
    "orderPrice": "1236.9", "execValue": "119.015", "execType": "Trade", "execQty": "0.1",
    "closedSize": "", "extraFees": "", "seq": 4688002127})";

  Instruments ins()
  {
    Instruments m;
    m[{"linear", "BTCUSDT"}] = {"linear", "BTCUSDT", 7702, 0.001};
    m[{"linear", "ETHPERP"}] = {"linear", "ETHPERP", 7703, 0.01};
    m[{"spot", "BTCUSDT"}] = {"spot", "BTCUSDT", 7701, 0.000001};
    return m;
  }

  Parse parse(const std::string &js, TradeCapture &tc, const std::string &cat = "", bool rest = false)
  {
    std::string why;
    return to_capture(nlohmann::json::parse(js), cat, ins(), rest, tc, why);
  }
}

TEST(BybitFills, StreamExample)
{
  TradeCapture tc;
  ASSERT_EQ(parse(kStream, tc), Parse::Ok);
  EXPECT_EQ(tc.venue, en::x::BYBITF);
  EXPECT_EQ(tc.trade_id, "0ab1bdf7-4219-438b-b30a-32ec863018f7");
  EXPECT_EQ(tc.report_id, tc.trade_id);
  EXPECT_EQ(tc.report_seq, 0u);
  EXPECT_EQ(tc.event, post_trade::Event::New);
  EXPECT_EQ(tc.venue_order_id, "9aac161b-8ed6-450d-9cab-c5cc67c21784");
  EXPECT_EQ(tc.symbol, "BTCUSDT");
  EXPECT_EQ(tc.asset, 7702);
  EXPECT_EQ(tc.side, post_trade::Side::Sell);
  EXPECT_EQ(tc.qty, 500); // 0.5 BTC in 0.001
  EXPECT_EQ(tc.price_mantissa, 959001);
  EXPECT_EQ(tc.price_decimals, 1);
  EXPECT_DOUBLE_EQ(tc.price(), 95900.1);
  // 1746270400353 ms = 2025-05-03 11:06:40.353 UTC
  EXPECT_EQ(tc.trade_date, 20250503u);
  EXPECT_EQ(tc.trade_time_ns, ((11 * 3600 + 6 * 60 + 40) * 1000LL + 353) * 1000000LL);
  EXPECT_FALSE(tc.poss_dup);
  EXPECT_FALSE(tc.raw.empty());
}

TEST(BybitFills, RestExampleTakesTheRequestsCategory)
{
  TradeCapture tc;
  ASSERT_EQ(parse(kRest, tc, "linear", true), Parse::Ok);
  EXPECT_EQ(tc.venue, en::x::BYBITF);
  EXPECT_EQ(tc.symbol, "ETHPERP");
  EXPECT_EQ(tc.side, post_trade::Side::Buy);
  EXPECT_EQ(tc.qty, 10);
  EXPECT_EQ(tc.price_mantissa, 119015);
  EXPECT_EQ(tc.price_decimals, 2);
  EXPECT_EQ(tc.trade_date, 20221229u); // 1672282722429 = 2022-12-29 02:58:42.429 UTC
  EXPECT_TRUE(tc.poss_dup);
  // The same element with no category from anywhere is not mapped.
  EXPECT_EQ(parse(kRest, tc, ""), Parse::NoInstrument);
}

TEST(BybitFills, SpotGoesToBybitSpotVenue)
{
  auto j = nlohmann::json::parse(kStream);
  j["category"] = "spot";
  j["execQty"] = "0.000123";
  TradeCapture tc;
  ASSERT_EQ(parse(j.dump(), tc), Parse::Ok);
  EXPECT_EQ(tc.venue, en::x::BYBIT);
  EXPECT_EQ(tc.asset, 7701);
  EXPECT_EQ(tc.qty, 123);
}

TEST(BybitFills, ExecTypes)
{
  auto j = nlohmann::json::parse(kStream);
  TradeCapture tc;
  // Position-changing types are trades. BustTrade is a takeover liquidation.
  for (auto t : {"Trade", "AdlTrade", "BustTrade", "Delivery", "BlockTrade", "MovePosition", "FutureSpread"})
  {
    j["execType"] = t;
    EXPECT_EQ(parse(j.dump(), tc), Parse::Ok) << t;
    EXPECT_EQ(tc.event, post_trade::Event::New) << t;
  }
  for (auto t : {"Funding", "Settle", "UNKNOWN", "SomethingNew"})
  {
    j["execType"] = t;
    EXPECT_EQ(parse(j.dump(), tc), Parse::NotATrade) << t;
  }
}

TEST(BybitFills, BadInputs)
{
  TradeCapture tc;
  auto j = nlohmann::json::parse(kStream);
  j["execQty"] = "0.0005"; // half a unit of 0.001
  EXPECT_EQ(parse(j.dump(), tc), Parse::BadQuantity);
  j = nlohmann::json::parse(kStream);
  j["symbol"] = "ETHUSDT";
  EXPECT_EQ(parse(j.dump(), tc), Parse::NoInstrument);
  j = nlohmann::json::parse(kStream);
  j.erase("execId");
  EXPECT_EQ(parse(j.dump(), tc), Parse::Malformed);
  j = nlohmann::json::parse(kStream);
  j["execPrice"] = "-1";
  EXPECT_EQ(parse(j.dump(), tc), Parse::Malformed);
  j = nlohmann::json::parse(kStream);
  j["side"] = "None";
  EXPECT_EQ(parse(j.dump(), tc), Parse::Malformed);
  j = nlohmann::json::parse(kStream);
  j["category"] = "inverse";
  EXPECT_EQ(parse(j.dump(), tc), Parse::NoInstrument);
}

TEST(BybitFills, Decimal)
{
  int64_t m;
  int d;
  ASSERT_TRUE(parse_decimal("95900.10", m, d));
  EXPECT_EQ(m, 9590010);
  EXPECT_EQ(d, 2);
  ASSERT_TRUE(parse_decimal("7", m, d));
  EXPECT_EQ(m, 7);
  EXPECT_EQ(d, 0);
  EXPECT_FALSE(parse_decimal("", m, d));
  EXPECT_FALSE(parse_decimal("1.2.3", m, d));
  EXPECT_FALSE(parse_decimal("1e5", m, d));
}

// The stream copy and the REST copy of one execution are one trade.
TEST(BybitFills, StreamAndRestCopiesAreOneTradeInTheLedger)
{
  post_trade::TradeLedger led;
  TradeCapture a, b;
  ASSERT_EQ(parse(kStream, a), Parse::Ok);
  auto j = nlohmann::json::parse(kStream);
  j.erase("category");
  ASSERT_EQ(parse(j.dump(), b, "linear", true), Parse::Ok);
  EXPECT_EQ(led.apply(a).what, post_trade::Applied::Added);
  EXPECT_EQ(led.apply(b).what, post_trade::Applied::Duplicate);
  EXPECT_EQ(led.net_qty("BTCUSDT"), -500);
}
