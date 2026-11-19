/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The dispatcher, one ITCH message type at a time, against the records
// feed.md section 7 says it sends.

#include <gtest/gtest.h>

#include <cstring>

#include "TestUtil.hpp"

using namespace jnx_md;
using namespace jnx_md::test;

namespace
{
  constexpr uint32_t kSecs = 27004;   // 07:30:04, before market hours
  constexpr uint64_t kBase = kMidnight + uint64_t(kSecs) * 1000000000ull;

  template <size_t N, size_t M>
  BookKey book_key(const char (&a)[N], const char (&b)[M])
  {
    char x[4] = {' ', ' ', ' ', ' '}, y[4] = {' ', ' ', ' ', ' '};
    std::memcpy(x, a, N - 1);
    std::memcpy(y, b, M - 1);
    return jnx_md::book_key(x, y);
  }

  class DispatcherTest : public ::testing::Test
  {
  protected:
    RecSink sink;
    Handler h{config(), sink};
    Feeder feed{h};
    int32_t asset = 0;

    void SetUp() override
    {
      std::vector<std::string> day = {T(kSecs)};
      for (auto &l : table37())
        day.push_back(l);
      day.push_back(R("1301", "DAY", 37, 40000, 20000));
      day.push_back(H(5, "1301", "DAY", 'T'));
      feed(day);
      asset = h.instruments().find(book_key("1301", "DAY"))->asset;
      sink.clear();
    }

    static bfile::l3_mbo_v2_t only_mbo(const RecSink &s)
    {
      auto v = s.of<bfile::l3_mbo_v2_t>();
      EXPECT_EQ(v.size(), 1u);
      return v.empty() ? bfile::l3_mbo_v2_t{} : v[0];
    }
  };
}

// Directory: l3_lim_t with the limits in JPY; status 18 until the spin's H,
// then 21 before market hours, 17 after Q, 4 after M, 2 when suspended.
TEST(DispatcherDirectory, LimitsAndTradingStatus)
{
  RecSink sink;
  Handler h(config(), sink);
  Feeder feed{h};
  std::vector<std::string> day = {T(kSecs), L(37, 1, 0), L(37, 5, 30000),
                                  R("7203", "DAY", 37, 35000, 25000)};
  feed(day);
  auto lim = sink.of<bfile::l3_lim_t>();
  ASSERT_EQ(lim.size(), 1u);
  EXPECT_DOUBLE_EQ(lim[0].pxh, 3500.0);
  EXPECT_DOUBLE_EQ(lim[0].pxl, 2500.0);
  EXPECT_EQ(lim[0].venue, char(en::x::JNX));
  auto sst = sink.of<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 1u);
  EXPECT_EQ(sst[0].tradingStatus, 18);   // not in the spin yet: suspended (ITCH 4.5)

  sink.clear();
  feed({H(10, "7203", "DAY", 'T')});
  sst = sink.of<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 1u);
  EXPECT_EQ(sst[0].tradingStatus, 21);   // 07:30, before market hours
  EXPECT_EQ(sst[0].txtim, kBase + 10);

  sink.clear();
  feed({S(20, "DAY", 'Q')});
  sst = sink.of<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 1u);
  EXPECT_EQ(sst[0].tradingStatus, 17);

  sink.clear();
  feed({H(30, "7203", "DAY", 'V')});
  ASSERT_EQ(sink.of<bfile::l3_sst_t>().size(), 1u);
  EXPECT_EQ(sink.of<bfile::l3_sst_t>()[0].tradingStatus, 2);

  sink.clear();
  feed({H(40, "7203", "DAY", 'T'), S(50, "", 'M')});   // blank group: every group
  sst = sink.of<bfile::l3_sst_t>();
  ASSERT_EQ(sst.size(), 2u);
  EXPECT_EQ(sst[0].tradingStatus, 17);
  EXPECT_EQ(sst[1].tradingStatus, 4);

  // Y, and S events other than Q and M, send nothing.
  sink.clear();
  feed({Y(60, "7203", "DAY", '1'), S(70, "", 'E'), S(80, "", 'C')});
  EXPECT_TRUE(sink.of<bfile::l3_sst_t>().empty());
  EXPECT_TRUE(sink.of<bfile::l3_mbo_v2_t>().empty());
}

// Without a Q, the market-hours phase comes from the clock (decision 5).
TEST(DispatcherDirectory, PhaseFromTheClockWithoutQ)
{
  RecSink sink;
  Handler h(config(), sink);
  Feeder feed{h};
  feed({T(8 * 3600), L(37, 1, 0), R("7203", "DAY", 37, 35000, 25000), H(1, "7203", "DAY", 'T')});
  EXPECT_EQ(sink.of<bfile::l3_sst_t>().back().tradingStatus, 21);
  sink.clear();
  feed({T(8 * 3600 + 20 * 60)});   // 08:20: start of trading
  ASSERT_EQ(sink.of<bfile::l3_sst_t>().size(), 1u);
  EXPECT_EQ(sink.of<bfile::l3_sst_t>()[0].tradingStatus, 17);
  sink.clear();
  feed({T(16 * 3600 + 30 * 60)});   // 16:30: end of trading
  EXPECT_EQ(sink.of<bfile::l3_sst_t>()[0].tradingStatus, 4);
}

TEST_F(DispatcherTest, OrderAdded)
{
  feed({A(123000, 202310190000000001ull, 'B', 300, "1301", "DAY", 25125)});
  const auto m = only_mbo(sink);
  EXPECT_EQ(m.typ, en::l3::MBO_V2);
  EXPECT_EQ(m.venue, char(en::x::JNX));
  EXPECT_EQ(m.orderUpdateAction, 0);
  EXPECT_EQ(m.securityID, asset);
  EXPECT_EQ(m.orderID, 202310190000000001ull);
  EXPECT_EQ(m.side, '0');
  EXPECT_DOUBLE_EQ(m.pxd, 2512.5);
  EXPECT_EQ(m.displayQty, 300u);
  EXPECT_EQ(m.transactTime, kBase + 123000);
  EXPECT_EQ(m.sendingTime, kBase + 123000);
  EXPECT_EQ(m.priority, feed.seq - 1);   // the sequence number of the add
  EXPECT_TRUE(m.endOfEvent);
  EXPECT_FALSE(m.recovery);
  EXPECT_EQ(m.order_flags, 0);
}

TEST_F(DispatcherTest, OrderAddedWithAttributes)
{
  feed({F(5, 77, 'S', 100, "1301", "DAY", 26000)});
  const auto m = only_mbo(sink);
  EXPECT_EQ(m.orderUpdateAction, 0);
  EXPECT_EQ(m.side, '1');
  EXPECT_DOUBLE_EQ(m.pxd, 2600.0);
  EXPECT_EQ(h.dispatcher().orders().at(77).order_type, 'Q');
}

// A reference price (Order Number 0) is not an order.
TEST_F(DispatcherTest, ReferencePriceSendsNothing)
{
  feed({A(5, 0, ' ', 0, "1301", "DAY", 25000), A(6, 0, ' ', 0, "1301", "DAY", 0x7FFFFFFF)});
  EXPECT_TRUE(sink.of<bfile::l3_mbo_v2_t>().empty());
  EXPECT_EQ(h.stats().reference_prices, 2u);
}

// E: the print, the execution, then the book change, on one transactTime.
TEST_F(DispatcherTest, PartialExecution)
{
  feed({A(5, 10, 'B', 500, "1301", "DAY", 25000)});
  sink.clear();
  feed({E(900, 10, 200)});
  ASSERT_EQ(sink.recs.size(), 4u);   // + EOP to the recorder
  const auto &p = std::get<bfile::l3_mbp_trd_t>(sink.recs[0].r);
  EXPECT_EQ(p.typ, en::l3::MBPT);
  EXPECT_EQ(p.securityID, asset);
  EXPECT_DOUBLE_EQ(p.pxd, 2500.0);
  EXPECT_EQ(p.sz, 200);
  EXPECT_EQ(p.side, '0');           // the resting order's side
  EXPECT_EQ(p.aggresorSide, 2);     // so a sell aggressed
  EXPECT_EQ(p.numorders, 1);
  const auto &x = std::get<bfile::l3_mbo_trd_v2_t>(sink.recs[1].r);
  EXPECT_EQ(x.orderID, 10u);
  EXPECT_EQ(x.lastQty, 200);
  const auto &c = std::get<bfile::l3_mbo_v2_t>(sink.recs[2].r);
  EXPECT_EQ(c.orderUpdateAction, 1);
  EXPECT_EQ(c.displayQty, 300u);
  EXPECT_DOUBLE_EQ(c.pxd, 2500.0);
  EXPECT_EQ(p.transactTime, kBase + 900);
  EXPECT_EQ(x.transactTime, p.transactTime);
  EXPECT_EQ(c.transactTime, p.transactTime);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eop_t>(sink.recs[3].r));
  EXPECT_EQ(sink.recs[3].where, Rec::Recorder);
}

TEST_F(DispatcherTest, FullExecutionRemovesTheOrder)
{
  feed({A(5, 11, 'S', 100, "1301", "DAY", 25010)});
  sink.clear();
  feed({E(6, 11, 100)});
  const auto mbo = sink.of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(mbo.size(), 1u);
  EXPECT_EQ(mbo[0].orderUpdateAction, 2);
  EXPECT_EQ(sink.of<bfile::l3_mbp_trd_t>()[0].aggresorSide, 1);   // a buy took the offer
  EXPECT_TRUE(h.dispatcher().orders().empty());
  sink.clear();
  feed({D(7, 11), E(8, 11, 1), U(9, 11, 12, 1, 1)});   // no D follows a full execution
  EXPECT_TRUE(sink.of<bfile::l3_mbo_v2_t>().empty());
  EXPECT_EQ(h.stats().unknown_delete, 1u);
  EXPECT_EQ(h.stats().unknown_exec, 1u);
  EXPECT_EQ(h.stats().unknown_replace, 1u);
}

TEST_F(DispatcherTest, OrderDeleted)
{
  feed({A(5, 12, 'B', 100, "1301", "DAY", 25000)});
  sink.clear();
  feed({D(6, 12)});
  const auto m = only_mbo(sink);
  EXPECT_EQ(m.orderUpdateAction, 2);
  EXPECT_EQ(m.orderID, 12u);
  EXPECT_TRUE(h.dispatcher().orders().empty());
  EXPECT_TRUE(h.instruments().find(book_key("1301", "DAY"))->book.empty());
}

// U: action 1 on the original orderID with the new price and size; the
// replacement is found by its new number afterwards. Queue place follows OB's
// rule (decision 2).
TEST_F(DispatcherTest, OrderReplaced)
{
  feed({A(5, 20, 'B', 300, "1301", "DAY", 25000), A(6, 21, 'B', 100, "1301", "DAY", 25000)});
  sink.clear();
  feed({U(7, 20, 30, 200, 25000)});   // same price, smaller: keeps its place
  auto m = only_mbo(sink);
  EXPECT_EQ(m.orderUpdateAction, 1);
  EXPECT_EQ(m.orderID, 20u);
  EXPECT_EQ(m.displayQty, 200u);
  EXPECT_EQ(m.side, '0');
  EXPECT_DOUBLE_EQ(m.pxd, 2500.0);
  const Instrument *i = h.instruments().find(book_key("1301", "DAY"));
  ASSERT_EQ(i->book.bids.at(25000).q.size(), 2u);
  EXPECT_EQ(i->book.bids.at(25000).q.front()->number, 30u);
  EXPECT_EQ(i->book.bids.at(25000).qty, 300u);

  sink.clear();
  feed({U(8, 30, 31, 200, 25000 + 1), U(9, 31, 32, 200, 25000)});   // away and back
  EXPECT_EQ(i->book.bids.at(25000).q.front()->number, 21u);       // now behind 21
  EXPECT_EQ(i->book.bids.at(25000).q.back()->number, 32u);
  m = sink.of<bfile::l3_mbo_v2_t>().back();
  EXPECT_EQ(m.orderID, 20u);
  EXPECT_EQ(h.stats().replace_keep_place, 1u);
  EXPECT_EQ(h.stats().replace_lose_place, 2u);

  sink.clear();
  feed({E(10, 32, 200)});   // executions find it by its new number
  EXPECT_EQ(sink.of<bfile::l3_mbo_trd_v2_t>()[0].orderID, 20u);
}

// orderID = (stream index << 58) | Order Number (section 7).
TEST(DispatcherOrderID, StreamIndexInTheTopBits)
{
  RecSink sink;
  Handler h(config(false, 5), sink);
  Feeder feed{h};
  feed({T(kSecs), L(37, 1, 0), R("1301", "DAY", 37, 40000, 20000),
        A(1, 202310190000204680ull, 'B', 100, "1301", "DAY", 25000)});
  const auto m = sink.of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].orderID, (5ull << 58) | 202310190000204680ull);
  EXPECT_EQ(h.stats().wide_order_number, 0u);
}

// One End of Packet per MoldUDP64 packet, to the recorder only, with the low
// 32 bits of the packet's first sequence number.
TEST_F(DispatcherTest, EndOfPacket)
{
  const uint64_t first = feed.seq;
  feed({A(1, 40, 'B', 100, "1301", "DAY", 25000), A(2, 41, 'S', 100, "1301", "DAY", 25100)});
  ASSERT_FALSE(sink.recs.empty());
  const auto &last = sink.recs.back();
  EXPECT_EQ(last.where, Rec::Recorder);
  EXPECT_EQ(std::get<bfile::l3_eop_t>(last.r).msgSeqNum, uint32_t(first));
  EXPECT_EQ(std::get<bfile::l3_eop_t>(last.r).sendingTime, kBase + 2);
}

// Seconds that go backward within a session add a day (night session).
TEST(DispatcherTime, SecondsWrapAddADay)
{
  RecSink sink;
  Handler h(config(), sink);
  Feeder feed{h};
  feed({T(86399), L(37, 1, 0), R("1301", "NGHT", 37, 40000, 20000), H(0, "1301", "NGHT", 'T'),
        T(1), A(5, 1, 'B', 100, "1301", "NGHT", 25000)});
  const auto m = sink.of<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].transactTime, kMidnight + 86401ull * 1000000000ull + 5);
  EXPECT_EQ(h.stats().seconds_wraps, 1u);
  // 00:00:01 the next day is 24:00:01 of the session: the night market is open.
  EXPECT_EQ(sink.of<bfile::l3_sst_t>().back().tradingStatus, 17);
}

// Prices off the venue's tick grid and outside the limits are counted.
TEST_F(DispatcherTest, PriceChecks)
{
  feed({A(1, 50, 'B', 100, "1301", "DAY", 30003),    // 3000.3: tick 0.5 above 3000
        A(2, 51, 'B', 100, "1301", "DAY", 45000)});  // above the 4000.0 limit
  EXPECT_EQ(h.stats().px_off_tick, 1u);
  EXPECT_EQ(h.stats().px_out_of_limits, 1u);
}

// The Kaspar tick of a book: the gcd of the ticks its limits reach.
TEST(Instruments, UnitFromTheLimits)
{
  Instruments::Config c;
  c.register_assets = false;
  Instruments ins(c);
  for (auto [tick, start] : std::vector<std::pair<uint32_t, uint32_t>>{
           {1, 0}, {5, 30000}, {10, 50000}, {50, 300000}, {100, 500000}, {500, 3000000},
           {1000, 5000000}})
    ins.tick_row(37, tick, start);
  EXPECT_EQ(ins.unit_for(37, 25000, 15000), 1u);       // 1,500 to 2,500 JPY
  EXPECT_EQ(ins.unit_for(37, 450000, 350000), 50u);    // 35,000 to 45,000 JPY
  EXPECT_EQ(ins.unit_for(37, 600000, 400000), 50u);    // reaches the 10 JPY band too
  EXPECT_EQ(ins.unit_for(37, 35000, 30000), 1u);       // 3,000.0 itself is in the 0.1 band
  EXPECT_EQ(ins.unit_for(37, 0, 0), 1u);               // no limits: the whole table
  EXPECT_EQ(ins.unit_for(99, 1000, 10), 1u);           // unknown table
  EXPECT_EQ(ins.tick_at(37, 30000), 1u);               // boundary: the lower band
  EXPECT_EQ(ins.tick_at(37, 30001), 5u);
  EXPECT_EQ(ins.tick_at(37, 6000000), 1000u);
}

TEST(Time, SessionDate)
{
  // 2023-10-18 22:30:04 UTC is 07:30:04 JST on the 19th.
  EXPECT_EQ(session_date(1697668204000000000ull), 20231019);
  // 2023-10-19 17:00 UTC is 02:00 JST on the 20th: still the night session of the 19th.
  EXPECT_EQ(session_date(1697734800000000000ull), 20231019);
  EXPECT_EQ(jst_midnight_ns(20231019), kMidnight);
}
