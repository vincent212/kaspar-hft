// PitchEuHandler on synthetic packets built from the cboe_europe.tbl structs.
// Not certified: no Cboe Europe capture was available (README.md).

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "cboe_europe/PitchEuHandler.hpp"
#include "cboe_europe/gen/cboe_europe.hpp"
#include "cboe_europe/gen/pitch_us.hpp"

using namespace cboe_europe;
namespace pitch = cboe::pitch::from_cboe_eu;

namespace
{
  constexpr int32_t VOD = 101, BARC = 102, SAP = 201;
  constexpr uint32_t DATE = 20261118; // GMT

  struct Rec
  {
    int32_t sec;
    bfile::l3_t r;
  };

  template <typename M>
  std::string msg(M m)
  {
    m.MessageLength = sizeof(M);
    m.MessageType = M::type;
    return std::string(reinterpret_cast<const char *>(&m), sizeof m);
  }

  std::string packet(uint8_t unit, uint32_t seq, const std::vector<std::string> &msgs)
  {
    std::string body;
    for (const auto &m : msgs)
      body += m;
    cboe::unit::from_cboe::SequencedUnitHeader h{};
    h.HdrLength = uint16_t(8 + body.size());
    h.HdrCount = uint8_t(msgs.size());
    h.HdrUnit = unit;
    h.HdrSequence = seq;
    return std::string(reinterpret_cast<const char *>(&h), 8) + body;
  }

  std::string time_msg(uint32_t s)
  {
    pitch::TimeMessage m{};
    m.Time = s;
    return msg(m);
  }

  std::string add_long(uint64_t id, char side, uint32_t qty, const char *sym, uint64_t px, uint32_t to = 0)
  {
    pitch::AddOrderLong m{};
    m.TimeOffset = to;
    m.OrderId = id;
    m.SideIndicator = side;
    m.Quantity = qty;
    m.set_Symbol(sym);
    m.Price = px;
    return msg(m);
  }

  std::string add_short(uint64_t id, char side, uint16_t qty, const char *sym, uint16_t px, uint32_t to = 0)
  {
    pitch::AddOrderShort m{};
    m.TimeOffset = to;
    m.OrderId = id;
    m.SideIndicator = side;
    m.Quantity = qty;
    m.set_Symbol(sym);
    m.Price = px;
    return msg(m);
  }

  std::string executed(uint64_t id, uint32_t qty, const char *flags = "12--", uint32_t to = 0)
  {
    pitch::OrderExecuted m{};
    m.TimeOffset = to;
    m.OrderId = id;
    m.ExecutedShares = qty;
    m.ExecutionId = 0xABCDEF;
    std::memcpy(m.ExecutionFlags, flags, 4);
    return msg(m);
  }

  std::string executed_at(uint64_t id, uint32_t qty, uint32_t remaining, uint64_t px, const char *flags = "12--")
  {
    pitch::OrderExecutedAtPriceSize m{};
    m.OrderId = id;
    m.ExecutedShares = qty;
    m.RemainingShares = remaining;
    m.ExecutionId = 1;
    m.Price = px;
    std::memcpy(m.ExecutionFlags, flags, 4);
    return msg(m);
  }

  std::string reduce(uint64_t id, uint32_t qty)
  {
    pitch::ReduceSizeLong m{};
    m.OrderId = id;
    m.CancelledShares = qty;
    return msg(m);
  }

  std::string modify(uint64_t id, uint32_t qty, uint64_t px)
  {
    pitch::ModifyOrderLong m{};
    m.OrderId = id;
    m.Shares = qty;
    m.Price = px;
    return msg(m);
  }

  std::string del(uint64_t id)
  {
    pitch::DeleteOrder m{};
    m.OrderId = id;
    return msg(m);
  }

  std::string trade_long(const char *sym, uint32_t qty, uint64_t px)
  {
    pitch::TradeLong m{};
    m.OrderId = 0;
    m.SideIndicator = 'B';
    m.Shares = qty;
    m.set_Symbol(sym);
    m.Price = px;
    m.ExecutionId = 7;
    std::memcpy(m.TradeFlags, "32D--", 5);
    return msg(m);
  }

  std::string trade_ext(const char *sym, uint64_t qty, uint64_t px, const char *mmt14)
  {
    pitch::TradeExtended m{};
    m.Shares = qty;
    m.set_Symbol(sym);
    m.Price = px;
    m.TradeID = 9;
    m.set_ExecutionVenue("CHIX");
    m.set_Currency("GBX");
    m.CboeTradeFlags = '-';
    std::memcpy(m.ExtendedTradeFlags, mmt14, 14);
    return msg(m);
  }

  template <typename M>
  std::string bare(uint32_t to = 0)
  {
    M m{};
    m.TimeOffset = to;
    return msg(m);
  }

  std::string status(const char *sym, char st)
  {
    pitch::TradingStatus m{};
    m.set_Symbol(sym);
    m.Status = st;
    std::memset(m.Reserved1, ' ', 3);
    return msg(m);
  }

  struct Fixture : ::testing::Test
  {
    std::vector<Rec> out;
    std::vector<std::tuple<uint8_t, uint32_t, uint32_t>> gaps;
    PitchEuHandler::Config cfg;
    std::unique_ptr<PitchEuHandler> h;

    void SetUp() override
    {
      cfg.venue = char(en::x::CBOECXE);
      cfg.trade_date = DATE;
      cfg.symbols = {{"VODl", VOD}, {"BARCl", BARC}, {"SAPd", SAP}};
      make();
    }
    void make()
    {
      h = std::make_unique<PitchEuHandler>(
          cfg, [this](int32_t sec, const bfile::l3_t &r) { out.push_back({sec, r}); },
          [this](uint8_t u, uint32_t f, uint32_t c) { gaps.emplace_back(u, f, c); });
    }
    void feed(const std::string &p) { ASSERT_TRUE(h->on_packet(p.data(), p.size())); }

    template <typename T>
    std::vector<std::pair<int32_t, T>> only() const
    {
      std::vector<std::pair<int32_t, T>> v;
      for (const auto &x : out)
        if (std::holds_alternative<T>(x.r))
          v.emplace_back(x.sec, std::get<T>(x.r));
      return v;
    }
    std::vector<bfile::l3_mbo_v2_t> mbo() const
    {
      std::vector<bfile::l3_mbo_v2_t> v;
      for (auto &[s, r] : only<bfile::l3_mbo_v2_t>())
        v.push_back(r);
      return v;
    }
  };
}

TEST(LondonTime, MidnightGmtAndBst)
{
  const uint64_t s = 1000000000ull;
  EXPECT_EQ(london_midnight_ns(20261118), 1794960000ull * s);         // 2026-11-18 00:00 UTC (date -u -r)
  EXPECT_EQ(london_midnight_ns(20260701), (1782864000ull - 3600) * s); // 2026-06-30 23:00 UTC
  // 2026: BST from Sunday 29 March to Sunday 25 October.
  EXPECT_EQ(london_midnight_ns(20260329) % (86400 * s), 0u);
  EXPECT_EQ(london_midnight_ns(20260330) % (86400 * s), (86400 - 3600) * s);
  EXPECT_EQ(london_midnight_ns(20261025) % (86400 * s), (86400 - 3600) * s);
  EXPECT_EQ(london_midnight_ns(20261026) % (86400 * s), 0u);
}

TEST(TradingStatus, Mapping)
{
  for (char c : std::string("TVN"))
    EXPECT_EQ(trading_status(c), 17) << c;
  for (char c : std::string("OEIMPUY"))
    EXPECT_EQ(trading_status(c), 21) << c;
  EXPECT_EQ(trading_status('C'), 4);
  EXPECT_EQ(trading_status('H'), 2);
  for (char c : std::string("SRZ"))
    EXPECT_EQ(trading_status(c), 18) << c;
}

TEST_F(Fixture, AddExecuteReduceModifyDelete)
{
  feed(packet(1, 1, {time_msg(34200), add_long(11, 'B', 1000, "VODl", 1025000, 447000)}));
  auto m = mbo();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].orderUpdateAction, 0);
  EXPECT_EQ(m[0].securityID, VOD);
  EXPECT_EQ(m[0].orderID, 11u);
  EXPECT_EQ(m[0].side, '0');
  EXPECT_DOUBLE_EQ(m[0].pxd, 102.5);
  EXPECT_EQ(m[0].displayQty, 1000u);
  EXPECT_EQ(m[0].priority, 2u); // the Add is the unit's second message
  EXPECT_TRUE(m[0].endOfEvent);
  EXPECT_FALSE(m[0].recovery);
  EXPECT_EQ(m[0].transactTime, london_midnight_ns(DATE) + 34200ull * 1000000000ull + 447000);
  EXPECT_EQ(m[0].venue, char(en::x::CBOECXE));

  // Execution: print, order execution, book change; one transactTime; endOfEvent last.
  out.clear();
  feed(packet(1, 3, {executed(11, 300)}));
  ASSERT_EQ(out.size(), 4u); // + end of packet
  const auto &pr = std::get<bfile::l3_mbp_trd_t>(out[0].r);
  const auto &ex = std::get<bfile::l3_mbo_trd_v2_t>(out[1].r);
  const auto &ch = std::get<bfile::l3_mbo_v2_t>(out[2].r);
  EXPECT_EQ(out[0].sec, VOD);
  EXPECT_EQ(out[1].sec, VOD);
  EXPECT_DOUBLE_EQ(pr.pxd, 102.5);
  EXPECT_EQ(pr.sz, 300);
  EXPECT_EQ(pr.aggresorSide, 2); // resting buy: the aggressor sold
  EXPECT_EQ(ex.orderID, 11u);
  EXPECT_EQ(ex.lastQty, 300);
  EXPECT_EQ(ch.orderUpdateAction, 1);
  EXPECT_EQ(ch.displayQty, 700u);
  EXPECT_FALSE(pr.endOfEvent);
  EXPECT_FALSE(ex.endOfEvent);
  EXPECT_TRUE(ch.endOfEvent);
  EXPECT_EQ(pr.transactTime, ex.transactTime);
  EXPECT_EQ(ex.transactTime, ch.transactTime);
  EXPECT_GT(pr.transactTime, m[0].transactTime); // same offset: bumped by 1 ns, never equal
  const auto &eop = std::get<bfile::l3_eop_t>(out[3].r);
  EXPECT_EQ(out[3].sec, -1);
  EXPECT_EQ(eop.msgSeqNum, 3u);

  out.clear();
  feed(packet(1, 4, {reduce(11, 200), modify(11, 900, 1030000), del(11)}));
  m = mbo();
  ASSERT_EQ(m.size(), 3u);
  EXPECT_EQ(m[0].orderUpdateAction, 1);
  EXPECT_EQ(m[0].displayQty, 500u);
  EXPECT_DOUBLE_EQ(m[0].pxd, 102.5);
  EXPECT_EQ(m[1].orderUpdateAction, 1); // modify: change record; OB applies the venue's priority rule
  EXPECT_EQ(m[1].displayQty, 900u);
  EXPECT_DOUBLE_EQ(m[1].pxd, 103.0);
  EXPECT_EQ(m[2].orderUpdateAction, 2);
  EXPECT_TRUE(m[0].endOfEvent && m[1].endOfEvent && m[2].endOfEvent);
  EXPECT_EQ(h->orders(1), 0u);
}

TEST_F(Fixture, ShortFormsAndExecutionAtPrice)
{
  feed(packet(2, 1, {add_short(5, 'S', 500, "BARCl", 10250)}));
  out.clear();
  feed(packet(2, 2, {executed_at(5, 100, 50, 1024000, "1K--")}));
  auto pr = only<bfile::l3_mbp_trd_t>();
  ASSERT_EQ(pr.size(), 1u);
  EXPECT_DOUBLE_EQ(pr[0].second.pxd, 102.4); // the message's price
  EXPECT_EQ(pr[0].second.aggresorSide, 0);   // closing auction: no aggressor
  auto m = mbo();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].displayQty, 50u);         // Remaining Shares, absolute
  EXPECT_DOUBLE_EQ(m[0].pxd, 102.5);       // the book price
  EXPECT_EQ(m[0].side, '1');
}

TEST_F(Fixture, OverExecutionRemovesOrder)
{
  feed(packet(1, 1, {add_long(1, 'S', 100, "VODl", 10000)}));
  out.clear();
  feed(packet(1, 2, {executed(1, 150)}));
  auto m = mbo();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].orderUpdateAction, 2);
  EXPECT_EQ(h->stats().over_executions, 1u);
  EXPECT_EQ(std::get<bfile::l3_mbp_trd_t>(out[0].r).aggresorSide, 1);
}

TEST_F(Fixture, TradesAndExtendedTrades)
{
  // The MMT string (EU 4.17, pp.32-35): offset 0 level 1, 3 levels 3.2/3.10,
  // 5 level 3.4, 9 level 3.8.
  const char *lit = "12-------P----";       // CLOB, continuous, plain vanilla
  const char *nego = "12-N-----P----";      // negotiated
  const char *cancel = "12---C---P----";    // cancellation of an earlier report
  const char *offbook = "45-------P----";   // off book
  const char *tech = "12-------T----";      // non price forming
  feed(packet(1, 1, {trade_long("VODl", 75000, 1025000), trade_ext("VODl", 100, 1025000, lit),
                     trade_ext("VODl", 100, 1025000, nego), trade_ext("VODl", 100, 1025000, cancel),
                     trade_ext("VODl", 100, 1025000, offbook), trade_ext("VODl", 100, 1025000, tech),
                     trade_ext("VODl", 100, 0, lit)}));
  auto pr = only<bfile::l3_mbp_trd_t>();
  ASSERT_EQ(pr.size(), 2u);
  EXPECT_EQ(pr[0].second.sz, 75000);
  EXPECT_EQ(pr[0].second.aggresorSide, 0);
  EXPECT_EQ(pr[1].second.sz, 100);
  EXPECT_EQ(h->stats().extended_printed, 1u);
  EXPECT_EQ(h->stats().extended_skipped, 5u);
  EXPECT_TRUE(mbo().empty()); // trades never touch the book

  cfg.extended_prints = PitchEuHandler::ExtendedPrints::none;
  make();
  out.clear();
  feed(packet(1, 1, {trade_ext("VODl", 100, 1025000, lit)}));
  EXPECT_TRUE(only<bfile::l3_mbp_trd_t>().empty());
}

TEST_F(Fixture, TransactionBlockIsOneEvent)
{
  feed(packet(1, 1, {add_long(1, 'S', 100, "VODl", 10000), add_long(2, 'S', 100, "VODl", 10000)}));
  out.clear();
  // One aggressor against two orders, across two packets (EU 4.11, p.26).
  feed(packet(1, 3, {bare<pitch::TransactionBegin>(10), executed(1, 100, "12--", 10)}));
  EXPECT_TRUE(mbo().empty()) << "held until Transaction End";
  feed(packet(1, 5, {executed(2, 40, "12--", 20), bare<pitch::TransactionEnd>(20)}));
  auto m = mbo();
  ASSERT_EQ(m.size(), 2u);
  EXPECT_FALSE(m[0].endOfEvent);
  EXPECT_TRUE(m[1].endOfEvent);
  EXPECT_EQ(m[0].transactTime, m[1].transactTime);
  int eop = 0, eoe = 0;
  for (auto &x : out)
  {
    eop += std::holds_alternative<bfile::l3_eop_t>(x.r);
    std::visit([&](auto &r) {
      if constexpr (requires { r.endOfEvent; })
        eoe += r.endOfEvent;
    }, x.r);
  }
  EXPECT_EQ(eop, 2);
  EXPECT_EQ(eoe, 1);

  // Without transaction markers honoured, every message is its own event.
  cfg.honour_transactions = false;
  make();
  out.clear();
  feed(packet(1, 1, {add_long(1, 'S', 100, "VODl", 10000), add_long(2, 'S', 100, "VODl", 10000)}));
  feed(packet(1, 3, {bare<pitch::TransactionBegin>(10), executed(1, 100, "12--", 10), executed(2, 40, "12--", 20),
                     bare<pitch::TransactionEnd>(20)}));
  m = mbo();
  ASSERT_EQ(m.size(), 4u);
  EXPECT_TRUE(m[2].endOfEvent && m[3].endOfEvent);
}

TEST_F(Fixture, TransactionBlockAcrossTwoBooks)
{
  // A block may hold messages for several symbols (EU 4.11, p.26): each book
  // gets endOfEvent on its own last record.
  feed(packet(1, 1, {add_long(1, 'S', 100, "VODl", 10000), add_long(2, 'S', 100, "BARCl", 10000)}));
  out.clear();
  feed(packet(1, 3, {bare<pitch::TransactionBegin>(), executed(1, 10), executed(2, 10), executed(1, 10),
                     bare<pitch::TransactionEnd>()}));
  std::map<int32_t, int> eoe, last;
  for (size_t i = 0; i < out.size(); ++i)
    std::visit([&](auto &r) {
      if constexpr (requires { r.endOfEvent; })
      {
        eoe[out[i].sec] += r.endOfEvent;
        if (r.endOfEvent)
          last[out[i].sec] = int(i);
      }
    }, out[i].r);
  EXPECT_EQ(eoe[VOD], 1);
  EXPECT_EQ(eoe[BARC], 1);
  EXPECT_EQ(last[VOD], 8);  // the change after the second execution of order 1
  EXPECT_EQ(last[BARC], 5); // the change after the execution of order 2
}

TEST_F(Fixture, DuplicatesDropped)
{
  auto p = packet(1, 1, {add_long(1, 'B', 100, "VODl", 10000)});
  feed(p);
  feed(p); // the other feed of an A/B pair
  EXPECT_EQ(mbo().size(), 1u);
  EXPECT_EQ(h->stats().duplicates, 1u);
}

TEST_F(Fixture, GapThenRetransmission)
{
  feed(packet(1, 1, {add_long(1, 'B', 100, "VODl", 10000)}));
  out.clear();
  feed(packet(1, 4, {add_long(4, 'B', 100, "VODl", 10000), add_long(5, 'B', 100, "VODl", 10000)}));
  ASSERT_EQ(gaps.size(), 1u);
  EXPECT_EQ(gaps[0], std::make_tuple(uint8_t(1), 2u, 2u));
  EXPECT_EQ(h->state(1), PitchEuHandler::UnitState::gapped);
  auto g = only<bfile::l3_gap_v2_t>();
  ASSERT_EQ(g.size(), 1u);
  EXPECT_EQ(g[0].first, VOD); // the unit's books
  EXPECT_TRUE(mbo().empty());

  // Gap response multicast, in two blocks, one overlapping what is buffered.
  feed(packet(1, 3, {add_long(3, 'B', 100, "VODl", 10000), add_long(4, 'B', 100, "VODl", 10000)}));
  EXPECT_TRUE(mbo().empty());
  feed(packet(1, 2, {add_long(2, 'B', 100, "VODl", 10000)}));
  auto m = mbo();
  ASSERT_EQ(m.size(), 4u);
  for (size_t i = 0; i < 4; ++i)
    EXPECT_EQ(m[i].orderID, i + 2);
  EXPECT_EQ(h->state(1), PitchEuHandler::UnitState::live);
  EXPECT_EQ(h->expected(1), 6u);
  EXPECT_EQ(h->stats().duplicates, 1u);
}

TEST_F(Fixture, HeartbeatRevealsGap)
{
  feed(packet(1, 1, {add_long(1, 'B', 100, "VODl", 10000)}));
  feed(packet(1, 5, {})); // real-time heartbeat: next sequence 5
  ASSERT_EQ(gaps.size(), 1u);
  EXPECT_EQ(gaps[0], std::make_tuple(uint8_t(1), 2u, 3u));
  feed(packet(1, 0, {})); // gap-channel or off-hours heartbeat
  EXPECT_EQ(gaps.size(), 1u);
}

TEST_F(Fixture, UnitClearClearsOnlyThatUnit)
{
  feed(packet(1, 1, {add_long(1, 'B', 100, "VODl", 10000), add_long(2, 'B', 100, "BARCl", 10000)}));
  feed(packet(7, 1, {add_long(1, 'B', 100, "SAPd", 10000)})); // same Order Id, other unit
  out.clear();
  feed(packet(1, 3, {bare<pitch::UnitClear>()}));
  auto c = only<bfile::l3_chr_v2_t>();
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0].first, VOD);
  EXPECT_EQ(c[1].first, BARC);
  EXPECT_EQ(h->orders(1), 0u);
  EXPECT_EQ(h->orders(7), 1u);
  feed(packet(1, 4, {del(1)}));
  EXPECT_EQ(h->stats().unknown_orders, 1u);
  feed(packet(7, 2, {del(1)}));
  auto m = mbo();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].securityID, SAP);
}

TEST_F(Fixture, LateJoinRecoversBySpin)
{
  // Joining at sequence 100: everything before is missing.
  feed(packet(3, 100, {add_long(50, 'S', 10, "VODl", 20000), del(40)}));
  ASSERT_EQ(gaps.size(), 1u);
  EXPECT_EQ(gaps[0], std::make_tuple(uint8_t(3), 1u, 99u));
  feed(packet(3, 102, {modify(50, 5, 20000)}));

  EXPECT_FALSE(h->spin_usable(3, 95)); // 96-99 never buffered (EU Appendix C, p.85)
  EXPECT_TRUE(h->spin_usable(3, 99));
  EXPECT_TRUE(h->spin_usable(3, 102));

  out.clear();
  h->spin_begin(3, 99);
  EXPECT_EQ(h->state(3), PitchEuHandler::UnitState::spinning);
  // Live data keeps arriving during the spin.
  feed(packet(3, 103, {add_long(60, 'B', 7, "VODl", 19000)}));
  h->spin_message(3, time_msg(36000).data(), 6);
  const auto st = status("VODl", 'T');
  h->spin_message(3, st.data(), st.size());
  const auto a = add_long(40, 'B', 20, "VODl", 19500);
  h->spin_message(3, a.data(), a.size());
  h->spin_finished(3);

  EXPECT_EQ(h->state(3), PitchEuHandler::UnitState::live);
  EXPECT_EQ(h->expected(3), 104u);
  auto m = mbo();
  ASSERT_EQ(m.size(), 5u);
  EXPECT_TRUE(m[0].recovery);
  EXPECT_EQ(m[0].orderID, 40u);
  EXPECT_EQ(m[1].orderID, 50u); // buffered 100: add
  EXPECT_FALSE(m[1].recovery);
  EXPECT_EQ(m[2].orderID, 40u); // buffered 101: delete of a spun order
  EXPECT_EQ(m[2].orderUpdateAction, 2);
  EXPECT_EQ(m[3].orderUpdateAction, 1); // 102: modify
  EXPECT_EQ(m[4].orderID, 60u);          // 103: arrived during the spin
  for (size_t i = 1; i < m.size(); ++i)
    EXPECT_GT(m[i].transactTime, m[i - 1].transactTime);
  auto s = only<bfile::l3_sst_t>();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].second.tradingStatus, 17);
  EXPECT_EQ(h->orders(3), 2u);
}

TEST_F(Fixture, SpinClearsBooksBeforeApplying)
{
  feed(packet(1, 1, {add_long(1, 'B', 100, "VODl", 10000)}));
  feed(packet(1, 3, {add_long(3, 'B', 100, "VODl", 10000)}));
  out.clear();
  h->spin_begin(1, 2);
  ASSERT_EQ(only<bfile::l3_chr_v2_t>().size(), 1u);
  EXPECT_EQ(h->orders(1), 0u);
  h->spin_finished(1);
  auto m = mbo();
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].orderID, 3u);
  EXPECT_EQ(h->state(1), PitchEuHandler::UnitState::live);
}

TEST_F(Fixture, UnknownSymbolsTrackedNotEmitted)
{
  feed(packet(1, 1, {add_long(1, 'B', 100, "XYZl", 10000), executed(1, 50), del(1), status("XYZl", 'T')}));
  EXPECT_TRUE(out.size() == 1u && std::holds_alternative<bfile::l3_eop_t>(out[0].r));
  EXPECT_EQ(h->stats().unknown_orders, 0u);
  EXPECT_EQ(h->stats().unknown_symbols, 2u);
}

TEST_F(Fixture, TradingStatusRecord)
{
  feed(packet(1, 1, {status("VODl", 'E'), status("VODl", 'T'), status("VODl", 'C')}));
  auto s = only<bfile::l3_sst_t>();
  ASSERT_EQ(s.size(), 3u);
  EXPECT_EQ(s[0].second.tradingStatus, 21);
  EXPECT_EQ(s[1].second.tradingStatus, 17);
  EXPECT_EQ(s[2].second.tradingStatus, 4);
  EXPECT_EQ(s[0].second.securityID, VOD);
}

TEST_F(Fixture, UnknownAndMalformedCounted)
{
  std::string iq(25, '\0');
  iq[0] = 25;
  iq[1] = char(0xD8); // Index Quote, not tabled
  std::string ox29(29, '\0');
  ox29[0] = 29;
  ox29[1] = 0x23; // Order Executed at the example's length (EU p.78)
  feed(packet(1, 1, {iq, ox29}));
  EXPECT_EQ(h->stats().unknown, 1u);
  EXPECT_EQ(h->stats().malformed, 1u);
  EXPECT_EQ(h->expected(1), 3u); // both still consume their sequence numbers
}

TEST_F(Fixture, RejectsBadUnits)
{
  std::string p = packet(1, 1, {del(1)});
  EXPECT_FALSE(h->on_packet(p.data(), 5));
  p[0] = char(100); // Hdr Length beyond the payload
  EXPECT_FALSE(h->on_packet(p.data(), p.size()));
}
