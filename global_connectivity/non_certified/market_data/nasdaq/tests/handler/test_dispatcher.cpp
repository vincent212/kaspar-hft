// Dispatcher: one test per ITCH message type, against the records feed.md
// section 7 lists, and the trading-status table (decision 6).
#include "ItchBuilder.hpp"

#include <gtest/gtest.h>

using namespace nasdaq_md;
using namespace nasdaq_md::test;

namespace
{
constexpr uint16_t L = 7;
constexpr int32_t A = 1000 + L;

class DispatcherTest : public ::testing::Test
{
protected:
  Instruments ins{test_registrar()};
  RecSink sink;
  Dispatcher d{test_config(), ins, sink};
  uint64_t seq = 1;

  void feed(const std::string &m) { d.on_itch(m.data(), m.size(), seq++); }

  void SetUp() override
  {
    feed(SysEvent('O', 10));
    feed(Dir(L, "TEST", 11));
    feed(SysEvent('S', 12));
    feed(Action(L, "TEST", 'T', 13));
    sink.clear();
  }

  template <class T>
  T rec(size_t i)
  {
    auto v = sink.data();
    EXPECT_LT(i, v.size());
    EXPECT_TRUE(std::holds_alternative<T>(v.at(i).second)) << "record " << i;
    return std::get<T>(v.at(i).second);   // by value: data() is a copy
  }
  size_t n() { return sink.data().size(); }
};

TEST_F(DispatcherTest, AddOrderIsANewOrder)
{
  feed(Add(L, 42, 'B', 300, 1234500, 1000));
  ASSERT_EQ(n(), 1u);
  EXPECT_EQ(sink.data()[0].first, A);
  const auto &r = rec<bfile::l3_mbo_v2_t>(0);
  EXPECT_EQ(r.typ, en::l3::MBO_V2);
  EXPECT_EQ(r.venue, char(en::x::NASDAQ));
  EXPECT_EQ(r.orderUpdateAction, 0);
  EXPECT_EQ(r.securityID, A);
  EXPECT_EQ(r.orderID, 42u);
  EXPECT_DOUBLE_EQ(r.pxd, 123.45);
  EXPECT_EQ(r.displayQty, 300u);
  EXPECT_EQ(r.side, '0');
  EXPECT_TRUE(r.endOfEvent);
  EXPECT_FALSE(r.recovery);
  EXPECT_EQ(r.transactTime, kMidnight + 1000);
  EXPECT_EQ(r.sendingTime, r.transactTime);
  EXPECT_EQ(r.order_flags, 0);
  EXPECT_GT(r.priority, 0u);
}

TEST_F(DispatcherTest, AddOrderMPIDIsANewOrderWithoutTheMPID)
{
  feed(AddMPID(L, 43, 'S', 200, 1235000, 1000));
  ASSERT_EQ(n(), 1u);
  const auto &r = rec<bfile::l3_mbo_v2_t>(0);
  EXPECT_EQ(r.side, '1');
  EXPECT_EQ(r.orderID, 43u);
  EXPECT_DOUBLE_EQ(r.pxd, 123.50);
}

TEST_F(DispatcherTest, PriorityIncreasesWithEveryNewOrder)
{
  feed(Add(L, 1, 'B', 100, 1000000, 1000));
  feed(Add(L, 2, 'B', 100, 1000000, 1001));
  EXPECT_LT(rec<bfile::l3_mbo_v2_t>(0).priority, rec<bfile::l3_mbo_v2_t>(1).priority);
}

TEST_F(DispatcherTest, OrderExecutedPrintsThenExecutesThenChanges)
{
  feed(Add(L, 42, 'B', 300, 1234500, 1000));
  sink.clear();
  feed(Exec(L, 42, 100, 2000));
  ASSERT_EQ(n(), 3u);
  const auto &p = rec<bfile::l3_mbp_trd_t>(0);
  EXPECT_DOUBLE_EQ(p.pxd, 123.45);   // E executes at the order's display price
  EXPECT_EQ(p.sz, 100);
  EXPECT_EQ(p.aggresorSide, 2);      // resting buy: the aggressor sold (decision 5)
  EXPECT_EQ(p.securityID, A);
  EXPECT_FALSE(p.endOfEvent);
  const auto &t = rec<bfile::l3_mbo_trd_v2_t>(1);
  EXPECT_EQ(t.orderID, 42u);
  EXPECT_EQ(t.lastQty, 100);
  EXPECT_FALSE(t.endOfEvent);
  const auto &b = rec<bfile::l3_mbo_v2_t>(2);
  EXPECT_EQ(b.orderUpdateAction, 1);
  EXPECT_EQ(b.displayQty, 200u);
  EXPECT_DOUBLE_EQ(b.pxd, 123.45);
  EXPECT_TRUE(b.endOfEvent);
  EXPECT_EQ(p.transactTime, kMidnight + 2000);
  EXPECT_EQ(t.transactTime, p.transactTime);
  EXPECT_EQ(b.transactTime, p.transactTime);
}

TEST_F(DispatcherTest, OrderExecutedInFullDeletes)
{
  feed(Add(L, 42, 'S', 300, 1234500, 1000));
  sink.clear();
  feed(Exec(L, 42, 300, 2000));
  ASSERT_EQ(n(), 3u);
  EXPECT_EQ(rec<bfile::l3_mbp_trd_t>(0).aggresorSide, 1);   // resting sell: the aggressor bought
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(2).orderUpdateAction, 2);
  EXPECT_EQ(d.book().size(), 0u);
}

TEST_F(DispatcherTest, ExecutedWithPricePrintableAtTheExecutionPrice)
{
  feed(Add(L, 42, 'B', 300, 1234500, 1000));
  sink.clear();
  feed(ExecPx(L, 42, 100, 'Y', 1234000, 2000));
  ASSERT_EQ(n(), 3u);
  EXPECT_DOUBLE_EQ(rec<bfile::l3_mbp_trd_t>(0).pxd, 123.40);
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(2).displayQty, 200u);
  EXPECT_DOUBLE_EQ(rec<bfile::l3_mbo_v2_t>(2).pxd, 123.45);   // the book keeps its price
}

TEST_F(DispatcherTest, ExecutedWithPriceNonPrintableHasNoPrint)
{
  feed(Add(L, 42, 'B', 300, 1234500, 1000));
  sink.clear();
  feed(ExecPx(L, 42, 300, 'N', 1234000, 2000));
  ASSERT_EQ(n(), 2u);
  EXPECT_EQ(rec<bfile::l3_mbo_trd_v2_t>(0).lastQty, 300);
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(1).orderUpdateAction, 2);
  EXPECT_TRUE(rec<bfile::l3_mbo_v2_t>(1).endOfEvent);
}

TEST_F(DispatcherTest, CancelReducesThenDeletes)
{
  feed(Add(L, 42, 'B', 300, 1234500, 1000));
  sink.clear();
  feed(Cancel(L, 42, 100, 2000));
  ASSERT_EQ(n(), 1u);
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(0).orderUpdateAction, 1);
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(0).displayQty, 200u);
  EXPECT_DOUBLE_EQ(rec<bfile::l3_mbo_v2_t>(0).pxd, 123.45);
  feed(Cancel(L, 42, 200, 2001));
  ASSERT_EQ(n(), 2u);
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(1).orderUpdateAction, 2);
}

TEST_F(DispatcherTest, DeleteDeletes)
{
  feed(Add(L, 42, 'B', 300, 1234500, 1000));
  sink.clear();
  feed(Delete(L, 42, 2000));
  ASSERT_EQ(n(), 1u);
  const auto &r = rec<bfile::l3_mbo_v2_t>(0);
  EXPECT_EQ(r.orderUpdateAction, 2);
  EXPECT_EQ(r.orderID, 42u);
  EXPECT_EQ(r.side, '0');
  EXPECT_TRUE(r.endOfEvent);
  EXPECT_EQ(d.book().size(), 0u);
}

TEST_F(DispatcherTest, ReplaceIsDeleteThenAddAtTheBack)
{
  feed(Add(L, 42, 'S', 300, 1234500, 1000));
  sink.clear();
  feed(Replace(L, 42, 43, 200, 1234500, 2000));
  ASSERT_EQ(n(), 2u);
  const auto &del = rec<bfile::l3_mbo_v2_t>(0);
  const auto &add = rec<bfile::l3_mbo_v2_t>(1);
  EXPECT_EQ(del.orderUpdateAction, 2);
  EXPECT_EQ(del.orderID, 42u);
  EXPECT_FALSE(del.endOfEvent);
  EXPECT_EQ(add.orderUpdateAction, 0);
  EXPECT_EQ(add.orderID, 43u);
  EXPECT_EQ(add.side, '1');          // carried over from the original
  EXPECT_EQ(add.displayQty, 200u);
  EXPECT_TRUE(add.endOfEvent);
  EXPECT_EQ(add.transactTime, del.transactTime);
  EXPECT_GT(add.priority, del.priority);
  // the original is gone; the new reference is used from now on
  sink.clear();
  feed(Delete(L, 42, 2001));
  EXPECT_EQ(n(), 0u);
  EXPECT_EQ(d.stats().unknown_ref['D'], 1u);
  feed(Delete(L, 43, 2002));
  EXPECT_EQ(n(), 1u);
}

TEST_F(DispatcherTest, NonCrossTradeIsAPrintOnly)
{
  feed(TradeP(L, 75, 1234700, 2000));
  ASSERT_EQ(n(), 1u);
  const auto &p = rec<bfile::l3_mbp_trd_t>(0);
  EXPECT_DOUBLE_EQ(p.pxd, 123.47);
  EXPECT_EQ(p.sz, 75);
  EXPECT_EQ(p.aggresorSide, 0);
  EXPECT_TRUE(p.endOfEvent);
}

TEST_F(DispatcherTest, CrossTradeIsAPrintWhenSharesArePositive)
{
  feed(CrossQ(L, 0, 1234800, 2000));
  EXPECT_EQ(n(), 0u);
  feed(CrossQ(L, 5000, 1234800, 2001));
  ASSERT_EQ(n(), 1u);
  EXPECT_EQ(rec<bfile::l3_mbp_trd_t>(0).sz, 5000);
  EXPECT_EQ(rec<bfile::l3_mbp_trd_t>(0).aggresorSide, 0);
  feed(CrossQ(L, 5000000000ull, 1234800, 2002));
  ASSERT_EQ(n(), 2u);
  EXPECT_EQ(rec<bfile::l3_mbp_trd_t>(1).sz, std::numeric_limits<int32_t>::max());
  EXPECT_EQ(d.stats().cross_capped, 1u);
}

TEST_F(DispatcherTest, BrokenTradeAndNoiiProduceNoRecord)
{
  feed(Broken(L, 9, 2000));
  feed(Noii(L, 2001));
  EXPECT_EQ(n(), 0u);
  EXPECT_EQ(d.stats().broken, 1u);
}

TEST_F(DispatcherTest, UnknownReferencesAreCountedAndProduceNothing)
{
  feed(Exec(L, 99, 1, 2000));
  feed(ExecPx(L, 99, 1, 'Y', 1, 2000));
  feed(Cancel(L, 99, 1, 2000));
  feed(Delete(L, 99, 2000));
  feed(Replace(L, 99, 100, 1, 1, 2000));
  EXPECT_EQ(n(), 0u);
  for (char t : {'E', 'C', 'X', 'D', 'U'})
    EXPECT_EQ(d.stats().unknown_ref[uint8_t(t)], 1u) << t;
}

TEST_F(DispatcherTest, SecuritiesNotRoutedAreTrackedButNotSent)
{
  feed(Dir(9, "NOROUTE", 20));
  feed(Add(9, 50, 'B', 100, 10000, 2000));
  feed(Exec(9, 50, 100, 2001));
  EXPECT_EQ(n(), 0u);
  EXPECT_EQ(d.stats().unknown_ref['E'], 0u);
  EXPECT_EQ(d.book().size(), 0u);
}

TEST_F(DispatcherTest, OffTickOrdersAreKeptButNotSent)
{
  feed(Add(L, 1, 'B', 100, 50, 2000));   // $0.0050 on a penny-tick asset
  EXPECT_EQ(n(), 0u);
  EXPECT_EQ(d.stats().off_tick, 1u);
  feed(Exec(L, 1, 100, 2001));
  EXPECT_EQ(n(), 0u);
  EXPECT_EQ(d.stats().unknown_ref['E'], 0u);
  feed(Dir(8, "SUBX", 30));               // tick 0.0001: sent
  sink.clear();
  feed(Add(8, 2, 'B', 100, 50, 2002));
  EXPECT_EQ(n(), 1u);
}

TEST_F(DispatcherTest, EndOfPacketSendsEopToTheRecorderAndEobToTouchedBooks)
{
  d.begin_packet(0x1234567890ull, 77);
  feed(Add(L, 1, 'B', 100, 10000, 2000));
  d.end_packet();
  ASSERT_EQ(sink.book.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eob_t>(sink.book[1].second));
  const auto &mbo = std::get<bfile::l3_mbo_v2_t>(sink.book[0].second);
  EXPECT_EQ(mbo.pkt_seq_num, 0x34567890u);
  EXPECT_EQ(mbo.handlerendtim, 77u);
  ASSERT_FALSE(sink.rec.empty());
  const auto *eop = std::get_if<bfile::l3_eop_t>(&sink.rec.back());
  ASSERT_NE(eop, nullptr);
  EXPECT_EQ(eop->msgSeqNum, 0x34567890u);   // low 32 bits
  EXPECT_EQ(eop->sendingTime, kMidnight + 2000);
  // a packet that touched no book sends no EOB
  sink.clear();
  d.begin_packet(2, 0);
  d.end_packet();
  EXPECT_TRUE(sink.book.empty());
}

TEST_F(DispatcherTest, EndOfMessagesClearsEveryBook)
{
  feed(Add(L, 1, 'B', 100, 10000, 2000));
  sink.clear();
  feed(SysEvent('C', 3000));
  ASSERT_GE(n(), 1u);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_chr_v2_t>(sink.data()[0].second));
  EXPECT_EQ(sink.data()[0].first, A);
  EXPECT_EQ(d.book().size(), 0u);
}

TEST_F(DispatcherTest, GapGoesToEveryRoutedBook)
{
  feed(Dir(8, "OTHER", 30));
  sink.clear();
  d.gap();
  ASSERT_EQ(n(), 2u);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_gap_v2_t>(sink.data()[0].second));
}

// ---- trading status (feed.md section 7, decision 6) --------------------------
class StatusTest : public ::testing::Test
{
protected:
  Instruments ins{test_registrar()};
  RecSink sink;
  Dispatcher d{test_config(), ins, sink};
  uint64_t seq = 1;
  void feed(const std::string &m) { d.on_itch(m.data(), m.size(), seq++); }
  int last_status()
  {
    for (auto it = sink.book.rbegin(); it != sink.book.rend(); ++it)
      if (auto *s = std::get_if<bfile::l3_sst_t>(&it->second))
        return s->tradingStatus;
    return -1;
  }
  size_t ssts()
  {
    size_t k = 0;
    for (auto &x : sink.book)
      k += std::holds_alternative<bfile::l3_sst_t>(x.second);
    return k;
  }
};

TEST_F(StatusTest, TheSessionTable)
{
  feed(SysEvent('O', 1));
  feed(Dir(L, "TEST", 2));
  EXPECT_EQ(last_status(), 21);                       // before Start of System hours
  feed(Action(L, "TEST", 'T', 3));
  EXPECT_EQ(last_status(), 21);                       // T before S is still pre-open
  feed(SysEvent('S', 4));
  EXPECT_EQ(last_status(), 17);                       // T from S
  feed(SysEvent('Q', 5));
  feed(SysEvent('M', 6));
  EXPECT_EQ(last_status(), 17);                       // Q and M change nothing
  feed(Action(L, "TEST", 'P', 7));
  EXPECT_EQ(last_status(), 2);                        // paused
  feed(Action(L, "TEST", 'Q', 8));
  EXPECT_EQ(last_status(), 21);                       // quotation only
  feed(Action(L, "TEST", 'H', 9));
  EXPECT_EQ(last_status(), 2);
  feed(Action(L, "TEST", 'T', 10));
  EXPECT_EQ(last_status(), 17);
  feed(OpHalt(L, "TEST", 'B', 'H', 11));              // BX halt: not this market
  EXPECT_EQ(last_status(), 17);
  feed(OpHalt(L, "TEST", 'Q', 'H', 12));
  EXPECT_EQ(last_status(), 2);
  feed(OpHalt(L, "TEST", 'Q', 'T', 13));
  EXPECT_EQ(last_status(), 17);
  feed(SysEvent('E', 14));
  EXPECT_EQ(last_status(), 4);
}

TEST_F(StatusTest, ASecurityAbsentFromThePreOpenSpinIsHalted)
{
  feed(Dir(L, "TEST", 2));
  feed(SysEvent('S', 4));
  EXPECT_EQ(last_status(), 2);
  feed(Action(L, "TEST", 'T', 5));
  EXPECT_EQ(last_status(), 17);
}

TEST_F(StatusTest, SentOnlyWhenTheDerivedValueChanges)
{
  feed(Dir(L, "TEST", 2));
  feed(Action(L, "TEST", 'T', 3));
  feed(SysEvent('S', 4));
  const size_t k = ssts();
  feed(Action(L, "TEST", 'T', 5));
  feed(SysEvent('Q', 6));
  EXPECT_EQ(ssts(), k);
}

// ---- recovery ---------------------------------------------------------------
TEST_F(DispatcherTest, GlimpseAddsAreRecoveryRecordsAndLiveRecordsFollowThem)
{
  namespace gl = nasdaq::glimpse::from_nasdaq;
  feed(Add(L, 1, 'B', 100, 10000, 5000));
  sink.clear();
  d.begin_recovery();
  EXPECT_TRUE(std::holds_alternative<bfile::l3_chr_v2_t>(sink.data().at(0).second));
  EXPECT_EQ(d.book().size(), 0u);
  // spun Add at 9000 ns; GLIMPSE messages have no length prefix
  Msg a;
  a.hdr('A', L, 9000).u(2, 8).c('S').u(50, 4).s("TEST", 8).u(10100, 4);
  sink.clear();
  EXPECT_EQ(d.on_glimpse(a.b.data(), a.b.size()), 0u);
  ASSERT_EQ(n(), 1u);
  EXPECT_TRUE(rec<bfile::l3_mbo_v2_t>(0).recovery);
  Msg g;
  g.c('G').s("                 123", 20);
  EXPECT_EQ(d.on_glimpse(g.b.data(), g.b.size()), 123u);
  d.end_recovery(123);
  // message 123 at the same nanosecond as the spin: restamped one ns later
  sink.clear();
  d.on_itch(Delete(L, 2, 9000).data(), Delete(L, 2, 9000).size(), 123);
  ASSERT_EQ(n(), 1u);
  EXPECT_FALSE(rec<bfile::l3_mbo_v2_t>(0).recovery);
  EXPECT_EQ(rec<bfile::l3_mbo_v2_t>(0).transactTime, kMidnight + 9001);
  EXPECT_EQ(d.stats().ts_clamped, 1u);
  EXPECT_EQ(d.stats().boundary_suspect, 0u);
}

TEST_F(DispatcherTest, TheMessageAtNIsCheckedAgainstTheSpin)
{
  d.begin_recovery();
  d.end_recovery(50);
  const auto m = Delete(L, 77, 9000);   // an order the spin did not have
  d.on_itch(m.data(), m.size(), 50);
  EXPECT_EQ(d.stats().boundary_suspect, 1u);
  const auto m2 = Delete(L, 78, 9001);  // only N is checked
  d.on_itch(m2.data(), m2.size(), 51);
  EXPECT_EQ(d.stats().boundary_suspect, 1u);
}
} // namespace
