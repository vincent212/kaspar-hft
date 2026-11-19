/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Builders and parsers against the examples in Clear Street's FIX API guide
// (docs.clearstreet.com/guides/fix-api, retrieved 2026-11-18), and every
// request validated with the dictionary build/ClearStreet-FIX42.xml.

#include <gtest/gtest.h>

#include <cstdio>

#include "quickfix/DataDictionary.h"
#include "quickfix/Message.h"

#include "clearstreet_om/CsFix.hpp"
#include "clearstreet_om/CsSession.hpp"
#include "clearstreet_om/SimClearStreet.hpp"

using namespace clearstreet::om;

namespace
{
  // A guide example ("|" for SOH, header without 9 and 10) as a wire message.
  std::string wire(std::string body_with_pipes)
  {
    for (auto &c : body_with_pipes)
      if (c == '|')
        c = '\x01';
    // body_with_pipes starts after 8=FIX.4.2|
    std::string body = body_with_pipes;
    std::string head = "8=FIX.4.2\x01" "9=" + std::to_string(body.size()) + "\x01";
    std::string m = head + body;
    unsigned sum = 0;
    for (unsigned char c : m)
      sum += c;
    char t[16];
    std::snprintf(t, sizeof t, "10=%03u\x01", sum % 256);
    return m + t;
  }

  const FIX::DataDictionary &dict()
  {
    static FIX::DataDictionary d(CS_DICT);
    return d;
  }

  // Header as the session would set it, then validated with the dictionary.
  void validate(FIX::Message m)
  {
    m.getHeader().setField(49, "CLIENT");
    m.getHeader().setField(56, "CLST");
    m.getHeader().setField(34, "2");
    m.getHeader().setField(52, "20260902-14:30:01.000");
    FIX::Message again(m.toString(), dict(), true);
    dict().validate(again);
  }

  std::string f(const FIX::Message &m, int tag) { return m.isSetField(tag) ? m.getField(tag) : std::string("<none>"); }
}

TEST(CsFix, NewOrderMatchesTheGuideLimitDaySell)
{
  // guide: 1=<account>|11=CLIENT-0002|21=1|55=AAPL|54=2|60=...|38=25.5|40=2|44=230.25|59=0
  NewOrderReq r;
  r.account = "ACC1";
  r.clordid = "CLIENT-0002";
  r.symbol = "AAPL";
  r.side = '2';
  r.qty = "25.5";
  r.ord_type = '2';
  r.px = "230.25";
  r.tif = '0';
  r.transact_time = "20260902-14:30:02.000";
  auto m = build_new_order(r);
  EXPECT_EQ(m.getHeader().getField(35), "D");
  EXPECT_EQ(f(m, 1), "ACC1");
  EXPECT_EQ(f(m, 11), "CLIENT-0002");
  EXPECT_EQ(f(m, 21), "1");
  EXPECT_EQ(f(m, 55), "AAPL");
  EXPECT_EQ(f(m, 54), "2");
  EXPECT_EQ(f(m, 60), "20260902-14:30:02.000");
  EXPECT_EQ(f(m, 38), "25.5");
  EXPECT_EQ(f(m, 40), "2");
  EXPECT_EQ(f(m, 44), "230.25");
  EXPECT_EQ(f(m, 59), "0");
  for (int absent : {77, 99, 336, 847, 100, 167, 15, 58})
    EXPECT_FALSE(m.isSetField(absent)) << absent;
  validate(m);
}

TEST(CsFix, TwapDmaExtendedAndStopLimitAsInTheGuide)
{
  NewOrderReq r;
  r.account = "ACC1";
  r.clordid = "CLIENT-0007";
  r.symbol = "AAPL";
  r.qty = "1000";
  r.ord_type = '1';
  r.target_strategy = 1002;
  auto m = build_new_order(r);
  EXPECT_EQ(f(m, 847), "1002");
  EXPECT_FALSE(m.isSetField(44));
  validate(m);

  r.clordid = "CLIENT-0008";
  r.ord_type = '2';
  r.px = "230.00";
  r.target_strategy = 1003;
  r.ex_destination = "XNAS";
  m = build_new_order(r);
  EXPECT_EQ(f(m, 847), "1003");
  EXPECT_EQ(f(m, 100), "XNAS");
  validate(m);

  NewOrderReq e;
  e.account = "ACC1";
  e.clordid = "CLIENT-0004";
  e.symbol = "AAPL";
  e.qty = "10";
  e.px = "225.00";
  e.extended_hours = true;
  m = build_new_order(e);
  EXPECT_EQ(f(m, 336), "6");
  validate(m);

  NewOrderReq s;
  s.account = "ACC1";
  s.clordid = "CLIENT-0003";
  s.symbol = "AAPL";
  s.side = '2';
  s.open_close = 'O';
  s.qty = "100";
  s.ord_type = '4';
  s.px = "215.00";
  s.stop_px = "216.00";
  s.tif = '1';
  m = build_new_order(s);
  EXPECT_EQ(f(m, 99), "216.00");
  EXPECT_EQ(f(m, 77), "O");
  validate(m);

  s.tif = '7'; // At the Close, a Clear Street extension
  s.ord_type = '2';
  s.stop_px.clear();
  validate(build_new_order(s));
}

TEST(CsFix, CancelAndReplaceMatchTheGuide)
{
  // guide: 1=<account>|41=CLIENT-0003|11=CLIENT-0005|55=AAPL|54=2|60=...|38=100
  CancelReq c;
  c.account = "ACC1";
  c.orig_clordid = "CLIENT-0003";
  c.clordid = "CLIENT-0005";
  c.symbol = "AAPL";
  c.side = '2';
  c.qty = "100";
  auto m = build_cancel(c);
  EXPECT_EQ(m.getHeader().getField(35), "F");
  EXPECT_EQ(f(m, 41), "CLIENT-0003");
  EXPECT_EQ(f(m, 11), "CLIENT-0005");
  EXPECT_EQ(f(m, 38), "100");
  for (int absent : {21, 40, 44, 59, 167})
    EXPECT_FALSE(m.isSetField(absent)) << absent;
  validate(m);

  // guide: 1=<account>|41=CLIENT-0002|11=CLIENT-0006|21=1|55=AAPL|54=2|60=...|38=50|40=2|44=229.50|59=0
  ReplaceReq g;
  g.account = "ACC1";
  g.orig_clordid = "CLIENT-0002";
  g.clordid = "CLIENT-0006";
  g.symbol = "AAPL";
  g.side = '2';
  g.qty = "50";
  g.px = "229.50";
  m = build_replace(g);
  EXPECT_EQ(m.getHeader().getField(35), "G");
  EXPECT_EQ(f(m, 41), "CLIENT-0002");
  EXPECT_EQ(f(m, 21), "1");
  EXPECT_EQ(f(m, 44), "229.50");
  validate(m);
}

TEST(CsFix, ParsesTheGuideExecutionReports)
{
  const char *ack =
      "35=8|49=CLST|56=CLIENT|34=2|52=20260902-14:30:02.100|37=01a01234-0000-7000-8000-000000000001|"
      "17=01a01234-0000-7000-8000-000000000002|20=0|150=0|39=0|11=CLIENT-0002|1=ACC1|55=AAPL|54=2|38=25.5|"
      "44=230.25|31=0|32=0|151=25.5|14=0|6=0|60=20260902-14:30:02.099|";
  const char *fill =
      "35=8|49=CLST|56=CLIENT|34=3|52=20260902-14:30:02.500|37=01a01234-0000-7000-8000-000000000001|"
      "17=01a01234-0000-7000-8000-000000000003|20=0|150=2|39=2|11=CLIENT-0002|1=ACC1|55=AAPL|54=2|38=25.5|"
      "44=230.25|31=230.25|32=25.5|151=0|14=25.5|6=230.25|60=20260902-14:30:02.480|";
  FIX::Message a(wire(ack), dict(), true), b(wire(fill), dict(), true);
  dict().validate(a);
  dict().validate(b);
  ExecReport r;
  std::string err;
  ASSERT_TRUE(parse_exec(a, r, err)) << err;
  EXPECT_EQ(r.exec_type, '0');
  EXPECT_EQ(r.ord_status, '0');
  EXPECT_EQ(r.order_id, "01a01234-0000-7000-8000-000000000001");
  EXPECT_EQ(r.clordid, "CLIENT-0002");
  EXPECT_DOUBLE_EQ(r.leaves, 25.5);
  EXPECT_EQ(r.seq, 2u);
  ASSERT_TRUE(parse_exec(b, r, err)) << err;
  EXPECT_EQ(r.exec_type, '2');
  EXPECT_DOUBLE_EQ(r.last_qty, 25.5);
  EXPECT_DOUBLE_EQ(r.last_px, 230.25);
  EXPECT_DOUBLE_EQ(r.cum, 25.5);
  EXPECT_EQ(utc_ns(r.transact_time) % 1'000'000'000ULL, 480'000'000ULL);

  // restated, pending new with EffectiveTime, and a resend with PossDup
  const char *pend =
      "35=8|49=CLST|56=CLIENT|34=9|43=Y|52=20260902-14:30:02.500|122=20260902-14:30:02.400|37=X|17=Y|20=0|"
      "150=A|39=A|11=C1|1=ACC1|55=AAPL|54=1|38=10|31=0|32=0|151=10|14=0|6=0|60=20260902-14:30:02.480|"
      "168=20260903-13:30:00.000|";
  FIX::Message p(wire(pend), dict(), true);
  dict().validate(p);
  ASSERT_TRUE(parse_exec(p, r, err)) << err;
  EXPECT_TRUE(r.poss_dup);
  EXPECT_EQ(r.effective_time, "20260903-13:30:00.000");

  // a report without CumQty is refused, not thrown
  FIX::Message bad(wire("35=8|49=CLST|56=CLIENT|34=4|52=20260902-14:30:02.500|37=X|17=Y|20=0|150=0|39=0|"
                        "11=C|55=AAPL|54=1|151=1|6=0|"),
                   dict(), false);
  EXPECT_FALSE(parse_exec(bad, r, err));
  EXPECT_NE(err.find("14"), std::string::npos);
}

TEST(CsFix, ParsesCancelRejectAndSessionReject)
{
  FIX::Message c(wire("35=9|49=CLST|56=CLIENT|34=5|52=20260902-14:30:02.500|37=NONE|11=C2|41=C1|39=8|1=ACC1|"
                      "60=20260902-14:30:02.500|434=1|102=1|58=Unknown order|"),
                 dict(), true);
  dict().validate(c);
  CancelReject x;
  std::string err;
  ASSERT_TRUE(parse_cancel_reject(c, x, err)) << err;
  EXPECT_EQ(x.response_to, '1');
  EXPECT_EQ(x.reason, 1);
  EXPECT_EQ(x.ord_status, '8');

  // the guide's Reject example
  FIX::Message j(wire("35=3|49=CLST|56=CLIENT|34=58|52=20260902-15:20:02.000|45=12|371=38|372=D|373=1|"
                      "58=Required tag missing: OrderQty (38)|"),
                 dict(), true);
  SessionReject s;
  ASSERT_TRUE(parse_session_reject(j, s, err)) << err;
  EXPECT_EQ(s.ref_seq, 12u);
  EXPECT_EQ(s.ref_msg_type, "D");
  EXPECT_EQ(s.ref_tag, 38);
  EXPECT_EQ(s.reason, 1);
}

TEST(CsFix, LogonWithCredentialsValidates)
{
  // guide: 98=0|108=30|553=<SenderCompID>|554=<api-key>; Clear Street's reply has Text (58)
  FIX::Message m(wire("35=A|49=CLST|56=CLIENT|34=1|52=20260902-14:30:00.000|98=0|108=30|553=CLIENT|"
                      "554=FaCm6AO4cDOGfcRGIaZXeQ.vRHTK7qCuXZdhQmXZ0WwcPtIuNILVXVnkfAuWH6AAQU|58=ACC1,ACC2|"),
                 dict(), true);
  dict().validate(m);
  EXPECT_EQ(redact(m.toString()).find("FaCm6AO4"), std::string::npos);
  EXPECT_NE(redact(m.toString()).find("554=***"), std::string::npos);
}

TEST(CsFix, Decimals)
{
  EXPECT_EQ(decimal(23025, 0.01), "230.25");
  EXPECT_EQ(decimal(21600, 0.01), "216.00");
  EXPECT_EQ(decimal(12345, 0.0001), "1.2345");
  EXPECT_EQ(decimal(7, 1.0), "7");
  EXPECT_EQ(decimal(100000000, 0.01), "1000000.00"); // never scientific notation
}

TEST(CsFix, GuideMatricesInTheSimulator)
{
  NewOrderReq ok;
  ok.account = "A";
  ok.clordid = "C";
  ok.symbol = "AAPL";
  ok.qty = "1";
  ok.px = "1.00";
  EXPECT_EQ(SimClearStreet::check(ok), "");
  auto r = ok;
  r.side = '5';
  EXPECT_NE(SimClearStreet::check(r), "");
  r = ok;
  r.ord_type = '1'; // market with a price
  EXPECT_NE(SimClearStreet::check(r), "");
  r.px.clear();
  r.tif = '1'; // market GTC
  EXPECT_NE(SimClearStreet::check(r), "");
  r = ok;
  r.ord_type = '3';
  r.px.clear();
  r.stop_px = "1.00";
  r.tif = '3'; // stop IOC
  EXPECT_NE(SimClearStreet::check(r), "");
  r = ok;
  r.extended_hours = true;
  r.tif = '3';
  EXPECT_NE(SimClearStreet::check(r), "");
  r = ok;
  r.target_strategy = 1003; // DMA without destination
  EXPECT_NE(SimClearStreet::check(r), "");
  r.ex_destination = "XNAS";
  EXPECT_EQ(SimClearStreet::check(r), "");
}

TEST(CsFix, RedactsTheApiKey)
{
  const std::string logon = "8=FIX.4.2\x01" "35=A\x01" "98=0\x01" "108=30\x01" "553=CLIENT\x01"
                            "554=FaCm6AO4cDOGfcRGIaZXeQ.vRHTK7qCuXZdhQmXZ0WwcPtIuNILVXVnkfAuWH6AAQU\x01" "10=000\x01";
  const std::string r = redact(logon);
  EXPECT_EQ(r.find("FaCm6AO4"), std::string::npos);
  EXPECT_NE(r.find("554=***\x01"), std::string::npos);
  EXPECT_NE(r.find("553=CLIENT\x01"), std::string::npos);
  // a tag ending in 554 (e.g. 1554=) is not touched
  EXPECT_EQ(redact("1554=x|"), "1554=x|");
}
