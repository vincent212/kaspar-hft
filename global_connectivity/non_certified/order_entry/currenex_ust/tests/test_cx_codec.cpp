/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Encode and decode of every Currenex UST OUCH 2.0 message against the
// specification's layouts. Each expected message is written out field by
// field in the spec's offset order (big-endian signed integers, Alpha
// left-justified and space padded), from offset 0 of the message header,
// independently of the generated structs, and framed with SOH and ETX by
// hand. Requests are compared byte for byte; reports are decoded and
// compared field by field. Values are the spec's examples where it gives
// them.

#include <gtest/gtest.h>

#include <string>

#include "currenex_ust/CxCodec.hpp"
#include "currenex_ust/act/CurrenexOM.hpp"

using namespace currenex::ouch;

namespace
{
  // One message, field by field from header offset 0; at() checks that each
  // field starts where the specification's table says.
  struct Bytes
  {
    std::string b;
    Bytes &at(size_t off)
    {
      EXPECT_EQ(b.size(), off) << "field at the wrong offset";
      return *this;
    }
    Bytes &u(uint64_t v, int n)
    {
      for (int i = n - 1; i >= 0; --i)
        b.push_back(char((v >> (8 * i)) & 0xff));
      return *this;
    }
    Bytes &c(char ch)
    {
      b.push_back(ch);
      return *this;
    }
    Bytes &a(std::string_view s, size_t n)
    {
      for (size_t i = 0; i < n; ++i)
        b.push_back(i < s.size() ? s[i] : ' ');
      return *this;
    }
    // The header (UST OUCH 9, p.17) and the type.
    Bytes &hdr(uint32_t seq, uint32_t ts, char type) { return at(0).u(seq, 4).at(4).u(ts, 4).at(8).c(type); }
    std::string framed() const { return std::string(1, '\x01') + b + std::string(1, '\x03'); }
  };

  // Spec examples (UST OUCH 9 and 10).
  constexpr uint32_t SEQ = 0xBC614E;      // 12345678
  constexpr uint32_t TS = 0x7BF7C5;       // 8124357 ms = 02:15:24.357
  constexpr int32_t SESSION = 0x51b;      // 1307
  constexpr int32_t CLID = 0x1bc4e9ef;    // 465889775
  constexpr int32_t CLID2 = 0x1bc4ed5d;   // 465890653
  constexpr int32_t CLID3 = 0x1bc4ed57;   // 465890647
  constexpr int64_t ORDERID = 0x26c8dfbb4; // 10411178932
  constexpr int64_t AMT = 0x3d0b27;       // 40,005.51
  constexpr int64_t MIN = 0x3d0900;       // 40,000.00
  constexpr int64_t RATE = 0x170CF9EB65;  // 99.001953125
  constexpr int64_t SHOW = 0x7fffffff;    // 21,474,836.47
  constexpr int32_t ROUTED = 0x531;       // 1329
  constexpr int64_t FILL = 0x17d7840;     // 250,000.00
  constexpr int64_t LEAVES = 0x2FAF080;   // 500,000.00
  constexpr int64_t SETTLE = 0x1390b416a00LL;
  constexpr int64_t TRADEDATE = 0x13900f4b200LL;
  constexpr int64_t TRANSACT = 0x1390086e6a0LL;
  constexpr int64_t MATURITY = 0x13c00483e00LL;
  constexpr int64_t ISSUE = 0x13c00483e00LL - 86'400'000LL * 365;
  constexpr int64_t COUPON = 0x1CBBF2F34ELL; // 123.412345678
  constexpr int64_t YIELD_A = 0xd71ae7d;     // 2.25554045
  constexpr int64_t YIELD_C = 0x13678f7c;    // 3.25554044
  constexpr int64_t TICK = 0x2FAF080;        // 0.05
  constexpr int64_t RATIO = 0xf4240;         // 1,000,000 (spec example)
  constexpr char EXEC[] = "A2012220064JB00";

  template <class M>
  std::string request(const M &m, uint32_t seq = SEQ, uint32_t ts = TS)
  {
    return to_frame(int32_t(seq), int32_t(ts), encoded(m));
  }

  template <class M>
  M report(const Bytes &b, size_t expect_size)
  {
    auto s = b.framed();
    auto f = deframe(s.data(), s.size());
    EXPECT_EQ(f.st, Frame::ok);
    EXPECT_EQ(f.consumed, s.size());
    EXPECT_EQ(uint32_t(f.seq), SEQ);
    EXPECT_EQ(uint32_t(f.ts), TS);
    EXPECT_EQ(f.len + HEADER_SIZE, expect_size); // the spec's total from offset 0
    EXPECT_TRUE(std::holds_alternative<M>(f.msg)) << from::name(f.msg);
    return std::holds_alternative<M>(f.msg) ? std::get<M>(f.msg) : M{};
  }

  // The spread trade legs (10.16.4 and 10.17.2 share them from AnchorLegInstrumentIndex on).
  Bytes &legs(Bytes &b, size_t base)
  {
    return b.at(base).u(0x0005, 2)
        .at(base + 2).a("9127955G2", 9)
        .at(base + 11).u(MATURITY, 8)
        .at(base + 19).u(ISSUE, 8)
        .at(base + 27).u(COUPON, 8)
        .at(base + 35).c('B')
        .at(base + 36).u(RATE, 8)
        .at(base + 44).u(FILL, 8)
        .at(base + 52).u(YIELD_A, 8)
        .at(base + 60).c('1')
        .at(base + 61).u(SETTLE, 8)
        .at(base + 69).a(EXEC, 20)
        .at(base + 89).u(0x0006, 2)
        .at(base + 91).a("91282CJY8", 9)
        .at(base + 100).u(MATURITY, 8)
        .at(base + 108).u(ISSUE, 8)
        .at(base + 116).u(COUPON, 8)
        .at(base + 124).c('S')
        .at(base + 125).u(RATE, 8)
        .at(base + 133).u(FILL, 8)
        .at(base + 141).u(YIELD_C, 8)
        .at(base + 149).c('1')
        .at(base + 150).u(SETTLE, 8)
        .at(base + 158).a("C2012220064JB00", 20);
  }

  template <class T>
  void check_legs(const T &m)
  {
    EXPECT_EQ(int16_t(m.AnchorLegInstrumentIndex), 5);
    EXPECT_EQ(m.get_AnchorLegSecurityID(), "9127955G2");
    EXPECT_EQ(int64_t(m.AnchorLegMaturityDate), MATURITY);
    EXPECT_EQ(int64_t(m.AnchorLegIssueDate), ISSUE);
    EXPECT_EQ(int64_t(m.AnchorLegCouponRate), COUPON);
    EXPECT_EQ(m.AnchorLegSide, 'B');
    EXPECT_EQ(int64_t(m.AnchorLegFillRate), RATE);
    EXPECT_EQ(int64_t(m.AnchorLegFillAmt), FILL);
    EXPECT_EQ(int64_t(m.AnchorLegYield), YIELD_A);
    EXPECT_EQ(m.AnchorLegPriceType, '1');
    EXPECT_EQ(int64_t(m.AnchorLegSettlementDate), SETTLE);
    EXPECT_EQ(m.get_AnchorLegExecutionID(), EXEC);
    EXPECT_EQ(int16_t(m.CalculatedLegInstrumentIndex), 6);
    EXPECT_EQ(m.get_CalculatedLegSecurityID(), "91282CJY8");
    EXPECT_EQ(int64_t(m.CalculatedLegMaturityDate), MATURITY);
    EXPECT_EQ(int64_t(m.CalculatedLegIssueDate), ISSUE);
    EXPECT_EQ(int64_t(m.CalculatedLegCouponRate), COUPON);
    EXPECT_EQ(m.CalculatedLegSide, 'S');
    EXPECT_EQ(int64_t(m.CalculatedLegFillRate), RATE);
    EXPECT_EQ(int64_t(m.CalculatedLegFillAmt), FILL);
    EXPECT_EQ(int64_t(m.CalculatedLegYield), YIELD_C);
    EXPECT_EQ(m.CalculatedLegPriceType, '1');
    EXPECT_EQ(int64_t(m.CalculatedLegSettlementDate), SETTLE);
    EXPECT_EQ(m.get_CalculatedLegExecutionID(), "C2012220064JB00");
  }
}

// ---- framing and header -------------------------------------------------------------------

TEST(CxFraming, HeaderAndDelimiters)
{
  to::Heartbeat h{};
  h.SessionID = SESSION;
  auto s = request(h);
  Bytes e;
  e.hdr(SEQ, TS, 'C').at(9).u(SESSION, 4);
  EXPECT_EQ(s, e.framed());
  EXPECT_EQ(s.size(), 13u + 2);
  EXPECT_EQ(s.front(), '\x01');
  EXPECT_EQ(s.back(), '\x03');
}

TEST(CxFraming, SeveralFramesInOneBlockAndPartialReads)
{
  Bytes h1, h2;
  h1.hdr(1, TS, 'C').at(9).u(SESSION, 4);
  h2.hdr(2, TS, 'C').at(9).u(SESSION, 4);
  auto block = h1.framed() + h2.framed();
  auto f1 = deframe(block.data(), block.size());
  ASSERT_EQ(f1.st, Frame::ok);
  EXPECT_EQ(f1.seq, 1);
  auto f2 = deframe(block.data() + f1.consumed, block.size() - f1.consumed);
  ASSERT_EQ(f2.st, Frame::ok);
  EXPECT_EQ(f2.seq, 2);
  EXPECT_EQ(f1.consumed + f2.consumed, block.size());
  for (size_t n = 0; n < h1.framed().size(); ++n)
    EXPECT_EQ(deframe(block.data(), n).st, Frame::incomplete) << n;
}

// Binary fields can hold 0x01 and 0x03: the frame is measured by its type,
// not by searching for ETX.
TEST(CxFraming, DelimiterBytesInsideBinaryFields)
{
  Bytes b;
  b.hdr(0x01030103, 0x03010301, 'C').at(9).u(0x03010301, 4);
  auto s = b.framed();
  auto f = deframe(s.data(), s.size());
  ASSERT_EQ(f.st, Frame::ok);
  EXPECT_EQ(uint32_t(f.seq), 0x01030103u);
  EXPECT_EQ(int32_t(std::get<from::Heartbeat>(f.msg).SessionID), 0x03010301);
}

TEST(CxFraming, GarbageUnknownTypeAndMissingEtx)
{
  std::string junk = "xyz";
  Bytes hb;
  hb.hdr(9, TS, 'C').at(9).u(SESSION, 4);
  auto s = junk + hb.framed();
  auto g = deframe(s.data(), s.size());
  EXPECT_EQ(g.st, Frame::garbage);
  EXPECT_EQ(g.consumed, 3u);

  // A type not in the table: skipped to the next frame, header still read.
  Bytes un;
  un.hdr(10, TS, 'Z').a("whatever", 8);
  auto s2 = un.framed() + hb.framed();
  auto u = deframe(s2.data(), s2.size());
  EXPECT_EQ(u.st, Frame::unknown);
  EXPECT_EQ(u.seq, 10);
  EXPECT_EQ(u.type, 'Z');
  EXPECT_EQ(u.consumed, un.framed().size());
  EXPECT_EQ(deframe(s2.data() + u.consumed, s2.size() - u.consumed).st, Frame::ok);

  // The type's length does not end at an ETX.
  auto bad = hb.framed();
  bad.back() = 'Q';
  EXPECT_EQ(deframe(bad.data(), bad.size()).st, Frame::garbage);
}

// ---- to Currenex --------------------------------------------------------------------------

TEST(CxEncode, Logon)
{
  to::Logon m{};
  m.set_UserID("AbcUser");
  m.set_Password("123pswd");
  m.SessionID = 0;
  m.Version = 200;
  Bytes e;
  e.hdr(1, TS, 'a').at(9).a("AbcUser", 20).at(29).a("123pswd", 20).at(49).u(0, 4).at(53).u(0x00C8, 2).at(55);
  EXPECT_EQ(request(m, 1), e.framed());
}

TEST(CxEncode, Logout)
{
  to::Logout m{};
  m.set_UserID("AbcUser");
  m.SessionID = SESSION;
  m.set_Reason("A6");
  Bytes e;
  e.hdr(SEQ, TS, 'B').at(9).a("AbcUser", 20).at(29).u(SESSION, 4).at(33).a("A6", 3).at(36);
  EXPECT_EQ(request(m), e.framed());
}

TEST(CxEncode, HeartbeatInstrumentInfoRequestRoutingUserInfoRequest)
{
  to::Heartbeat h{};
  h.SessionID = SESSION;
  to::InstrumentInfoRequest i{};
  i.SessionID = SESSION;
  to::RoutingUserInfoRequest r{};
  r.SessionID = SESSION;
  Bytes eh, ei, er;
  eh.hdr(SEQ, TS, 'C').at(9).u(SESSION, 4).at(13);
  ei.hdr(SEQ, TS, 'E').at(9).u(SESSION, 4).at(13);
  er.hdr(SEQ, TS, 'G').at(9).u(SESSION, 4).at(13);
  EXPECT_EQ(request(h), eh.framed());
  EXPECT_EQ(request(i), ei.framed());
  EXPECT_EQ(request(r), er.framed());
}

TEST(CxEncode, NewOrder)
{
  to::NewOrder m{};
  m.ClOrderID = CLID;
  m.OrderType = 'F';
  m.InstrumentIndex = 5;
  m.Side = 'B';
  m.OrderAmt = AMT;
  m.MinAmt = MIN;
  m.Price = RATE;
  m.ShowAmt = SHOW;
  m.ExpireType = 'G';
  m.RoutedUserKey = ROUTED;
  Bytes e;
  e.hdr(SEQ, TS, 'L')
      .at(9).u(CLID, 4)
      .at(13).c('F')
      .at(14).u(5, 2)
      .at(16).c('B')
      .at(17).u(AMT, 8)
      .at(25).u(MIN, 8)
      .at(33).u(RATE, 8)
      .at(41).u(SHOW, 8)
      .at(49).c('G')
      .at(50).u(ROUTED, 4)
      .at(54);
  EXPECT_EQ(request(m), e.framed());
}

// RoutedUserKey -1 (not routing) is a signed Integer: all bits set.
TEST(CxEncode, NewOrderNotRouted)
{
  to::NewOrder m{};
  m.RoutedUserKey = NOT_ROUTED;
  auto s = encoded(m);
  EXPECT_EQ(s.substr(42, 4), std::string("\xff\xff\xff\xff", 4));
}

TEST(CxEncode, OrderCancelRequest)
{
  to::OrderCancelRequest m{};
  m.NewClOrderID = CLID2;
  m.PrevClOrderID = CLID3;
  m.InstrumentIndex = 5;
  m.RoutedUserKey = ROUTED;
  Bytes e;
  e.hdr(SEQ, TS, 'N').at(9).u(CLID2, 4).at(13).u(CLID3, 4).at(17).u(5, 2).at(19).u(ROUTED, 4).at(23);
  EXPECT_EQ(request(m), e.framed());
}

TEST(CxEncode, OrderReplaceOrCancelAndEvenIfPartiallyFilled)
{
  to::OrderReplaceOrCancelRequest p{};
  p.NewClOrderID = 0x282c2db1;
  p.OrigClOrderID = 0x1d2c2db1;
  p.OrderAmt = AMT;
  p.Price = RATE;
  p.InstrumentIndex = 5;
  p.RoutedUserKey = ROUTED;
  to::OrderReplaceEvenIfPartiallyFilled s{};
  s.NewClOrderID = 0x282c2db1;
  s.OrigClOrderID = 0x1d2c2db1;
  s.OrderAmt = AMT;
  s.Price = RATE;
  s.InstrumentIndex = 5;
  s.RoutedUserKey = ROUTED;
  for (char type : {'P', 'S'})
  {
    Bytes e;
    e.hdr(SEQ, TS, type)
        .at(9).u(0x282c2db1, 4)
        .at(13).u(0x1d2c2db1, 4)
        .at(17).u(AMT, 8)
        .at(25).u(RATE, 8)
        .at(33).u(5, 2)
        .at(35).u(ROUTED, 4)
        .at(39);
    EXPECT_EQ(type == 'P' ? request(p) : request(s), e.framed()) << type;
  }
}

TEST(CxEncode, ResendRequest)
{
  to::ResendRequest m{};
  m.BeginSeqNo = 0x16842; // 92226
  Bytes e;
  e.hdr(SEQ, TS, '2').at(9).u(0x16842, 4).at(13);
  EXPECT_EQ(request(m), e.framed());
}

// ---- from Currenex ------------------------------------------------------------------------

TEST(CxDecode, LogonResponse)
{
  Bytes b;
  b.hdr(SEQ, TS, 'a').at(9).a("AbcUser", 20).at(29).a("", 20).at(49).u(SESSION, 4).at(53).u(200, 2);
  auto m = report<from::Logon>(b, 55);
  EXPECT_EQ(m.get_UserID(), "AbcUser");
  EXPECT_EQ(int32_t(m.SessionID), SESSION);
  EXPECT_EQ(int16_t(m.Version), 200);
}

TEST(CxDecode, Logout)
{
  Bytes b;
  b.hdr(SEQ, TS, 'B').at(9).a("AbcUser", 20).at(29).u(SESSION, 4).at(33).a("A10", 3);
  auto m = report<from::Logout>(b, 36);
  EXPECT_EQ(m.get_UserID(), "AbcUser");
  EXPECT_EQ(int32_t(m.SessionID), SESSION);
  EXPECT_EQ(m.get_Reason(), "A10");
  EXPECT_STREQ(logout_text(m.get_Reason()), "Invalid First Sequence Number");
}

TEST(CxDecode, Heartbeat)
{
  Bytes b;
  b.hdr(SEQ, TS, 'C').at(9).u(SESSION, 4);
  EXPECT_EQ(int32_t(report<from::Heartbeat>(b, 13).SessionID), SESSION);
}

TEST(CxDecode, OutrightInstrumentInfo)
{
  Bytes b;
  b.hdr(SEQ, TS, 'D')
      .at(9).u(SESSION, 4)
      .at(13).u(5, 2)
      .at(15).c('3')
      .at(16).a("OTR-2YR", 20)
      .at(36).u(0x139061b0e00LL, 8)
      .at(44).a("9127955G2", 9)
      .at(53).c('1')
      .at(54).u(MATURITY, 8)
      .at(62).u(ISSUE, 8)
      .at(70).a("OLD", 10)
      .at(80).a("2Y", 10);
  auto m = report<from::OutrightInstrumentInfo>(b, 90);
  EXPECT_EQ(int32_t(m.SessionID), SESSION);
  EXPECT_EQ(int16_t(m.InstrumentIndex), 5);
  EXPECT_EQ(m.InstrumentType, '3');
  EXPECT_EQ(m.get_InstrumentID(), "OTR-2YR");
  EXPECT_EQ(int64_t(m.SettlementDate), 0x139061b0e00LL); // 2012-08-08 12:00 UTC
  EXPECT_EQ(m.get_SecurityID(), "9127955G2");
  EXPECT_EQ(m.SecurityType, '1');
  EXPECT_EQ(int64_t(m.MaturityDate), MATURITY);
  EXPECT_EQ(int64_t(m.IssueDate), ISSUE);
  EXPECT_EQ(m.get_InstrumentState(), "OLD");
  EXPECT_EQ(m.get_InstrumentTenor(), "2Y");
}

TEST(CxDecode, SpreadInstrumentInfo)
{
  Bytes b;
  b.hdr(SEQ, TS, 'e')
      .at(9).u(SESSION, 4)
      .at(13).u(5, 2)
      .at(15).c('1')
      .at(16).a("OTR/OTR/A-2YR/3YR", 20)
      .at(36).a("OTR/OTR/A", 15)
      .at(51).a("2Y/3Y", 10)
      .at(61).u(TICK, 8)
      .at(69).u(0x0201, 2)
      .at(71).a("OTR-2Y", 10)
      .at(81).a("OTR", 10)
      .at(91).a("2Y", 5)
      .at(96).a("9127955G2", 9)
      .at(105).u(RATIO, 8)
      .at(113).c('B')
      .at(114).u(0x01f9, 2)
      .at(116).a("OTR-3Y", 10)
      .at(126).a("OTR", 10)
      .at(136).a("3Y", 5)
      .at(141).a("91282CJY8", 9)
      .at(150).u(RATIO, 8)
      .at(158).c('S');
  auto m = report<from::SpreadInstrumentInfo>(b, 159);
  EXPECT_EQ(int16_t(m.InstrumentIndex), 5);
  EXPECT_EQ(m.InstrumentType, '1');
  EXPECT_EQ(m.get_InstrumentID(), "OTR/OTR/A-2YR/3YR");
  EXPECT_EQ(m.get_InstrumentState(), "OTR/OTR/A");
  EXPECT_EQ(m.get_InstrumentTenor(), "2Y/3Y");
  EXPECT_EQ(int64_t(m.InstrumentTickSize), TICK);
  EXPECT_EQ(int16_t(m.AnchorLegInstrumentIndex), 0x0201);
  EXPECT_EQ(m.get_AnchorLegInstrumentID(), "OTR-2Y");
  EXPECT_EQ(m.get_AnchorLegInstrumentState(), "OTR");
  EXPECT_EQ(m.get_AnchorLegInstrumentTenor(), "2Y");
  EXPECT_EQ(m.get_AnchorLegSecurityID(), "9127955G2");
  EXPECT_EQ(int64_t(m.AnchorLegRatio), RATIO);
  EXPECT_EQ(m.AnchorLegSide, 'B');
  EXPECT_EQ(int16_t(m.CalculatedLegInstrumentIndex), 0x01f9);
  EXPECT_EQ(m.get_CalculatedLegInstrumentID(), "OTR-3Y");
  EXPECT_EQ(m.get_CalculatedLegInstrumentState(), "OTR");
  EXPECT_EQ(m.get_CalculatedLegInstrumentTenor(), "3Y");
  EXPECT_EQ(m.get_CalculatedLegSecurityID(), "91282CJY8");
  EXPECT_EQ(int64_t(m.CalculatedLegRatio), RATIO);
  EXPECT_EQ(m.CalculatedLegSide, 'S');
}

TEST(CxDecode, RoutingUserInfo)
{
  Bytes b;
  b.hdr(SEQ, TS, 'g').at(9).u(SESSION, 4).at(13).a("RoutedUser1", 20).at(33).u(ROUTED, 4);
  auto m = report<from::RoutingUserInfo>(b, 37);
  EXPECT_EQ(int32_t(m.SessionID), SESSION);
  EXPECT_EQ(m.get_RoutedUserId(), "RoutedUser1");
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, NewOrderAckConfirmedAndRejected)
{
  Bytes b;
  b.hdr(SEQ, TS, 'M').at(9).u(CLID, 4).at(13).u(ORDERID, 8).at(21).c('C').at(22).u(0, 2).at(24).u(ROUTED, 4);
  auto m = report<from::NewOrderAck>(b, 28);
  EXPECT_EQ(int32_t(m.ClOrderID), CLID);
  EXPECT_EQ(int64_t(m.OrderID), ORDERID);
  EXPECT_EQ(m.Status, 'C');
  EXPECT_EQ(int16_t(m.ErrorCode), 0);
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);

  Bytes r;
  r.hdr(SEQ, TS, 'M').at(9).u(CLID, 4).at(13).u(uint64_t(-1), 8).at(21).c('R').at(22).u(0x0b, 2).at(24).u(uint32_t(-1), 4);
  auto j = report<from::NewOrderAck>(r, 28);
  EXPECT_EQ(int64_t(j.OrderID), -1); // set to -1 on reject
  EXPECT_EQ(j.Status, 'R');
  EXPECT_EQ(int16_t(j.ErrorCode), 0x0b);
  EXPECT_STREQ(error_text(j.ErrorCode), "Invalid Credit");
  EXPECT_EQ(int32_t(j.RoutedUserKey), -1);
}

TEST(CxDecode, OrderCancelReject)
{
  Bytes b;
  b.hdr(SEQ, TS, 'O').at(9).u(CLID2, 4).at(13).u(CLID3, 4).at(17).u(1, 2).at(19).u(ROUTED, 4);
  auto m = report<from::OrderCancelReject>(b, 23);
  EXPECT_EQ(int32_t(m.NewClOrderID), CLID2);
  EXPECT_EQ(int32_t(m.PrevClOrderID), CLID3);
  EXPECT_EQ(int16_t(m.ErrorCode), 1);
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, OrderReplaceAck)
{
  Bytes b;
  b.hdr(SEQ, TS, 'Q').at(9).u(0x282c2db1, 4).at(13).u(0x1d2c2db1, 4).at(17).c('P').at(18).u(0, 2).at(20).u(ROUTED, 4);
  auto m = report<from::OrderReplaceAck>(b, 24);
  EXPECT_EQ(int32_t(m.NewClOrderID), 0x282c2db1);
  EXPECT_EQ(int32_t(m.PrevClOrderID), 0x1d2c2db1);
  EXPECT_EQ(m.Status, 'P');
  EXPECT_EQ(int16_t(m.ErrorCode), 0);
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, OrderCancelledOrExpired)
{
  Bytes b;
  b.hdr(SEQ, TS, 'R').at(9).u(CLID, 4).at(13).u(ORDERID, 8).at(21).c('C').at(22).u(2, 2).at(24).u(ROUTED, 4);
  auto m = report<from::OrderCancelledOrExpired>(b, 28);
  EXPECT_EQ(int32_t(m.ClOrderID), CLID);
  EXPECT_EQ(int64_t(m.OrderID), ORDERID);
  EXPECT_EQ(m.Status, 'C');
  EXPECT_EQ(int16_t(m.Type), 2); // below minimum cancel
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, PendingFillAndPendingFillCancel)
{
  for (char type : {'U', 'V'})
  {
    Bytes b;
    b.hdr(SEQ, TS, type)
        .at(9).u(CLID, 4)
        .at(13).u(ORDERID, 8)
        .at(21).u(0x15, 4)
        .at(25).u(FILL, 8)
        .at(33).u(RATE, 8)
        .at(41).c('2')
        .at(42).a("NA", 4)
        .at(46).u(ROUTED, 4);
    auto check = [&](const auto &m) {
      EXPECT_EQ(int32_t(m.ClOrderID), CLID);
      EXPECT_EQ(int64_t(m.OrderID), ORDERID);
      EXPECT_EQ(int32_t(m.TradeLinkID), 0x15);
      EXPECT_EQ(int64_t(m.FillAmt), FILL);
      EXPECT_EQ(int64_t(m.FillRate), RATE);
      EXPECT_EQ(m.AggressorFlag, '2');
      EXPECT_EQ(m.get_ExecBroker(), "NA");
      EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
    };
    if (type == 'U')
      check(report<from::PendingFill>(b, 50));
    else
      check(report<from::PendingFillCancel>(b, 50));
  }
}

TEST(CxDecode, OutrightPendingFillTrade)
{
  Bytes b;
  b.hdr(SEQ, TS, 't')
      .at(9).u(CLID, 4)
      .at(13).u(ORDERID, 8)
      .at(21).u(0x15, 4)
      .at(25).u(5, 2)
      .at(27).c('B')
      .at(28).u(FILL, 8)
      .at(36).u(RATE, 8)
      .at(44).a("NA", 4)
      .at(48).a(EXEC, 20)
      .at(68).c('1')
      .at(69).u(SETTLE, 8)
      .at(77).u(TRADEDATE, 8)
      .at(85).u(TRANSACT, 8)
      .at(93).u(LEAVES, 8)
      .at(101).a("9127955G2", 9)
      .at(110).u(MATURITY, 8)
      .at(118).u(ISSUE, 8)
      .at(126).u(COUPON, 8)
      .at(134).c('1')
      .at(135).c('2')
      .at(136).u(ROUTED, 4);
  auto m = report<from::PendingFillTrade>(b, 140);
  EXPECT_EQ(int32_t(m.ClOrderID), CLID);
  EXPECT_EQ(int64_t(m.OrderID), ORDERID);
  EXPECT_EQ(int32_t(m.TradeLinkID), 0x15);
  EXPECT_EQ(int16_t(m.InstrumentIndex), 5);
  EXPECT_EQ(m.Side, 'B');
  EXPECT_EQ(int64_t(m.FillAmt), FILL);
  EXPECT_EQ(int64_t(m.FillRate), RATE);
  EXPECT_EQ(m.get_ExecBroker(), "NA");
  EXPECT_EQ(m.get_ExecutionID(), EXEC);
  EXPECT_EQ(m.ExecType, '1');
  EXPECT_EQ(int64_t(m.SettleDate), SETTLE);
  EXPECT_EQ(int64_t(m.TradeDate), TRADEDATE);
  EXPECT_EQ(int64_t(m.TransactTime), TRANSACT);
  EXPECT_EQ(int64_t(m.LeavesAmt), LEAVES);
  EXPECT_EQ(m.get_SecurityID(), "9127955G2");
  EXPECT_EQ(int64_t(m.MaturityDate), MATURITY);
  EXPECT_EQ(int64_t(m.IssueDate), ISSUE);
  EXPECT_EQ(int64_t(m.CouponRate), COUPON);
  EXPECT_EQ(m.PriceType, '1');
  EXPECT_EQ(m.AggressorFlag, '2');
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, SpreadPendingFillTrade)
{
  Bytes b;
  b.hdr(SEQ, TS, 'f')
      .at(9).u(CLID, 4)
      .at(13).u(ORDERID, 8)
      .at(21).u(0x15, 4)
      .at(25).u(5, 2)
      .at(27).c('B')
      .at(28).u(FILL, 8)
      .at(36).u(TICK, 8)
      .at(44).a("NA", 4)
      .at(48).a(EXEC, 20)
      .at(68).c('1')
      .at(69).u(TRADEDATE, 8)
      .at(77).u(TRANSACT, 8)
      .at(85).u(LEAVES, 8)
      .at(93).c('4')
      .at(94).c('2');
  legs(b, 95).at(273).u(ROUTED, 4);
  auto m = report<from::PendingFillSpreadTrade>(b, 277);
  EXPECT_EQ(int32_t(m.ClOrderID), CLID);
  EXPECT_EQ(int64_t(m.OrderID), ORDERID);
  EXPECT_EQ(int32_t(m.TradeLinkID), 0x15);
  EXPECT_EQ(int16_t(m.InstrumentIndex), 5);
  EXPECT_EQ(m.Side, 'B');
  EXPECT_EQ(int64_t(m.FillAmt), FILL);
  EXPECT_EQ(int64_t(m.FillRate), TICK);
  EXPECT_EQ(m.get_ExecutionID(), EXEC);
  EXPECT_EQ(m.ExecType, '1');
  EXPECT_EQ(int64_t(m.TradeDate), TRADEDATE);
  EXPECT_EQ(int64_t(m.TransactTime), TRANSACT);
  EXPECT_EQ(int64_t(m.LeavesAmt), LEAVES);
  EXPECT_EQ(m.PriceType, '4');
  EXPECT_EQ(m.AggressorFlag, '2');
  check_legs(m);
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, OutrightTrade)
{
  Bytes b;
  b.hdr(SEQ, TS, 'T')
      .at(9).u(CLID, 4)
      .at(13).u(ORDERID, 8)
      .at(21).u(5, 2)
      .at(23).c('B')
      .at(24).u(FILL, 8)
      .at(32).u(RATE, 8)
      .at(40).a("NA", 4)
      .at(44).a(EXEC, 20)
      .at(64).c('1')
      .at(65).u(SETTLE, 8)
      .at(73).u(TRADEDATE, 8)
      .at(81).u(TRANSACT, 8)
      .at(89).u(LEAVES, 8)
      .at(97).a("9127955G2", 9)
      .at(106).u(MATURITY, 8)
      .at(114).u(ISSUE, 8)
      .at(122).u(COUPON, 8)
      .at(130).c('1')
      .at(131).c('2')
      .at(132).u(ROUTED, 4);
  auto m = report<from::Trade>(b, 136);
  EXPECT_EQ(int32_t(m.ClOrderID), CLID);
  EXPECT_EQ(int64_t(m.OrderID), ORDERID);
  EXPECT_EQ(int16_t(m.InstrumentIndex), 5);
  EXPECT_EQ(m.Side, 'B');
  EXPECT_EQ(int64_t(m.FillAmt), FILL);
  EXPECT_EQ(int64_t(m.FillRate), RATE);
  EXPECT_EQ(m.get_ExecBroker(), "NA");
  EXPECT_EQ(m.get_ExecutionID(), EXEC);
  EXPECT_EQ(m.ExecType, '1');
  EXPECT_EQ(int64_t(m.SettleDate), SETTLE);
  EXPECT_EQ(int64_t(m.TradeDate), TRADEDATE);
  EXPECT_EQ(int64_t(m.TransactTime), TRANSACT);
  EXPECT_EQ(int64_t(m.LeavesAmt), LEAVES);
  EXPECT_EQ(m.get_SecurityID(), "9127955G2");
  EXPECT_EQ(int64_t(m.MaturityDate), MATURITY);
  EXPECT_EQ(int64_t(m.IssueDate), ISSUE);
  EXPECT_EQ(int64_t(m.CouponRate), COUPON);
  EXPECT_EQ(m.PriceType, '1');
  EXPECT_EQ(m.AggressorFlag, '2');
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, SpreadTrade)
{
  Bytes b;
  b.hdr(SEQ, TS, 'F')
      .at(9).u(CLID, 4)
      .at(13).u(ORDERID, 8)
      .at(21).u(5, 2)
      .at(23).c('S')
      .at(24).u(FILL, 8)
      .at(32).u(TICK, 8)
      .at(40).a("NA", 4)
      .at(44).a(EXEC, 20)
      .at(64).c('1')
      .at(65).u(TRADEDATE, 8)
      .at(73).u(TRANSACT, 8)
      .at(81).u(LEAVES, 8)
      .at(89).c('4')
      .at(90).c('1');
  legs(b, 91).at(269).u(ROUTED, 4);
  auto m = report<from::SpreadTrade>(b, 273);
  EXPECT_EQ(int32_t(m.ClOrderID), CLID);
  EXPECT_EQ(int64_t(m.OrderID), ORDERID);
  EXPECT_EQ(int16_t(m.InstrumentIndex), 5);
  EXPECT_EQ(m.Side, 'S');
  EXPECT_EQ(int64_t(m.FillAmt), FILL);
  EXPECT_EQ(int64_t(m.FillRate), TICK);
  EXPECT_EQ(m.get_ExecutionID(), EXEC);
  EXPECT_EQ(m.ExecType, '1');
  EXPECT_EQ(int64_t(m.TradeDate), TRADEDATE);
  EXPECT_EQ(int64_t(m.TransactTime), TRANSACT);
  EXPECT_EQ(int64_t(m.LeavesAmt), LEAVES);
  EXPECT_EQ(m.PriceType, '4');
  EXPECT_EQ(m.AggressorFlag, '1');
  check_legs(m);
  EXPECT_EQ(int32_t(m.RoutedUserKey), ROUTED);
}

TEST(CxDecode, GapFill)
{
  Bytes b;
  b.hdr(SEQ, TS, '4').at(9).u(0x075BCD15, 4).at(13).c('2');
  auto m = report<from::GapFill>(b, 14);
  EXPECT_EQ(int32_t(m.NewSeqNo), 123456789);
  EXPECT_EQ(m.ReasonCode, '2');
}

// ---- units and identifiers ----------------------------------------------------------------

TEST(CxUnits, RatesAndAmounts)
{
  // 99-00+ (99 and 1/256) quoted in 1/256ths: 25345 ticks -> 99.00390625.
  EXPECT_EQ(CurrenexOM::to_rate(25345, 1.0 / 256), 99'003'906'250LL);
  // The spec's example rate, 99.001953125 = 99 + 1/512.
  EXPECT_EQ(CurrenexOM::to_rate(50689, 1.0 / 512), RATE);
  EXPECT_EQ(CurrenexOM::to_rate(0, 1.0 / 256), 0);
  // 1,000,000.00 is 0x5F5E100 (UST OUCH 4.2.2, p.6).
  EXPECT_EQ(1'000'000 * AMOUNT_SCALE, 0x5F5E100);
}

TEST(CxIds, WeekStartIsSaturdayMidnightUtc)
{
  // 2026-11-18 is a Wednesday; the week started Saturday 2026-11-14 00:00 UTC.
  int64_t wed = 1'795'000'000'000LL; // 2026-11-18 11:06:40 UTC
  int64_t sat = ClOrderIdGen::week_start_ms(wed);
  EXPECT_EQ(sat % MS_PER_DAY, 0);
  EXPECT_EQ(sat, 1'794'614'400'000LL);
  EXPECT_EQ(ClOrderIdGen::week_start_ms(sat), sat);
  EXPECT_EQ(ClOrderIdGen::week_start_ms(sat - 1), sat - 7LL * MS_PER_DAY);
  // A whole week of milliseconds fits a positive Integer.
  EXPECT_LT(7LL * MS_PER_DAY, int64_t(INT32_MAX));
}

TEST(CxIds, StrictlyIncreasingFromTheClock)
{
  int64_t now = 1'794'614'400'000LL + 1000;
  ClOrderIdGen g([&] { return now; });
  EXPECT_EQ(g.next(), 1000);
  EXPECT_EQ(g.next(), 1001); // faster than the clock: still increasing
  now += 5000;
  EXPECT_EQ(g.next(), 6000);
  // A restart later in the week starts above what the first issued.
  ClOrderIdGen g2([&] { return now + 1; });
  EXPECT_GT(g2.next(), 6000);
  // A new week starts again from its own milliseconds.
  now += 7LL * MS_PER_DAY;
  EXPECT_EQ(g.next(), 6000);
}
