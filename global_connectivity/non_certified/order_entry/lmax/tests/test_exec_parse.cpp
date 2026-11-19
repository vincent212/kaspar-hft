/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The generated Broker Trading dictionary, the requests as built, and the
// report parsers. LMAX publishes no sample messages: each report is written
// from the fields its spec lists.

#include <gtest/gtest.h>

#include "quickfix/DataDictionary.h"
#include "quickfix/Message.h"

#include "FixText.hpp"
#include "lmax_om/ExecParse.hpp"

using namespace lmax::om;
using lmax::test::fixmsg;
using lmax::test::visible;

namespace
{
  const FIX::DataDictionary &dict()
  {
    static FIX::DataDictionary d(LMAX_TRADING_DICT);
    return d;
  }
  FIX::Message parse(const std::string &text)
  {
    FIX::Message m(text, dict(), true);
    dict().validate(m);
    return m;
  }
  std::string wire(FIX::Message m)
  {
    m.getHeader().setField(49, "CLIENT");
    m.getHeader().setField(56, "LMXBD");
    m.getHeader().setField(34, "5");
    m.getHeader().setField(52, "20261118-10:00:00.000");
    return m.toString();
  }
}

TEST(LmaxTradingDictionary, RequestsAsBuiltValidate)
{
  NewOrderReq n;
  n.clordid = "12345678901234567890";
  n.security_id = "4001";
  n.account = "ACC";
  n.side = '2';
  n.qty = "0.5";
  n.ord_type = '2';
  n.px = "1.15230";
  n.tif = '3';
  n.reinstate = true;
  n.transact_time = "20261118-10:00:00.000";
  const std::string d = wire(build_new_order(n));
  ASSERT_NO_THROW(parse(d)) << visible(d);
  EXPECT_NE(visible(d).find("|11=12345678901234567890|48=4001|22=8|1=ACC|54=2|18=H|60=20261118-10:00:00.000|38=0.5|40=2|44=1.15230|59=3|"),
            std::string::npos)
      << visible(d);
  n.reinstate = false;
  EXPECT_EQ(visible(wire(build_new_order(n))).find("|18="), std::string::npos); // default: cancel on disconnect

  CancelReq c{"2", "1", "4001", "", "0.5", "20261118-10:00:00.000", '1'};
  const std::string f = wire(build_cancel(c));
  ASSERT_NO_THROW(parse(f)) << visible(f);
  EXPECT_NE(visible(f).find("|41=1|11=2|48=4001|22=8|54=1|60="), std::string::npos) << visible(f);

  ReplaceReq r;
  r.clordid = "3";
  r.orig_clordid = "2";
  r.security_id = "4001";
  r.qty = "1.0";
  r.px = "1.15240";
  const std::string g = wire(build_replace(r));
  ASSERT_NO_THROW(parse(g)) << visible(g);
  EXPECT_NE(visible(g).find("|41=2|11=3|48=4001|22=8|54=1|60="), std::string::npos) << visible(g);

  StatusReq h{"3", "4001", "", "S1", '1'};
  const std::string hs = wire(build_status(h));
  ASSERT_NO_THROW(parse(hs)) << visible(hs);
  EXPECT_NE(visible(hs).find("|11=3|790=S1|48=4001|22=8|54=1|"), std::string::npos) << visible(hs);

  TcrReq t{"T1", "", true, "20261118-00:00:00.000", ""};
  const std::string ad = wire(build_tcr_request(t));
  ASSERT_NO_THROW(parse(ad)) << visible(ad);
  EXPECT_NE(visible(ad).find("|568=T1|569=1|263=1|580=1|60=20261118-00:00:00.000|"), std::string::npos)
      << visible(ad);
}

TEST(LmaxExecParse, TradeReport)
{
  auto m = parse(fixmsg("8",
                        "37=1000000007|527=M000000000000001|11=42|17=E000000000000009|150=F|39=1|1=ACC|"
                        "48=4001|22=8|54=1|38=1.0|40=2|44=1.15230|59=0|32=0.4|31=1.15229|151=0.6|14=0.4|"
                        "6=1.15229|60=20261118-10:00:00.123|12=0.02|13=3|479=USD|",
                        12, "LMXBD"));
  ExecReport r;
  std::string err;
  ASSERT_TRUE(parse_exec(m, r, err)) << err;
  EXPECT_EQ(r.order_id, "1000000007");
  EXPECT_EQ(r.secondary_exec_id, "M000000000000001");
  EXPECT_EQ(r.clordid, "42");
  EXPECT_EQ(r.exec_id, "E000000000000009");
  EXPECT_EQ(r.exec_type, 'F');
  EXPECT_EQ(r.ord_status, '1');
  EXPECT_TRUE(r.has_last);
  EXPECT_DOUBLE_EQ(r.last_qty, 0.4);
  EXPECT_DOUBLE_EQ(r.last_px, 1.15229);
  EXPECT_EQ(r.last_px_text, "1.15229");
  EXPECT_DOUBLE_EQ(r.cum, 0.4);
  EXPECT_DOUBLE_EQ(r.leaves, 0.6);
  EXPECT_EQ(r.commission, "0.02");
  EXPECT_EQ(r.comm_ccy, "USD");
  EXPECT_EQ(r.seq, 12u);
}

TEST(LmaxExecParse, MissingRequiredFieldsAreReportedNotThrown)
{
  // No OrderID (37).
  FIX::Message m(fixmsg("8", "17=E1|150=0|39=0|54=1|151=1|14=0|6=0|48=4001|22=8|"), dict(), true);
  ExecReport r;
  std::string err;
  EXPECT_FALSE(parse_exec(m, r, err));
  EXPECT_NE(err.find("37"), std::string::npos);
  // A trade without LastQty.
  FIX::Message t(fixmsg("8", "37=1|17=E1|150=F|39=1|54=1|151=1|14=0|6=0|48=4001|22=8|31=1.1|"), dict(), true);
  EXPECT_FALSE(parse_exec(t, r, err));
}

TEST(LmaxExecParse, CancelReject)
{
  auto m = parse(fixmsg("9", "37=1000000007|11=43|41=42|39=2|434=1|102=0|58=too late|", 3, "LMXBD"));
  CancelReject c;
  std::string err;
  ASSERT_TRUE(parse_cancel_reject(m, c, err)) << err;
  EXPECT_EQ(c.clordid, "43");
  EXPECT_EQ(c.orig_clordid, "42");
  EXPECT_EQ(c.ord_status, '2');
  EXPECT_EQ(c.response_to, '1');
  EXPECT_EQ(c.reason, 0);
  EXPECT_EQ(c.text, "too late");
}

TEST(LmaxExecParse, TradeCaptureReportAndAck)
{
  auto m = parse(fixmsg("AE",
                        "568=T1|912=Y|17=E000000000000009|527=M000000000000001|48=4001|22=8|32=0.4|"
                        "31=1.15229|75=20261118|60=20261118-10:00:00.123|552=1|54=1|37=1000000007|11=42|1=ACC|",
                        20, "LMXBD"));
  TradeCaptureReport r;
  std::string err;
  ASSERT_TRUE(parse_tcr(m, r, err)) << err;
  EXPECT_EQ(r.req_id, "T1");
  EXPECT_TRUE(r.last_rpt);
  EXPECT_EQ(r.last_qty_text, "0.4");
  EXPECT_EQ(r.last_px_text, "1.15229");
  EXPECT_EQ(r.trade_date, "20261118");
  ASSERT_EQ(r.sides.size(), 1u);
  EXPECT_EQ(r.sides[0].side, '1');
  EXPECT_EQ(r.sides[0].order_id, "1000000007");
  EXPECT_EQ(r.sides[0].clordid, "42");
  EXPECT_EQ(r.sides[0].account, "ACC");

  auto a = parse(fixmsg("AQ", "568=T1|569=1|263=1|748=0|749=0|750=0|", 21, "LMXBD"));
  TcrAck k;
  ASSERT_TRUE(parse_tcr_ack(a, k, err)) << err;
  EXPECT_EQ(k.total, 0);
  EXPECT_EQ(k.result, 0);
  EXPECT_EQ(k.status, 0);
  EXPECT_EQ(k.subscription, '1');
}

TEST(LmaxExecParse, Decimals)
{
  EXPECT_EQ(decimals_of(0.00001), 5);
  EXPECT_EQ(decimals_of(0.1), 1);
  EXPECT_EQ(decimals_of(0.001), 3);
  EXPECT_EQ(decimals_of(1), 0);
  EXPECT_EQ(decimal(1.152304999, 0.00001), "1.15230");
  EXPECT_EQ(decimal(2.5, 0.1), "2.5");
}
