/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Encode and decode of every OUCH 5.0 message against the specification's
// layouts. Each expected message is written out field by field in the
// spec's offset order (big-endian integers, Alpha left-justified and space
// padded), independently of the generated structs, and compared byte for
// byte (requests) or field by field (reports).

#include <gtest/gtest.h>

#include <string>

#include "nasdaq_ouch/OuchCodec.hpp"
#include "nasdaq_ouch/gen/ouch50.hpp"

using namespace nasdaq::ouch;
namespace to = nasdaq::ouch::to_nasdaq;
namespace from = nasdaq::ouch::from_nasdaq;

namespace
{
  // One message, field by field; at() checks that each field starts where
  // the specification's table says.
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
    Bytes &raw(std::string_view s)
    {
      b.append(s);
      return *this;
    }
  };

  template <class M>
  std::string encoded(M m, std::string_view tail = {})
  {
    char buf[512];
    size_t n = to::encode(buf, sizeof(buf), m, tail);
    return std::string(buf, n);
  }

  template <class M>
  M decoded_as(const std::string &bytes, size_t expect_size, std::string *tail = nullptr)
  {
    auto d = from::decode(bytes.data(), bytes.size());
    EXPECT_TRUE(d) << "status " << int(d.st);
    EXPECT_TRUE(std::holds_alternative<M>(d.msg)) << from::name(d.msg);
    EXPECT_EQ(d.size, expect_size);
    if (tail)
      tail->assign(d.tail ? d.tail : "", d.tail_len);
    return std::holds_alternative<M>(d.msg) ? std::get<M>(d.msg) : M{};
  }

  const std::string FIRM_OPT = std::string("\x05\x02", 2) + "M2TT"; // Firm option, TagValue
}

// ---- inbound: encoded by the OM -----------------------------------------------------------

TEST(OuchEncode, EnterOrder) // OUCH 5.0 2.1, pp.4-5
{
  to::EnterOrder m{};
  m.UserRefNum = 0x01020304;
  m.Side = 'T';
  m.Quantity = 999'999;
  m.set_Symbol("AAPL");
  m.Price = 1'999'999'900;
  m.TimeInForce = '0';
  m.Display = 'Y';
  m.Capacity = 'A';
  m.InterMarketSweepEligibility = 'N';
  m.CrossType = 'N';
  m.set_ClOrdID("ABC123");
  m.AppendageLength = uint16_t(FIRM_OPT.size());
  Bytes x;
  x.at(0).c('O').at(1).u(0x01020304, 4).at(5).c('T').at(6).u(999'999, 4).at(10).a("AAPL", 8).at(18)
      .u(1'999'999'900, 8).at(26).c('0').at(27).c('Y').at(28).c('A').at(29).c('N').at(30).c('N').at(31)
      .a("ABC123", 14).at(45).u(6, 2).at(47).raw(FIRM_OPT);
  EXPECT_EQ(sizeof(m), 47u);
  EXPECT_EQ(encoded(m, FIRM_OPT), x.b);
}

TEST(OuchEncode, ReplaceOrderRequest) // OUCH 5.0 2.2, pp.6-7
{
  to::ReplaceOrderRequest m{};
  m.OrigUserRefNum = 7;
  m.UserRefNum = 8;
  m.Quantity = 500;
  m.Price = 41'2500;
  m.TimeInForce = '3';
  m.Display = 'N';
  m.InterMarketSweepEligibility = 'N';
  m.set_ClOrdID("R1");
  m.AppendageLength = 0;
  Bytes x;
  x.at(0).c('U').at(1).u(7, 4).at(5).u(8, 4).at(9).u(500, 4).at(13).u(41'2500, 8).at(21).c('3').at(22).c('N')
      .at(23).c('N').at(24).a("R1", 14).at(38).u(0, 2).at(40);
  EXPECT_EQ(encoded(m), x.b);
}

TEST(OuchEncode, CancelOrderRequest) // OUCH 5.0 2.3, pp.7-8
{
  to::CancelOrderRequest m{};
  m.UserRefNum = 42;
  m.Quantity = 0;
  m.AppendageLength = 0;
  Bytes x;
  x.at(0).c('X').at(1).u(42, 4).at(5).u(0, 4).at(9).u(0, 2).at(11);
  EXPECT_EQ(encoded(m), x.b);
}

TEST(OuchEncode, ModifyOrderRequest) // OUCH 5.0 2.4, p.8
{
  to::ModifyOrderRequest m{};
  m.UserRefNum = 42;
  m.Side = 'E';
  m.Quantity = 50;
  m.AppendageLength = 0;
  Bytes x;
  x.at(0).c('M').at(1).u(42, 4).at(5).c('E').at(6).u(50, 4).at(10).u(0, 2).at(12);
  EXPECT_EQ(encoded(m), x.b);
}

TEST(OuchEncode, MassCancelRequest) // OUCH 5.0 2.5, p.9
{
  to::MassCancelRequest m{};
  m.UserRefNum = 9;
  m.set_Firm("M2TT");
  m.set_Symbol("");
  m.AppendageLength = 0;
  Bytes x;
  x.at(0).c('C').at(1).u(9, 4).at(5).a("M2TT", 4).at(9).a("", 8).at(17).u(0, 2).at(19);
  EXPECT_EQ(encoded(m), x.b);
}

TEST(OuchEncode, DisableAndEnableOrderEntryRequest) // OUCH 5.0 2.6 and 2.7, pp.9-10
{
  to::DisableOrderEntryRequest d{};
  d.UserRefNum = 10;
  d.set_Firm("M2TT");
  d.AppendageLength = 0;
  Bytes x;
  x.at(0).c('D').at(1).u(10, 4).at(5).a("M2TT", 4).at(9).u(0, 2).at(11);
  EXPECT_EQ(encoded(d), x.b);
  to::EnableOrderEntryRequest e{};
  e.UserRefNum = 11;
  e.set_Firm("M2TT");
  e.AppendageLength = 0;
  Bytes y;
  y.at(0).c('E').at(1).u(11, 4).at(5).a("M2TT", 4).at(9).u(0, 2).at(11);
  EXPECT_EQ(encoded(e), y.b);
}

TEST(OuchEncode, AccountQueryRequest) // OUCH 5.0 2.8, p.10
{
  to::AccountQueryRequest m{};
  m.AppendageLength = 0;
  EXPECT_EQ(encoded(m), std::string("Q\0\0", 3));
}

// ---- outbound: decoded by the OM ----------------------------------------------------------

TEST(OuchDecode, SystemEvent) // OUCH 5.0 3.1, p.11
{
  Bytes x;
  x.at(0).c('S').at(1).u(34'200'000'000'000ULL, 8).at(9).c('S');
  auto m = decoded_as<from::SystemEvent>(x.b, 10);
  EXPECT_EQ(uint64_t(m.Timestamp), 34'200'000'000'000ULL);
  EXPECT_EQ(m.EventCode, 'S');
}

TEST(OuchDecode, OrderAccepted) // OUCH 5.0 3.2, pp.11-12
{
  Bytes x;
  x.at(0).c('A').at(1).u(1, 8).at(9).u(77, 4).at(13).c('B').at(14).u(300, 4).at(18).a("MSFT", 8).at(26)
      .u(41'2500, 8).at(34).c('0').at(35).c('Y').at(36).u(0x0102030405060708ULL, 8).at(44).c('P').at(45).c('N')
      .at(46).c('N').at(47).c('L').at(48).a("CL1", 14).at(62).u(6, 2).at(64).raw(FIRM_OPT);
  std::string tail;
  auto m = decoded_as<from::OrderAccepted>(x.b, 70, &tail);
  EXPECT_EQ(uint32_t(m.UserRefNum), 77u);
  EXPECT_EQ(m.Side, 'B');
  EXPECT_EQ(uint32_t(m.Quantity), 300u);
  EXPECT_EQ(m.get_Symbol(), "MSFT");
  EXPECT_EQ(uint64_t(m.Price), 41'2500u);
  EXPECT_EQ(m.TimeInForce, '0');
  EXPECT_EQ(m.Display, 'Y');
  EXPECT_EQ(uint64_t(m.OrderReferenceNumber), 0x0102030405060708ULL);
  EXPECT_EQ(m.Capacity, 'P');
  EXPECT_EQ(m.OrderState, 'L');
  EXPECT_EQ(m.get_ClOrdID(), "CL1");
  EXPECT_EQ(sizeof(m), 62u); // Appendage Length (62) and the appendage are the tail
  EXPECT_EQ(tail, std::string("\x00\x06", 2) + FIRM_OPT);
  EXPECT_EQ(find_option(tail.substr(2), TAG_FIRM).value_or(""), "M2TT");
  // Without Appendage Length (62 bytes): decoded, with an empty tail.
  decoded_as<from::OrderAccepted>(x.b.substr(0, 62), 62, &tail);
  EXPECT_TRUE(tail.empty());
}

TEST(OuchDecode, OrderReplaced) // OUCH 5.0 3.3, pp.12-14
{
  Bytes x;
  x.at(0).c('U').at(1).u(1, 8).at(9).u(5, 4).at(13).u(6, 4).at(17).c('S').at(18).u(400, 4).at(22).a("MSFT", 8)
      .at(30).u(41'2400, 8).at(38).c('0').at(39).c('Z').at(40).u(99, 8).at(48).c('A').at(49).c('N').at(50).c('N')
      .at(51).c('D').at(52).a("CL2", 14).at(66).u(0, 2).at(68);
  auto m = decoded_as<from::OrderReplaced>(x.b, 68);
  EXPECT_EQ(uint32_t(m.OrigUserRefNum), 5u);
  EXPECT_EQ(uint32_t(m.UserRefNum), 6u);
  EXPECT_EQ(uint32_t(m.Quantity), 400u);
  EXPECT_EQ(m.Display, 'Z');
  EXPECT_EQ(uint64_t(m.OrderReferenceNumber), 99u);
  EXPECT_EQ(m.OrderState, 'D');
  EXPECT_EQ(m.get_ClOrdID(), "CL2");
}

TEST(OuchDecode, OrderCanceledWithAndWithoutAppendage) // OUCH 5.0 3.4, p.14
{
  Bytes x;
  x.at(0).c('C').at(1).u(1, 8).at(9).u(5, 4).at(13).u(60, 4).at(17).c('U').at(18);
  auto m = decoded_as<from::OrderCanceled>(x.b, 18);
  EXPECT_EQ(uint32_t(m.UserRefNum), 5u);
  EXPECT_EQ(uint32_t(m.Quantity), 60u);
  EXPECT_EQ(m.Reason, 'U');
  // With UserRefIdx: Appendage Length at 18, the option at 20.
  std::string idx("\x02\x1c\x07", 3);
  x.u(3, 2).at(20).raw(idx);
  std::string tail;
  decoded_as<from::OrderCanceled>(x.b, 23, &tail);
  ASSERT_EQ(tail.size(), 5u);
  EXPECT_EQ(find_option(tail.substr(2), TAG_USER_REF_IDX).value_or(""), std::string("\x07", 1));
}

TEST(OuchDecode, AIQCanceled) // OUCH 5.0 3.5, pp.14-15
{
  Bytes x;
  x.at(0).c('D').at(1).u(1, 8).at(9).u(5, 4).at(13).u(30, 4).at(17).c('Q').at(18).u(25, 4).at(22).u(41'2500, 8)
      .at(30).c('R').at(31).c('O').at(32);
  auto m = decoded_as<from::AIQCanceled>(x.b, 32);
  EXPECT_EQ(uint32_t(m.DecrementShares), 30u);
  EXPECT_EQ(m.Reason, 'Q');
  EXPECT_EQ(uint32_t(m.QuantityPreventedFromTrading), 25u);
  EXPECT_EQ(uint64_t(m.ExecutionPrice), 41'2500u);
  EXPECT_EQ(m.LiquidityFlag, 'R');
  EXPECT_EQ(m.AIQStrategy, 'O');
}

TEST(OuchDecode, OrderExecuted) // OUCH 5.0 3.6, p.15
{
  Bytes x;
  x.at(0).c('E').at(1).u(34'260'000'000'000ULL, 8).at(9).u(5, 4).at(13).u(40, 4).at(17).u(41'2600, 8).at(25)
      .c('A').at(26).u(0xAABBCCDDEEFF0011ULL, 8).at(34).u(0, 2).at(36);
  auto m = decoded_as<from::OrderExecuted>(x.b, 36);
  EXPECT_EQ(uint64_t(m.Timestamp), 34'260'000'000'000ULL);
  EXPECT_EQ(uint32_t(m.UserRefNum), 5u);
  EXPECT_EQ(uint32_t(m.Quantity), 40u);
  EXPECT_EQ(uint64_t(m.Price), 41'2600u);
  EXPECT_EQ(m.LiquidityFlag, 'A');
  EXPECT_EQ(uint64_t(m.MatchNumber), 0xAABBCCDDEEFF0011ULL);
}

TEST(OuchDecode, BrokenTrade) // OUCH 5.0 3.7, p.16
{
  Bytes x;
  x.at(0).c('B').at(1).u(1, 8).at(9).u(5, 4).at(13).u(123456, 8).at(21).c('E').at(22).a("CL3", 14).at(36);
  auto m = decoded_as<from::BrokenTrade>(x.b, 36);
  EXPECT_EQ(uint64_t(m.MatchNumber), 123456u);
  EXPECT_EQ(m.Reason, 'E');
  EXPECT_EQ(m.get_ClOrdID(), "CL3");
}

TEST(OuchDecode, Rejected) // OUCH 5.0 3.8, p.16
{
  Bytes x;
  x.at(0).c('J').at(1).u(1, 8).at(9).u(5, 4).at(13).u(0x0029, 2).at(15).a("CL4", 14).at(29);
  auto m = decoded_as<from::Rejected>(x.b, 29);
  EXPECT_EQ(uint32_t(m.UserRefNum), 5u);
  EXPECT_EQ(uint16_t(m.Reason), 0x0029u);
  EXPECT_STREQ(reject_reason_text(m.Reason), "Risk: Port Message Rate Restriction");
  EXPECT_EQ(m.get_ClOrdID(), "CL4");
}

TEST(OuchDecode, CancelPendingAndCancelReject) // OUCH 5.0 3.9 and 3.10, p.17
{
  Bytes p;
  p.at(0).c('P').at(1).u(1, 8).at(9).u(5, 4).at(13);
  EXPECT_EQ(uint32_t(decoded_as<from::CancelPending>(p.b, 13).UserRefNum), 5u);
  Bytes i;
  i.at(0).c('I').at(1).u(1, 8).at(9).u(6, 4).at(13);
  EXPECT_EQ(uint32_t(decoded_as<from::CancelReject>(i.b, 13).UserRefNum), 6u);
}

TEST(OuchDecode, OrderPriorityUpdate) // OUCH 5.0 3.11, pp.17-18
{
  Bytes x;
  x.at(0).c('T').at(1).u(1, 8).at(9).u(5, 4).at(13).u(41'2500, 8).at(21).c('Y').at(22).u(555, 8).at(30);
  auto m = decoded_as<from::OrderPriorityUpdate>(x.b, 30);
  EXPECT_EQ(uint64_t(m.Price), 41'2500u);
  EXPECT_EQ(m.Display, 'Y');
  EXPECT_EQ(uint64_t(m.OrderReferenceNumber), 555u);
}

TEST(OuchDecode, OrderModified) // OUCH 5.0 3.12, p.18
{
  Bytes x;
  x.at(0).c('M').at(1).u(1, 8).at(9).u(5, 4).at(13).c('T').at(14).u(80, 4).at(18);
  auto m = decoded_as<from::OrderModified>(x.b, 18);
  EXPECT_EQ(m.Side, 'T');
  EXPECT_EQ(uint32_t(m.Quantity), 80u);
}

TEST(OuchDecode, OrderRestated) // OUCH 5.0 3.13, pp.18-19
{
  std::string disp_qty("\x05\x16\x00\x00\x00\x64", 6); // Display Quantity (22) = 100
  Bytes x;
  x.at(0).c('R').at(1).u(1, 8).at(9).u(5, 4).at(13).c('R').at(14).u(6, 2).at(16).raw(disp_qty);
  std::string tail;
  auto m = decoded_as<from::OrderRestated>(x.b, 22, &tail);
  EXPECT_EQ(m.Reason, 'R');
  ASSERT_EQ(tail.size(), 8u); // Appendage Length (14, 2), then the option
  EXPECT_EQ(find_option(tail.substr(2), 22).value_or(""), std::string("\x00\x00\x00\x64", 4));
}

TEST(OuchDecode, MassCancelResponse) // OUCH 5.0 3.14, p.19
{
  Bytes x;
  x.at(0).c('X').at(1).u(1, 8).at(9).u(12, 4).at(13).a("M2TT", 4).at(17).a("MSFT", 8).at(25).u(0, 2).at(27);
  auto m = decoded_as<from::MassCancelResponse>(x.b, 27);
  EXPECT_EQ(uint32_t(m.UserRefNum), 12u);
  EXPECT_EQ(m.get_Firm(), "M2TT");
  EXPECT_EQ(m.get_Symbol(), "MSFT");
}

TEST(OuchDecode, DisableAndEnableOrderEntryResponse) // OUCH 5.0 3.15 and 3.16, pp.19-20
{
  Bytes g;
  g.at(0).c('G').at(1).u(1, 8).at(9).u(13, 4).at(13).a("M2TT", 4).at(17).u(0, 2).at(19);
  EXPECT_EQ(uint32_t(decoded_as<from::DisableOrderEntryResponse>(g.b, 19).UserRefNum), 13u);
  Bytes k;
  k.at(0).c('K').at(1).u(1, 8).at(9).u(14, 4).at(13).a("M2TT", 4).at(17).u(0, 2).at(19);
  EXPECT_EQ(decoded_as<from::EnableOrderEntryResponse>(k.b, 19).get_Firm(), "M2TT");
}

TEST(OuchDecode, AccountQueryResponse) // OUCH 5.0 3.17, p.20
{
  Bytes x;
  x.at(0).c('Q').at(1).u(1, 8).at(9).u(1001, 4).at(13);
  EXPECT_EQ(uint32_t(decoded_as<from::AccountQueryResponse>(x.b, 13).NextUserRefNum), 1001u);
}

TEST(OuchDecode, ShortAndUnknownMessages)
{
  auto d = from::decode("E\0\0", 3);
  EXPECT_EQ(d.st, nasdaq::status::incomplete); // Executed is 34 bytes before its Appendage Length
  d = from::decode("Y", 1);
  EXPECT_EQ(d.st, nasdaq::status::unknown);
}

// ---- helpers -------------------------------------------------------------------------------

TEST(OuchCodec, AppendageBuilderAndLookup)
{
  Appendage a;
  a.alpha(TAG_FIRM, "M2", 4).number(TAG_MIN_QTY, 100, 4).alpha(TAG_SHARES_LOCATED, "Y", 1);
  EXPECT_EQ(a.str(), std::string("\x05\x02M2  \x05\x03\x00\x00\x00\x64\x02\x19Y", 15));
  EXPECT_EQ(find_option(a.str(), TAG_FIRM).value_or(""), "M2  ");
  EXPECT_EQ(find_option(a.str(), TAG_SHARES_LOCATED).value_or(""), "Y");
  EXPECT_FALSE(find_option(a.str(), TAG_LOCATE_BROKER).has_value());
  EXPECT_FALSE(find_option(std::string("\x09\x02", 2), TAG_FIRM).has_value()); // length past the end
}

TEST(OuchCodec, ToBytesSetsTypeAndAppendageLength)
{
  from::OrderExecuted e{};
  e.UserRefNum = 5;
  auto b = to_bytes(e, FIRM_OPT);
  ASSERT_EQ(b.size(), 34u + 2u + 6u); // Appendage Length written before the options
  EXPECT_EQ(b[0], 'E');
  EXPECT_EQ(b.substr(34, 2), std::string("\x00\x06", 2));
  EXPECT_EQ(to_bytes(e).size(), 34u); // no options: neither the length nor the appendage
  from::Rejected r{};
  EXPECT_EQ(to_bytes(r).size(), 29u); // appendage absent
  to::CancelOrderRequest c{};
  auto cb = to_bytes(c, std::string("\x02\x1c\x07", 3)); // a request: the field is set
  ASSERT_EQ(cb.size(), 11u + 3u);
  EXPECT_EQ(cb.substr(9, 2), std::string("\x00\x03", 2));
}

TEST(OuchCodec, ReasonTexts)
{
  EXPECT_STREQ(reject_reason_text(0x0001), "Quote Unavailable");
  EXPECT_STREQ(reject_reason_text(0x0033), "Risk: IPO Market Buy Not Allowed");
  EXPECT_STREQ(reject_reason_text(0x0040), "Invalid AIQ");
  EXPECT_STREQ(reject_reason_text(0x0034), "unknown reject reason");
  EXPECT_STREQ(cancel_reason_text('U'), "user requested");
  EXPECT_STREQ(cancel_reason_text('?'), "unknown cancel reason");
}

TEST(OuchCodec, EasternMidnight)
{
  constexpr uint64_t S = 1'000'000'000ULL;
  // 2026-01-15 12:00 UTC (EST): midnight is 05:00 UTC.
  uint64_t jan = 1768478400ULL * S;
  EXPECT_EQ(eastern_midnight_epoch_ns(jan), (1768478400ULL - 7 * 3600) * S);
  // 2026-07-15 12:00 UTC (EDT): midnight is 04:00 UTC.
  uint64_t jul = 1784116800ULL * S;
  EXPECT_EQ(eastern_midnight_epoch_ns(jul), (1784116800ULL - 8 * 3600) * S);
  // 2026-03-08 (DST starts 07:00 UTC): at 12:00 UTC midnight was still EST.
  uint64_t mar = 1772971200ULL * S;
  EXPECT_EQ(eastern_midnight_epoch_ns(mar), (1772971200ULL - 7 * 3600) * S);
  // 2026-11-01 (DST ends 06:00 UTC): at 12:00 UTC midnight was EDT.
  uint64_t nov = 1793534400ULL * S;
  EXPECT_EQ(eastern_midnight_epoch_ns(nov), (1793534400ULL - 8 * 3600) * S);
  // 2026-01-15 03:00 UTC is still Jan 14 in New York.
  uint64_t early = (1768478400ULL - 9 * 3600) * S;
  EXPECT_EQ(eastern_midnight_epoch_ns(early), (1768478400ULL - 7 * 3600 - 86400) * S);
}
