/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// TradeCaptureReport and ExecutionReport into TradeCapture, the trade id
// rule, and the trading-day boundary.

#include <gtest/gtest.h>

#include "lmax_fix/LmaxFix.hpp"
#include "lmax_post_trade/TradeMap.hpp"
#include "post_trade/TradeLedger.hpp"

using namespace lmax;
using namespace lmax::pt;
using post_trade::Event;
using post_trade::Side;

namespace
{
  MapContext ctx()
  {
    MapContext c;
    c.venue = en::x::UNI;
    c.instruments["4001"] = {7001, 0.1};
    return c;
  }
  om::TradeCaptureReport tcr()
  {
    om::TradeCaptureReport r;
    r.req_id = "T1";
    r.exec_id = "E000000000000009";
    r.secondary_exec_id = "M000000000000001";
    r.security_id = "4001";
    r.last_qty_text = "0.4";
    r.last_px_text = "1.15229";
    r.last_qty = 0.4;
    r.last_px = 1.15229;
    r.trade_date = "20261118";
    r.transact_time = "20261118-10:00:00.123";
    r.sides.push_back({'2', "1000000007", "42", "ACC"});
    return r;
  }
  om::ExecReport exec(char type)
  {
    om::ExecReport r;
    r.exec_type = type;
    r.exec_id = "E000000000000010";
    r.secondary_exec_id = "M000000000000001";
    r.order_id = "1000000007";
    r.clordid = "42";
    r.side = '2';
    r.security_id = "4001";
    r.has_last = true;
    r.last_qty = 0.4;
    r.last_px = 1.15229;
    r.transact_time = "20261118-10:00:00.123";
    r.settl_date = "20261120";
    return r;
  }
}

TEST(LmaxTradeMap, TradeCaptureReport)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(from_tcr(tcr(), ctx(), t, err)) << err;
  EXPECT_EQ(t.trade_id, "1000000007:M000000000000001");
  EXPECT_EQ(t.report_id, "N:1000000007:M000000000000001");
  EXPECT_EQ(t.event, Event::New);
  EXPECT_EQ(t.side, Side::Sell);
  EXPECT_EQ(t.asset, 7001);
  EXPECT_EQ(t.qty, 4);
  EXPECT_EQ(t.price_mantissa, 115229);
  EXPECT_EQ(t.price_decimals, 5);
  EXPECT_EQ(t.trade_date, 20261118u);
  EXPECT_EQ(t.trade_time_ns, (10LL * 3600) * 1000000000LL + 123000000LL);
  EXPECT_EQ(t.account, "ACC");
  EXPECT_EQ(t.venue_order_id, "1000000007");
}

TEST(LmaxTradeMap, ExecutionReportOfTheSameFillIsTheSameReport)
{
  post_trade::msg::TradeCapture a, b;
  std::string err;
  ASSERT_TRUE(from_tcr(tcr(), ctx(), a, err));
  ASSERT_TRUE(from_exec(exec('F'), ctx(), b, err)) << err;
  EXPECT_EQ(a.trade_id, b.trade_id);
  EXPECT_EQ(a.report_id, b.report_id);
  EXPECT_EQ(b.settle_date, 20261120u);
  post_trade::TradeLedger l;
  EXPECT_EQ(l.apply(b).what, post_trade::Applied::Added);
  EXPECT_EQ(l.apply(a).what, post_trade::Applied::Duplicate);
  EXPECT_EQ(l.net_qty("4001"), -4);
}

TEST(LmaxTradeMap, TradeIdFallsBackToExecId)
{
  EXPECT_EQ(trade_id("O1", "0", "E1"), "O1:E1");
  EXPECT_EQ(trade_id("O1", "", "E1"), "O1:E1");
  EXPECT_EQ(trade_id("O1", "M1", "E1"), "O1:M1");
}

TEST(LmaxTradeMap, CorrectionAndCancel)
{
  post_trade::msg::TradeCapture n, g, h;
  std::string err;
  ASSERT_TRUE(from_exec(exec('F'), ctx(), n, err));
  auto ge = exec('G');
  ge.exec_id = "E2";
  ge.last_qty = 0.3;
  ASSERT_TRUE(from_exec(ge, ctx(), g, err)) << err;
  EXPECT_EQ(g.event, Event::Amend);
  EXPECT_EQ(g.ref_trade_id, n.trade_id);
  EXPECT_EQ(g.qty, 3);
  auto he = exec('H');
  he.exec_id = "E3";
  he.has_last = false;
  ASSERT_TRUE(from_exec(he, ctx(), h, err)) << err;
  EXPECT_EQ(h.event, Event::Cancel);
  post_trade::TradeLedger l;
  EXPECT_EQ(l.apply(n).what, post_trade::Applied::Added);
  EXPECT_EQ(l.apply(g).what, post_trade::Applied::Amended);
  EXPECT_EQ(l.net_qty("4001"), -3);
  auto rm = l.apply(h);
  EXPECT_EQ(rm.what, post_trade::Applied::Removed);
  EXPECT_EQ(rm.out.qty, 3); // economics from the ledger
  EXPECT_EQ(l.net_qty("4001"), 0);
  // Without a SecondaryExecID a correction cannot be linked.
  auto bad = exec('H');
  bad.secondary_exec_id = "0";
  EXPECT_FALSE(from_exec(bad, ctx(), h, err));
}

TEST(LmaxTradeMap, RefusesWhatItCannotMap)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  auto r = tcr();
  r.last_qty = 0.45;
  r.last_qty_text = "0.45";
  EXPECT_FALSE(from_tcr(r, ctx(), t, err)); // not a whole unit
  r = tcr();
  r.security_id = "4999";
  EXPECT_FALSE(from_tcr(r, ctx(), t, err));
  r = tcr();
  r.sides.clear();
  EXPECT_FALSE(from_tcr(r, ctx(), t, err));
  EXPECT_FALSE(from_exec(exec('0'), ctx(), t, err));
}

TEST(LmaxTradeMap, DecimalText)
{
  int64_t m;
  int d;
  ASSERT_TRUE(decimal_text("1.15229", m, d));
  EXPECT_EQ(m, 115229);
  EXPECT_EQ(d, 5);
  ASSERT_TRUE(decimal_text("155", m, d));
  EXPECT_EQ(m, 155);
  EXPECT_EQ(d, 0);
  ASSERT_TRUE(decimal_text("-0.5", m, d));
  EXPECT_EQ(m, -5);
  EXPECT_FALSE(decimal_text("1e5", m, d));
  EXPECT_FALSE(decimal_text("", m, d));
}

TEST(LmaxTradeMap, TradingDayStartsAt1700NewYork)
{
  // 2026-11-18 is in EST (UTC-5): 17:00 New York = 22:00 UTC.
  const uint64_t t1 = fix::utc_timestamp_ns("20261118-21:59:59.000");
  EXPECT_EQ(utc_timestamp(trading_day_start_ns(t1)), "20261117-22:00:00.000");
  const uint64_t t2 = fix::utc_timestamp_ns("20261118-22:00:00.000");
  EXPECT_EQ(utc_timestamp(trading_day_start_ns(t2)), "20261118-22:00:00.000");
  // 2026-07-01 is in EDT (UTC-4): 17:00 New York = 21:00 UTC.
  const uint64_t t3 = fix::utc_timestamp_ns("20260701-12:00:00.000");
  EXPECT_EQ(utc_timestamp(trading_day_start_ns(t3)), "20260630-21:00:00.000");
  // DST started 2026-03-08 at 07:00 UTC.
  const uint64_t t4 = fix::utc_timestamp_ns("20260309-12:00:00.000");
  EXPECT_EQ(utc_timestamp(trading_day_start_ns(t4)), "20260308-21:00:00.000");
}
