/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Encode and decode every message of the protocol against the layouts in the
// specifications: the SoupBinTCP 3.00 packets in both directions (written out
// byte by byte from the tables in section 2) and the DROP 2.20 trade message
// line (section 1.6). Then the mapping of DROP lines to TradeCapture.

#include <gtest/gtest.h>

#include <string>
#include <variant>

#include "nasdaq_post_trade/DropLine.hpp"
#include "nasdaq_post_trade/SoupSession.hpp"
#include "nasdaq_post_trade/drop_msgs.hpp"

using namespace nasdaq_post_trade;
namespace soup_in = nasdaq::soup::from_nasdaq;
namespace soup_out = nasdaq::soup::to_nasdaq;

namespace
{
  std::string bytes(std::initializer_list<int> b)
  {
    std::string s;
    for (int c : b) s.push_back(char(c));
    return s;
  }

  // The line of DROP 2.20, 1.6 built from the table's sample values, field by
  // field at the table's offsets.
  std::string sample_line()
  {
    std::string s;
    s += "  34293.1";        //   0  Time Stamp (9), numeric, right justified
    s += ",E";               //   9, 10 Type
    s += ",ABCD01";          //  11, 12 Source (6)
    s += ",USR1,TKN0000001"; //  18, 19 Order Token (15) = user(4)+comma(1)+token(10)
    s += ",          ";      //  34, 35 Replaced Token (10), blank
    s += ",B";               //  45, 46 Buy/Sell
    s += ", 10000";          //  47, 48 Shares (6)
    s += ",INTC    ";        //  54, 55 Stock (8), alpha, left justified
    s += ",    12.8750";     //  63, 64 Price (11): 6 whole places, point, 4 decimals
    s += ",BIGJ";            //  75, 76 Firm (4)
    s += ",      836455";    //  80, 81 Reference (12)
    s += ",      122853";    //  93, 94 Match/TIF (12)
    s += ",A";               // 106, 107 Capacity
    s += ",R";               // 108, 109 Liquidity Code
    s += ",Q";               // 110, 111 Clearing Code
    s += ",    12.8751";     // 112, 113 Reference Price (11)
    s += ",I";               // 124, 125 Reference Price Type
    return s;
  }
}

// ---------------------------------------------------- SoupBinTCP, server side

TEST(SoupCodec, Debug)
{
  const std::string p = bytes({0, 4, '+'}) + "abc";
  auto d = soup_in::decode(p.data(), p.size());
  ASSERT_TRUE(d);
  EXPECT_TRUE(std::holds_alternative<soup_in::Debug>(d.msg));
  EXPECT_EQ(std::string(d.tail, d.tail_len), "abc");
  EXPECT_EQ(d.size, p.size());
}

TEST(SoupCodec, LoginAccepted)
{
  // 2.2.1: length, 'A' at 2, Session at 3 (10, left padded), Sequence Number
  // at 13 (20, left padded).
  const std::string p = bytes({0, 31}) + "A" + "     S0001" + std::string(19, ' ') + "5";
  ASSERT_EQ(p.size(), 33u);
  auto d = soup_in::decode(p.data(), p.size());
  ASSERT_TRUE(d);
  const auto &a = std::get<soup_in::LoginAccepted>(d.msg);
  EXPECT_EQ(a.get_Session(), "S0001");
  EXPECT_EQ(a.get_SequenceNumber(), "5");
  EXPECT_EQ(sizeof(soup_in::LoginAccepted), 33u);
}

TEST(SoupCodec, LoginRejected)
{
  for (char code : {'A', 'S'})
  {
    const std::string p = bytes({0, 2, 'J', code});
    auto d = soup_in::decode(p.data(), p.size());
    ASSERT_TRUE(d);
    EXPECT_EQ(std::get<soup_in::LoginRejected>(d.msg).RejectReasonCode, code);
  }
}

TEST(SoupCodec, SequencedDataCarriesOneDropLine)
{
  const std::string line = sample_line();
  std::string p = bytes({0, int(line.size() + 1)}) + "S" + line;
  auto d = soup_in::decode(p.data(), p.size());
  ASSERT_TRUE(d);
  EXPECT_TRUE(std::holds_alternative<soup_in::SequencedData>(d.msg));
  EXPECT_EQ(std::string(d.tail, d.tail_len), line);
}

TEST(SoupCodec, HeartbeatAndEndOfSession)
{
  const std::string h = bytes({0, 1, 'H'});
  const std::string z = bytes({0, 1, 'Z'});
  auto dh = soup_in::decode(h.data(), h.size());
  auto dz = soup_in::decode(z.data(), z.size());
  ASSERT_TRUE(dh);
  ASSERT_TRUE(dz);
  EXPECT_TRUE(std::holds_alternative<soup_in::ServerHeartbeat>(dh.msg));
  EXPECT_TRUE(std::holds_alternative<soup_in::EndOfSession>(dz.msg));
}

TEST(SoupCodec, PartialUnknownAndBackToBack)
{
  const std::string two = bytes({0, 1, 'H', 0, 2, 'J', 'A'});
  auto a = soup_in::decode(two.data(), two.size());
  ASSERT_TRUE(a);
  auto b = soup_in::decode(two.data() + a.size, two.size() - a.size);
  ASSERT_TRUE(b);
  EXPECT_EQ(std::get<soup_in::LoginRejected>(b.msg).RejectReasonCode, 'A');
  EXPECT_EQ(soup_in::decode(two.data(), 2).st, nasdaq::status::incomplete);
  EXPECT_EQ(soup_in::decode(two.data() + 3, 3).st, nasdaq::status::incomplete);
  const std::string unk = bytes({0, 1, '?'});
  auto u = soup_in::decode(unk.data(), unk.size());
  EXPECT_EQ(u.st, nasdaq::status::unknown);
  EXPECT_EQ(u.size, 3u);
}

// ---------------------------------------------------- SoupBinTCP, client side

TEST(SoupCodec, LoginRequest)
{
  // 2.3.1: 'L' at 2, Username at 3 (6), Password at 9 (10), Requested Session
  // at 19 (10), Requested Sequence Number at 29 (20); 49 bytes in all.
  LoginParams p{"user1", "secret", "", 1};
  const std::string s = SoupSession::encode_login(p);
  ASSERT_EQ(s.size(), 49u);
  EXPECT_EQ(s.substr(0, 3), bytes({0, 47, 'L'}));
  EXPECT_EQ(s.substr(3, 6), "user1 ") << "padded on the right";
  EXPECT_EQ(s.substr(9, 10), "secret    ");
  EXPECT_EQ(s.substr(19, 10), std::string(10, ' ')) << "blank: the active session";
  EXPECT_EQ(s.substr(29, 20), std::string(19, ' ') + "1");

  p.session = "S0001";
  p.next_seq = 12345;
  const std::string r = SoupSession::encode_login(p);
  EXPECT_EQ(r.substr(19, 10), "     S0001");
  EXPECT_EQ(r.substr(29, 20), std::string(15, ' ') + "12345");
}

TEST(SoupCodec, ClientHeartbeatLogoutUnsequenced)
{
  EXPECT_EQ(SoupSession::encode_heartbeat(), bytes({0, 1, 'R'}));
  EXPECT_EQ(SoupSession::encode_logout(), bytes({0, 1, 'O'}));
  char buf[16];
  const size_t n = soup_out::encode(buf, sizeof buf, soup_out::UnsequencedData{}, "xy");
  EXPECT_EQ(std::string(buf, n), bytes({0, 3, 'U', 'x', 'y'}));
}

// ------------------------------------------------------------------ DROP line

TEST(DropCodec, LineLayout)
{
  EXPECT_EQ(LINE_SIZE, 126u);
  EXPECT_EQ(sample_line().size(), LINE_SIZE);
  EXPECT_EQ(offsetof(Line, Type), 10u);
  EXPECT_EQ(offsetof(Line, Stock), 55u);
  EXPECT_EQ(offsetof(Line, MatchTIF), 94u);
  EXPECT_EQ(offsetof(Line, ReferencePriceType), 125u);
}

TEST(DropCodec, ParseSampleLine)
{
  DropReport r;
  std::string err;
  ASSERT_TRUE(parse_line(sample_line(), r, &err)) << err;
  EXPECT_EQ(r.time_ns, 34293'100'000'000LL);
  EXPECT_EQ(r.type, 'E');
  EXPECT_EQ(r.source, "ABCD01");
  EXPECT_EQ(r.order_token, "USR1,TKN0000001");
  EXPECT_EQ(r.replaced_token, "");
  EXPECT_EQ(r.side, 'B');
  EXPECT_EQ(r.shares, 10000);
  EXPECT_EQ(r.stock, "INTC");
  EXPECT_EQ(r.price_e4, 128750);
  EXPECT_EQ(r.firm, "BIGJ");
  EXPECT_EQ(r.reference, 836455u);
  EXPECT_EQ(r.match_tif, 122853u);
  EXPECT_EQ(r.capacity, 'A');
  EXPECT_EQ(r.liquidity, 'R');
  EXPECT_EQ(r.clearing, 'Q');
  EXPECT_EQ(r.ref_price_e4, 128751);
  EXPECT_EQ(r.ref_price_type, 'I');
}

TEST(DropCodec, FormatRoundTrip)
{
  DropReport r;
  ASSERT_TRUE(parse_line(sample_line(), r));
  // The formatter writes the time to the millisecond: 34293.100.
  const std::string f = format_line(r);
  ASSERT_EQ(f.size(), LINE_SIZE);
  EXPECT_EQ(f.substr(9), sample_line().substr(9));
  EXPECT_EQ(f.substr(0, 9), "34293.100");
  DropReport back;
  ASSERT_TRUE(parse_line(f, back));
  EXPECT_EQ(back.time_ns, r.time_ns);
  EXPECT_EQ(back.price_e4, r.price_e4);
  EXPECT_EQ(back.order_token, r.order_token);
}

TEST(DropCodec, LineTerminators)
{
  DropReport r;
  EXPECT_TRUE(parse_line(sample_line() + "\r\n", r));
  EXPECT_TRUE(parse_line(sample_line() + "\n", r));
  EXPECT_EQ(format_line(r, true).substr(LINE_SIZE), "\r\n");
}

TEST(DropCodec, RejectsMalformedLines)
{
  DropReport r;
  std::string err;
  EXPECT_FALSE(parse_line(sample_line().substr(0, 125), r, &err));
  EXPECT_NE(err.find("125 bytes"), std::string::npos);

  std::string bad = sample_line();
  bad[63] = ' ';
  EXPECT_FALSE(parse_line(bad, r, &err));
  EXPECT_NE(err.find("offset 63"), std::string::npos);

  bad = sample_line();
  bad[50] = 'x'; // inside Shares
  EXPECT_FALSE(parse_line(bad, r, &err));
  EXPECT_NE(err.find("Shares"), std::string::npos);

  bad = sample_line();
  bad.replace(64, 11, "   12.87501"); // five decimals
  EXPECT_FALSE(parse_line(bad, r, &err));
}

TEST(DropCodec, Conversions)
{
  int64_t v = 0;
  EXPECT_TRUE(parse_price("    12.8750", v));
  EXPECT_EQ(v, 128750);
  EXPECT_TRUE(parse_price("12.875", v)) << "the table's sample is not zero padded";
  EXPECT_EQ(v, 128750);
  EXPECT_TRUE(parse_price("0", v));
  EXPECT_EQ(v, 0);
  EXPECT_TRUE(parse_price("999999.9999", v));
  EXPECT_EQ(v, 9999999999);
  EXPECT_FALSE(parse_price("", v));
  EXPECT_FALSE(parse_price("1 2.5", v));
  EXPECT_EQ(format_price(128750), "12.8750");
  EXPECT_EQ(format_price(5), "0.0005");

  EXPECT_TRUE(parse_time("34293.1", v));
  EXPECT_EQ(v, 34293'100'000'000LL);
  EXPECT_TRUE(parse_time("  57600", v));
  EXPECT_EQ(v, 57600'000'000'000LL);
  EXPECT_FALSE(parse_time("86400.000", v));
  EXPECT_EQ(format_time(34293'125'000'000LL), "34293.125");

  uint64_t u = 1;
  EXPECT_TRUE(parse_uint("      ", u));
  EXPECT_EQ(u, 0u);
  EXPECT_FALSE(parse_uint("12a", u));
}

// ------------------------------------------------------------------ mapping

namespace
{
  DropReport sample(char type)
  {
    DropReport r;
    EXPECT_TRUE(parse_line(sample_line(), r));
    r.type = type;
    return r;
  }
}

TEST(DropMapping, ExecutionIsANewTrade)
{
  std::map<std::string, int> assets{{"INTC", 101}};
  MapContext c{"S0001", 42, 20261118, false, &assets};
  auto t = to_trade_capture(sample('E'), sample_line(), c);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->venue, en::x::NASDAQ);
  EXPECT_EQ(t->event, post_trade::Event::New);
  EXPECT_EQ(t->trade_id, "122853.836455") << "match number . order reference";
  EXPECT_EQ(t->ref_trade_id, "");
  EXPECT_EQ(t->report_id, "S0001:42");
  EXPECT_EQ(t->report_seq, 42u);
  EXPECT_EQ(t->venue_order_id, "836455");
  EXPECT_EQ(t->symbol, "INTC");
  EXPECT_EQ(t->asset, 101);
  EXPECT_EQ(t->side, post_trade::Side::Buy);
  EXPECT_EQ(t->qty, 10000);
  EXPECT_EQ(t->price_mantissa, 128750);
  EXPECT_EQ(t->price_decimals, 4);
  EXPECT_DOUBLE_EQ(t->price(), 12.875);
  EXPECT_EQ(t->trade_date, 20261118u);
  EXPECT_EQ(t->trade_time_ns, 34293'100'000'000LL);
  EXPECT_EQ(t->settle_date, 0u);
  EXPECT_EQ(t->account, "ABCD01");
  EXPECT_EQ(t->firm, "BIGJ");
  EXPECT_EQ(t->counterparty, "");
  EXPECT_FALSE(t->poss_dup);
  EXPECT_EQ(t->raw, sample_line());
}

TEST(DropMapping, CorrectionAndBreakReferToTheTrade)
{
  MapContext c{"S0001", 50, 20261118, true, nullptr};
  auto a = to_trade_capture(sample('C'), "", c);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->event, post_trade::Event::Amend);
  EXPECT_EQ(a->ref_trade_id, "122853.836455");
  EXPECT_EQ(a->asset, -1) << "unmapped symbol";
  EXPECT_TRUE(a->poss_dup);
  auto b = to_trade_capture(sample('B'), "", c);
  ASSERT_TRUE(b);
  EXPECT_EQ(b->event, post_trade::Event::Bust);
  EXPECT_EQ(b->ref_trade_id, b->trade_id);
}

TEST(DropMapping, SidesAndOrderEvents)
{
  MapContext c{"S", 1, 0, false, nullptr};
  auto r = sample('E');
  r.side = 'T';
  EXPECT_EQ(to_trade_capture(r, "", c)->side, post_trade::Side::SellShort);
  r.side = 'E';
  EXPECT_EQ(to_trade_capture(r, "", c)->side, post_trade::Side::SellShortExempt);
  r.side = 'S';
  EXPECT_EQ(to_trade_capture(r, "", c)->side, post_trade::Side::Sell);
  for (char t : {'A', 'X', 'U', 'Y'})
    EXPECT_FALSE(to_trade_capture(sample(t), "", c)) << t;
}

TEST(DropMapping, EasternDate)
{
  // 2026-11-18 03:00 UTC is 2026-11-17 22:00 EST.
  EXPECT_EQ(eastern_date(1'794'970'800), 20261117u); // 2026-11-18T03:00:00Z
  EXPECT_EQ(eastern_date(1'794'985'200), 20261118u); // 2026-11-18T07:00:00Z
  // DST 2026 starts 2026-03-08 07:00 UTC: 03:30 UTC that day is 22:30 EST on the 7th,
  // 2026-07-01 03:30 UTC is 23:30 EDT on June 30.
  EXPECT_EQ(eastern_date(1'772'940'600), 20260307u); // 2026-03-08T03:30:00Z
  EXPECT_EQ(eastern_date(1'782'876'600), 20260630u); // 2026-07-01T03:30:00Z
  EXPECT_EQ(eastern_date(1'782'880'200), 20260701u); // 2026-07-01T04:30:00Z
}
