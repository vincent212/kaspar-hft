/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The spot WebSocket v2 and Futures REST messages, on the documentation's
// examples (exchange/api-reference/spot-websocket-v2/{add_order,cancel_order,
// executions}; api-reference/order-management/{send-order,cancel-order,
// get-specific-orders-status}; api-reference/historical-data/get-your-fills).

#include <gtest/gtest.h>

#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenFuturesCodec.hpp"
#include "kraken_oe/KrakenSpotCodec.hpp"

using namespace kraken::oe;
using json = nlohmann::json;

// ---- spot ---------------------------------------------------------------------------------

TEST(SpotCodec, AddOrderCarriesDocumentedFieldsWithExactDecimals)
{
  NewOrder o;
  o.cl_ord_id = "k3x9";
  o.symbol = "BTC/USD";
  o.buy = true;
  o.qty = "1.20000000";
  o.price = "26500.4";
  auto j = kraken::parse_raw(spot::add_order(o, "G38a1tGFzqGiUCmnegBcm8d4nfP3tytiNQz6tkCBYXY", 123456789));
  EXPECT_EQ(j["method"], "add_order");
  EXPECT_EQ(j["req_id"], 123456789);
  const auto &p = j["params"];
  EXPECT_EQ(p["order_type"], "limit");
  EXPECT_EQ(p["side"], "buy");
  EXPECT_EQ(p["symbol"], "BTC/USD");
  EXPECT_EQ(p["order_qty"], "1.20000000"); // a JSON number with its text kept
  EXPECT_EQ(p["limit_price"], "26500.4");
  EXPECT_EQ(p["time_in_force"], "gtc");
  EXPECT_EQ(p["cl_ord_id"], "k3x9");
  EXPECT_EQ(p["token"], "G38a1tGFzqGiUCmnegBcm8d4nfP3tytiNQz6tkCBYXY");
  EXPECT_FALSE(p.contains("post_only"));
  // The raw text: numbers, not strings.
  EXPECT_NE(spot::add_order(o, "t", 1).find(R"("order_qty":1.20000000)"), std::string::npos);

  o.market = true;
  o.tif = Tif::IOC;
  o.post_only = false;
  auto m = kraken::parse_raw(spot::add_order(o, "t", 2));
  EXPECT_EQ(m["params"]["order_type"], "market");
  EXPECT_FALSE(m["params"].contains("limit_price"));
  EXPECT_EQ(m["params"]["time_in_force"], "ioc");
}

TEST(SpotCodec, CancelByVenueIdElseClientId)
{
  auto a = json::parse(spot::cancel_order("OM5CRX-N2HAL-GFGWE9", "c1", "tok", 7));
  EXPECT_EQ(a["params"]["order_id"], json::array({"OM5CRX-N2HAL-GFGWE9"}));
  EXPECT_FALSE(a["params"].contains("cl_ord_id"));
  auto b = json::parse(spot::cancel_order("", "c1", "tok", 8));
  EXPECT_EQ(b["params"]["cl_ord_id"], json::array({"c1"}));
}

TEST(SpotCodec, AddOrderResponsesFromDocumentation)
{
  auto lookup = [](uint64_t id) { return id == 123456789 ? std::string("k3x9") : std::string(); };
  auto ok = spot::parse(kraken::parse_raw(R"({"method":"add_order","req_id":123456789,
      "result":{"order_id":"AA5JGQ-SBMRC-SCJ7J7","order_userref":100054},"success":true,
      "time_in":"2023-09-21T14:15:07.197274Z","time_out":"2023-09-21T14:15:07.205301Z"})"),
                        lookup);
  ASSERT_EQ(ok.reports.size(), 1u);
  EXPECT_EQ(ok.reports[0].kind, Report::Accepted);
  EXPECT_EQ(ok.reports[0].order_id, "AA5JGQ-SBMRC-SCJ7J7");
  EXPECT_EQ(ok.reports[0].cl_ord_id, "k3x9"); // from req_id
  auto err = spot::parse(kraken::parse_raw(R"({"error":"EOrder:Insufficient funds","method":"add_order",
      "req_id":123456789,"success":false,"time_in":"2023-09-21T14:15:07.197274Z",
      "time_out":"2023-09-21T14:15:07.205301Z"})"),
                         lookup);
  ASSERT_EQ(err.reports.size(), 1u);
  EXPECT_EQ(err.reports[0].kind, Report::Rejected);
  EXPECT_EQ(err.reports[0].cl_ord_id, "k3x9");
  EXPECT_EQ(err.reports[0].reason, "EOrder:Insufficient funds");
}

TEST(SpotCodec, CancelResponses)
{
  auto lookup = [](uint64_t) { return std::string("c1"); };
  auto ok = spot::parse(kraken::parse_raw(R"({"method":"cancel_order","req_id":123456789,
      "result":{"order_id":"OLUMT4-UTEGU-ZYM7E9"},"success":true})"),
                        lookup);
  EXPECT_TRUE(ok.reports.empty()); // the executions channel reports the cancel
  auto bad = spot::parse(kraken::parse_raw(R"({"error":"EOrder:Unknown order","method":"cancel_order",
      "req_id":5,"success":false})"),
                         lookup);
  ASSERT_EQ(bad.reports.size(), 1u);
  EXPECT_EQ(bad.reports[0].kind, Report::CancelRejected);
  EXPECT_EQ(bad.reports[0].cl_ord_id, "c1");
}

TEST(SpotCodec, ExecutionsExamplesFromDocumentation)
{
  auto live = spot::parse(kraken::parse_raw(R"({"channel":"executions","type":"update","data":[
      {"timestamp":"2023-09-22T10:33:05.709982Z","order_status":"new","exec_type":"new","order_userref":3,
       "order_id":"OK4GJX-KSTLS-7DZZO5"}],"sequence":9})"),
                          {});
  ASSERT_EQ(live.reports.size(), 1u);
  EXPECT_EQ(live.reports[0].kind, Report::Accepted);
  EXPECT_EQ(live.reports[0].order_id, "OK4GJX-KSTLS-7DZZO5");
  ASSERT_TRUE(live.sequence);
  EXPECT_EQ(*live.sequence, 9u);

  auto ex = spot::parse(kraken::parse_raw(R"({"channel":"executions","type":"update","data":[
      {"order_id":"OK4GJX-KSTLS-7DZZO5","order_userref":3,"exec_id":"TGBB7L-HT5LX-J3BZ4A","exec_type":"trade",
       "trade_id":62887576,"symbol":"BTC/USD","side":"sell","last_qty":0.005,"last_price":26599.9,
       "liquidity_ind":"t","cost":132.9995,"order_type":"limit","timestamp":"2023-09-22T10:33:05.709993Z",
       "order_status":"partially_filled","cum_qty":0.005,"cum_cost":132.9995,"avg_price":26599.9,
       "fee_usd_equiv":0.3458,"fees":[{"asset":"USD","qty":0.3458}]}],"sequence":10})"),
                        {});
  ASSERT_EQ(ex.reports.size(), 1u);
  const auto &t = ex.reports[0];
  EXPECT_EQ(t.kind, Report::Trade);
  EXPECT_EQ(t.exec_id, "TGBB7L-HT5LX-J3BZ4A");
  EXPECT_EQ(t.trade_id, "62887576");
  EXPECT_EQ(t.last_qty, "0.005");
  EXPECT_EQ(t.last_price, "26599.9");
  EXPECT_FALSE(t.buy);
  EXPECT_FALSE(t.maker);
  EXPECT_EQ(t.time_ns, kraken::parse_rfc3339("2023-09-22T10:33:05.709993Z"));
  EXPECT_EQ(*ex.sequence, 10u);
}

TEST(SpotCodec, SnapshotOpenOrderIsAcceptedAndOtherTypesMap)
{
  auto s = spot::parse(kraken::parse_raw(R"({"channel":"executions","type":"snapshot","data":[
      {"order_id":"O1","cl_ord_id":"c1","order_status":"new","symbol":"BTC/USD"},
      {"order_id":"O2","exec_type":"canceled","order_status":"canceled","reason":"User requested"},
      {"order_id":"O3","exec_type":"expired","order_status":"expired"},
      {"order_id":"O4","exec_type":"filled","order_status":"filled"},
      {"order_id":"O5","exec_type":"amended","order_status":"new"},
      {"order_id":"O6","exec_type":"pending_new","order_status":"pending_new"}],"sequence":1})"),
                       {});
  ASSERT_EQ(s.reports.size(), 6u);
  EXPECT_EQ(s.reports[0].kind, Report::Accepted);
  EXPECT_TRUE(s.reports[0].snapshot);
  EXPECT_EQ(s.reports[1].kind, Report::Canceled);
  EXPECT_EQ(s.reports[1].reason, "User requested");
  EXPECT_EQ(s.reports[2].kind, Report::Canceled);
  EXPECT_EQ(s.reports[3].kind, Report::Filled);
  EXPECT_EQ(s.reports[4].kind, Report::Restated);
  EXPECT_EQ(s.reports[5].kind, Report::Other);
}

TEST(SpotCodec, SubscribeAcknowledgementAndError)
{
  auto ok = spot::parse(kraken::parse_raw(R"({"method":"subscribe","result":{"channel":"executions",
      "maxratecount":180,"snapshot":true},"success":true,"time_in":"x","time_out":"y"})"),
                        {});
  EXPECT_TRUE(ok.subscribed_executions);
  auto bad = spot::parse(kraken::parse_raw(R"({"error":"EAccount:Invalid permissions","method":"subscribe",
      "success":false})"),
                         {});
  EXPECT_EQ(bad.subscribe_error, "EAccount:Invalid permissions");
  auto sub = json::parse(spot::subscribe_executions("tok", true, true));
  EXPECT_EQ(sub["params"]["channel"], "executions");
  EXPECT_EQ(sub["params"]["snap_trades"], true);
}

// ---- futures ------------------------------------------------------------------------------

TEST(FuturesCodec, SendOrderBodies)
{
  NewOrder o;
  o.cl_ord_id = "abc";
  o.symbol = "PF_XBTUSD";
  o.qty = "0.0001";
  o.price = "10000";
  EXPECT_EQ(futures::send_order_body(o), "orderType=lmt&symbol=PF_XBTUSD&side=buy&size=0.0001&limitPrice=10000&cliOrdId=abc");
  o.post_only = true;
  EXPECT_EQ(futures::send_order_body(o).substr(0, 15), "orderType=post&");
  o.post_only = false;
  o.tif = Tif::IOC;
  EXPECT_EQ(futures::send_order_body(o).substr(0, 14), "orderType=ioc&");
  o.tif = Tif::FOK;
  EXPECT_EQ(futures::send_order_body(o).substr(0, 14), "orderType=fok&");
  o.market = true;
  o.buy = false;
  EXPECT_EQ(futures::send_order_body(o), "orderType=mkt&symbol=PF_XBTUSD&side=sell&size=0.0001&cliOrdId=abc");
  EXPECT_EQ(futures::cancel_order_body("cb4e34f6-4eb3-4d4b-9724-4c3035b99d47", "x"),
            "order_id=cb4e34f6-4eb3-4d4b-9724-4c3035b99d47");
  EXPECT_EQ(futures::cancel_order_body("", "1234568"), "cliOrdId=1234568");
  EXPECT_EQ(futures::orders_status_body({"a", "b"}, {"c"}), "orderIds=a&orderIds=b&cliOrdIds=c");
}

TEST(FuturesCodec, SendOrderExamplesFromDocumentation)
{
  // PlacedOrderExample
  auto placed = futures::parse_send(kraken::parse_raw(R"({"result":"success","sendStatus":{
      "order_id":"179f9af8-e45e-469d-b3e9-2fd4675cb7d0","status":"placed","receivedTime":"2019-09-05T16:33:50.734Z",
      "orderEvents":[{"type":"PLACE","order":{"orderId":"179f9af8-e45e-469d-b3e9-2fd4675cb7d0","cliOrdId":null,
      "type":"lmt","symbol":"PI_XBTUSD","side":"buy","quantity":10000,"filled":0,"limitPrice":9400,
      "reduceOnly":false,"timestamp":"2019-09-05T16:33:50.734Z","lastUpdateTimestamp":"2019-09-05T16:33:50.734Z"},
      "reducedQuantity":null}]},"serverTime":"2019-09-05T16:33:50.734Z"})"),
                                    "c1");
  ASSERT_EQ(placed.size(), 1u);
  EXPECT_EQ(placed[0].kind, Report::Accepted);
  EXPECT_EQ(placed[0].order_id, "179f9af8-e45e-469d-b3e9-2fd4675cb7d0");
  EXPECT_EQ(placed[0].cl_ord_id, "c1");

  // RejectedOrderExample
  auto rej = futures::parse_send(kraken::parse_raw(R"({"result":"success","sendStatus":{
      "order_id":"614a5298-0071-450f-83c6-0617ce8c6bc4","status":"iocWouldNotExecute",
      "receivedTime":"2019-09-05T16:32:54.076Z","orderEvents":[{"type":"REJECT",
      "uid":"614a5298-0071-450f-83c6-0617ce8c6bc4","reason":"IOC_WOULD_NOT_EXECUTE","order":{
      "orderId":"614a5298-0071-450f-83c6-0617ce8c6bc4","cliOrdId":null,"type":"lmt","symbol":"PI_XBTUSD",
      "side":"buy","quantity":10000,"filled":0,"limitPrice":9400,"reduceOnly":true,
      "timestamp":"2019-09-05T16:32:54.076Z","lastUpdateTimestamp":"2019-09-05T16:32:54.076Z"}}]},
      "serverTime":"2019-09-05T16:32:54.077Z"})"),
                                 "c2");
  ASSERT_EQ(rej.size(), 1u);
  EXPECT_EQ(rej[0].kind, Report::Rejected);
  EXPECT_EQ(rej[0].reason, "iocWouldNotExecute (IOC_WOULD_NOT_EXECUTE)");

  // ExecutedOrderExample: accepted; the execution itself comes from /fills.
  auto exe = futures::parse_send(kraken::parse_raw(R"({"result":"success","sendStatus":{
      "order_id":"61ca5732-3478-42fe-8362-abbfd9465294","status":"placed","receivedTime":"2019-12-11T17:17:33.888Z",
      "orderEvents":[{"type":"EXECUTION","executionId":"e1ec9f63-2338-4c44-b40a-43486c6732d7","price":7244.5,
      "amount":10,"orderPriorEdit":null,"orderPriorExecution":{"orderId":"61ca5732-3478-42fe-8362-abbfd9465294",
      "cliOrdId":null,"type":"lmt","symbol":"PI_XBTUSD","side":"buy","quantity":10,"filled":0,"limitPrice":7500,
      "reduceOnly":false,"timestamp":"2019-12-11T17:17:33.888Z","lastUpdateTimestamp":"2019-12-11T17:17:33.888Z"},
      "takerReducedQuantity":null}]},"serverTime":"2019-12-11T17:17:33.888Z"})"),
                                 "c3");
  ASSERT_EQ(exe.size(), 1u);
  EXPECT_EQ(exe[0].kind, Report::Accepted);

  // exchange/guides/futures/rest: insufficientAvailableFunds; and an error result.
  auto funds = futures::parse_send(kraken::parse_raw(R"({"result":"success","serverTime":"2016-02-25T09:45:53.818Z",
      "sendStatus":{"receivedTime":"2016-02-25T09:45:53.601Z","status":"insufficientAvailableFunds"}})"),
                                   "c4");
  EXPECT_EQ(funds[0].kind, Report::Rejected);
  EXPECT_EQ(funds[0].reason, "insufficientAvailableFunds");
  auto lim = futures::parse_send(
      kraken::parse_raw(R"({"result":"error","serverTime":"2016-02-25T09:45:53.818Z","error":"apiLimitExceeded"})"),
      "c5");
  EXPECT_EQ(lim[0].kind, Report::Rejected);
  EXPECT_EQ(lim[0].reason, "apiLimitExceeded");
}

TEST(FuturesCodec, CancelExampleAndOutcomes)
{
  auto c = futures::parse_cancel(kraken::parse_raw(R"({"result":"success","cancelStatus":{"status":"cancelled",
      "order_id":"cb4e34f6-4eb3-4d4b-9724-4c3035b99d47","receivedTime":"2020-07-22T13:26:20.806Z",
      "orderEvents":[{"type":"CANCEL","uid":"cb4e34f6-4eb3-4d4b-9724-4c3035b99d47","order":{
      "orderId":"cb4e34f6-4eb3-4d4b-9724-4c3035b99d47","cliOrdId":"1234568","type":"lmt","symbol":"PI_XBTUSD",
      "side":"buy","quantity":5500,"filled":0,"limitPrice":8000,"reduceOnly":false,
      "timestamp":"2020-07-22T13:25:56.366Z","lastUpdateTimestamp":"2020-07-22T13:25:56.366Z"}}]},
      "serverTime":"2020-07-22T13:26:20.806Z"})"),
                                 "1234568");
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].kind, Report::Canceled);
  EXPECT_EQ(c[0].order_id, "cb4e34f6-4eb3-4d4b-9724-4c3035b99d47");
  auto f = futures::parse_cancel(kraken::parse_raw(R"({"result":"success","cancelStatus":{"status":"filled"}})"), "x");
  EXPECT_EQ(f[0].kind, Report::CancelRejected);
  EXPECT_EQ(f[0].reason, "filled");
  auto n = futures::parse_cancel(kraken::parse_raw(R"({"result":"success","cancelStatus":{"status":"notFound"}})"), "x");
  EXPECT_EQ(n[0].kind, Report::CancelRejected);
}

TEST(FuturesCodec, FillsExampleFromDocumentation)
{
  auto f = futures::parse_fills(kraken::parse_raw(R"({"result":"success","serverTime":"2021-11-18T02:40:00.000Z",
      "fills":[{"fill_id":"98e3deeb-0385-4b25-b15e-7e8453512cb2","fillTime":"2021-11-18T02:39:41.826Z",
      "fillType":"maker","order_id":"06b9d509-965c-4788-b317-0e5ca11d56fb","price":47000,"side":"buy",
      "size":10,"symbol":"PI_XBTUSD","realized_pnl":12.5}]})"));
  ASSERT_EQ(f.size(), 1u);
  EXPECT_EQ(f[0].kind, Report::Trade);
  EXPECT_EQ(f[0].exec_id, "98e3deeb-0385-4b25-b15e-7e8453512cb2");
  EXPECT_EQ(f[0].order_id, "06b9d509-965c-4788-b317-0e5ca11d56fb");
  EXPECT_EQ(f[0].last_qty, "10");
  EXPECT_EQ(f[0].last_price, "47000");
  EXPECT_TRUE(f[0].maker);
  EXPECT_TRUE(f[0].buy);
  EXPECT_EQ(f[0].time_ns, kraken::parse_rfc3339("2021-11-18T02:39:41.826Z"));
}

TEST(FuturesCodec, OrdersStatusMapping)
{
  auto r = futures::parse_orders_status(kraken::parse_raw(R"({"result":"success","serverTime":"x","orders":[
      {"order":{"type":"ORDER","orderId":"o1","cliOrdId":"c1","symbol":"PF_XBTUSD","side":"buy","quantity":0.001,
       "filled":0,"limitPrice":10000,"reduceOnly":false,"timestamp":"t","lastUpdateTimestamp":"t","algoId":null,
       "priceTriggerOptions":null,"triggerTime":null},"status":"ENTERED_BOOK","updateReason":null,"error":null},
      {"order":{"orderId":"o2","cliOrdId":null},"status":"CANCELLED","updateReason":"CANCELLED_BY_USER","error":null},
      {"order":{"orderId":"o3","cliOrdId":"c3"},"status":"FULLY_EXECUTED","updateReason":"FULL_FILL","error":null},
      {"order":{"orderId":"o4","cliOrdId":"c4"},"status":"REJECTED","updateReason":null,"error":"POST_WOULD_EXECUTE"},
      {"order":{"orderId":"o5","cliOrdId":"c5"},"status":"TRIGGER_PLACED","updateReason":null,"error":null}]})"));
  ASSERT_EQ(r.size(), 5u);
  EXPECT_EQ(r[0].kind, Report::Accepted);
  EXPECT_EQ(r[1].kind, Report::Canceled);
  EXPECT_EQ(r[1].reason, "CANCELLED_BY_USER");
  EXPECT_TRUE(r[1].cl_ord_id.empty());
  EXPECT_EQ(r[2].kind, Report::Filled);
  EXPECT_EQ(r[3].kind, Report::Rejected);
  EXPECT_EQ(r[3].reason, "POST_WOULD_EXECUTE");
  EXPECT_EQ(r[4].kind, Report::Other);
}
