// Decode T7 EOBI R15.0 messages through the header generated from eobi.tbl.
// Every byte sequence is built from the layouts of "T7 Release 15.0 Enhanced
// Order Book Interface Manual", Version 1 (29 July 2026), chapter 8, at the
// manual's offsets (BodyLen at 0, TemplateID at 2, MsgSeqNum at 4), little
// endian (7.4). The manual has no byte-level examples.
#include "eobi.hpp"
#include "../../test_support.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

using namespace test_support;

namespace
{
// A message as the manual lays it out: BodyLen and TemplateID filled in,
// fields written at the manual's offsets, little endian.
struct Msg : le_writer
{
  Msg(uint16_t len, uint16_t tid, uint32_t seq) : le_writer(len)
  {
    put<uint16_t>(0, len);
    put<uint16_t>(2, tid);
    put<uint32_t>(4, seq);
  }
};

constexpr int64_t NO_PRICE = INT64_MIN; // 0x8000000000000000, 7.4
constexpr uint64_t NO_TIME = UINT64_MAX; // 0xFFFFFFFFFFFFFFFF, 7.4
constexpr int64_t px(int64_t whole, int64_t frac8 = 0) { return whole * 100000000 + frac8; } // 8 decimals
constexpr int64_t qty(int64_t whole, int64_t frac4 = 0) { return whole * 10000 + frac4; }   // 4 decimals
} // namespace

int main()
{
  using namespace t7;
  namespace E = eobi::from_t7;
  namespace P = pkt::from_t7;
  namespace G = grp::from_t7;

  const int64_t sec = 5578483;            // SecurityID
  const uint64_t prio = 1785312000123456789ull; // TrdRegTSTimePriority
  const uint64_t t0 = 1785312000200000000ull;

  // Packet Header, 32 bytes (8.2, p.40-41).
  Msg ph(32, 13002, 0xFFFFFFFFu);
  ph.put<uint32_t>(8, 4711).put<int32_t>(12, 589).put<uint8_t>(16, 3).put<uint8_t>(17, 1)
      .put<uint8_t>(18, 0).put<uint8_t>(19, 0x07).put<uint64_t>(24, t0);
  {
    auto d = P::decode(ph.data(), ph.size());
    const auto h = msg_as<P::PacketHeader>(d);
    CHECK(d.size == 32 && h.MessageLength == 32 && h.MessageType == 13002);
    CHECK(h.MsgSeqNum == 0xFFFFFFFFu && h.ApplSeqNum == 4711 && h.MarketSegmentID == 589);
    CHECK(h.PartitionID == 3 && h.CompletionIndicator == 1 && h.ApplSeqResetIndicator == 0);
    CHECK(h.DSCP == 0x07 && h.TransactTime == t0);
    // A Packet Header of another release (another TemplateID) is not read with this layout.
    Msg other = ph;
    other.put<uint16_t>(2, 13004);
    auto u = P::decode(other.data(), other.size());
    CHECK(u.st == status::unknown && u.size == 32);
  }

  // Order Add, 56 bytes (8.4, p.56-57).
  Msg add(56, 13100, 101);
  add.put<uint64_t>(8, t0 - 5000).put<int64_t>(16, sec).put<uint64_t>(24, prio)
      .put<int64_t>(32, qty(25)).put<uint8_t>(40, 1).put<uint8_t>(41, 0xFF).put<int8_t>(42, -1)
      .put<int64_t>(48, px(18452, 50000000));
  {
    auto d = E::decode(add.data(), add.size());
    const auto m = msg_as<E::OrderAdd>(d);
    CHECK(d.size == 56 && m.MsgSeqNum == 101 && m.RequestTime == t0 - 5000);
    CHECK(m.SecurityID == sec && m.TrdRegTSTimePriority == prio && m.DisplayQty == qty(25));
    CHECK(m.Side == 1 && m.OrdType == 0xFF && m.HHIIndicator == -1 && m.Price == px(18452, 50000000));
  }

  // Order Modify (new priority), 88 bytes (8.4, p.59-61).
  Msg mod(88, 13101, 102);
  mod.put<uint64_t>(8, t0).put<uint64_t>(16, prio).put<int64_t>(24, px(18452, 50000000))
      .put<int64_t>(32, qty(25)).put<int64_t>(40, sec).put<uint64_t>(48, prio + 1000)
      .put<int64_t>(56, qty(30)).put<uint8_t>(64, 1).put<uint8_t>(65, 0xFF).put<int8_t>(66, 4)
      .put<int64_t>(72, px(18453)).put<int8_t>(80, int8_t(0x80));
  {
    auto d = E::decode(mod.data(), mod.size());
    const auto m = msg_as<E::OrderModify>(d);
    CHECK(d.size == 88 && m.MsgSeqNum == 102 && m.RequestTime == t0);
    CHECK(m.TrdRegTSPrevTimePriority == prio && m.PrevPrice == px(18452, 50000000));
    CHECK(m.PrevDisplayQty == qty(25) && m.SecurityID == sec);
    CHECK(m.TrdRegTSTimePriority == prio + 1000 && m.DisplayQty == qty(30) && m.Side == 1);
    CHECK(m.OrdType == 0xFF && m.HHIIndicator == 4 && m.Price == px(18453));
    CHECK(m.PrevPriceHHIIndicator == INT8_MIN);
  }

  // Order Modify Same Priority, 72 bytes (8.4, p.62-63).
  Msg msp(72, 13106, 103);
  msp.put<uint64_t>(8, NO_TIME).put<uint64_t>(16, t0 + 1).put<int64_t>(24, qty(30))
      .put<int64_t>(32, sec).put<uint64_t>(40, prio + 1000).put<int64_t>(48, qty(12, 5000))
      .put<uint8_t>(56, 1).put<uint8_t>(57, 0xFF).put<int8_t>(58, -1).put<int64_t>(64, px(18453));
  {
    auto d = E::decode(msp.data(), msp.size());
    const auto m = msg_as<E::OrderModifySamePriority>(d);
    CHECK(d.size == 72 && m.MsgSeqNum == 103 && m.RequestTime == NO_TIME && m.TransactTime == t0 + 1);
    CHECK(m.PrevDisplayQty == qty(30) && m.SecurityID == sec && m.TrdRegTSTimePriority == prio + 1000);
    CHECK(m.DisplayQty == qty(12, 5000) && m.Side == 1 && m.OrdType == 0xFF && m.HHIIndicator == -1);
    CHECK(m.Price == px(18453));
  }

  // Order Delete, 64 bytes (8.4, p.64-65).
  Msg del(64, 13102, 104);
  del.put<uint64_t>(8, t0 + 2).put<uint64_t>(16, t0 + 3).put<int64_t>(24, sec)
      .put<uint64_t>(32, prio + 1000).put<int64_t>(40, qty(12, 5000)).put<uint8_t>(48, 2)
      .put<uint8_t>(49, 1).put<int8_t>(50, 0).put<int64_t>(56, NO_PRICE);
  {
    auto d = E::decode(del.data(), del.size());
    const auto m = msg_as<E::OrderDelete>(d);
    CHECK(d.size == 64 && m.MsgSeqNum == 104 && m.RequestTime == t0 + 2 && m.TransactTime == t0 + 3);
    CHECK(m.SecurityID == sec && m.TrdRegTSTimePriority == prio + 1000 && m.DisplayQty == qty(12, 5000));
    CHECK(m.Side == 2 && m.OrdType == 1 && m.HHIIndicator == 0 && m.Price == NO_PRICE);
  }

  // Order Mass Delete, 24 bytes (8.4, p.66).
  Msg mass(24, 13103, 105);
  mass.put<int64_t>(8, sec).put<uint64_t>(16, t0 + 4);
  {
    auto d = E::decode(mass.data(), mass.size());
    const auto m = msg_as<E::OrderMassDelete>(d);
    CHECK(d.size == 24 && m.MsgSeqNum == 105 && m.SecurityID == sec && m.TransactTime == t0 + 4);
  }

  // Execution Summary, 104 bytes (8.3, p.43-45).
  Msg es(104, 13202, 106);
  es.put<int64_t>(8, sec).put<uint64_t>(16, t0 + 5).put<uint64_t>(24, t0 + 9).put<int64_t>(32, qty(7))
      .put<uint8_t>(40, 2).put<uint16_t>(42, 0xFFFF).put<uint8_t>(44, 3).put<int64_t>(48, px(18451))
      .put<uint64_t>(56, prio + 2000).put<int64_t>(64, qty(3)).put<int64_t>(72, px(18450))
      .put<int64_t>(80, qty(0)).put<int64_t>(88, qty(1)).put<uint64_t>(96, t0 + 7);
  {
    auto d = E::decode(es.data(), es.size());
    const auto m = msg_as<E::ExecutionSummary>(d);
    CHECK(d.size == 104 && m.MsgSeqNum == 106 && m.SecurityID == sec && m.RequestTime == t0 + 5);
    CHECK(m.ExecID == t0 + 9 && m.LastQty == qty(7) && m.AggressorSide == 2 && m.TradeCondition == 0xFFFF);
    CHECK(m.TradingHHIIndicator == 3 && m.LastPx == px(18451) && m.TrdRegTSPrevTimePriority == prio + 2000);
    CHECK(m.DisplayQty == qty(3) && m.Price == px(18450) && m.RestingHiddenQty == 0);
    CHECK(m.RestingCxlQty == qty(1) && m.AggressorTime == t0 + 7);
  }

  // Partial Order Execution and Full Order Execution, 56 bytes each (8.4, p.67-70).
  for (uint16_t tid : {uint16_t(13105), uint16_t(13104)})
  {
    Msg ex(56, tid, 107);
    ex.put<uint8_t>(8, 1).put<uint8_t>(9, 0xFF).put<uint8_t>(10, 1).put<int8_t>(11, -1)
        .put<uint32_t>(12, 900001).put<int64_t>(16, px(18451)).put<uint64_t>(24, prio)
        .put<int64_t>(32, sec).put<int64_t>(40, qty(4)).put<int64_t>(48, px(18450, 99000000));
    auto d = E::decode(ex.data(), ex.size());
    CHECK(d && d.size == 56);
    auto check = [&](const auto &m) {
      CHECK(m.MsgSeqNum == 107 && m.Side == 1 && m.OrdType == 0xFF && m.AlgorithmicTradeIndicator == 1);
      CHECK(m.HHIIndicator == -1 && m.TrdMatchID == 900001 && m.Price == px(18451));
      CHECK(m.TrdRegTSTimePriority == prio && m.SecurityID == sec && m.LastQty == qty(4));
      CHECK(m.LastPx == px(18450, 99000000));
    };
    if (tid == 13105)
      check(msg_as<E::PartialOrderExecution>(d));
    else
      check(msg_as<E::FullOrderExecution>(d));
  }

  // Trade Report, 56 bytes (8.3, p.49-51): an auction price without turnover, LastQty 0 (4.13).
  Msg tr(56, 13201, 108);
  tr.put<int64_t>(8, sec).put<uint64_t>(16, t0 + 10).put<int64_t>(24, 0).put<int64_t>(32, px(18455))
      .put<uint32_t>(40, 0xFFFFFFFFu).put<uint8_t>(44, 7).put<uint8_t>(45, 1).put<uint8_t>(46, 0xFF)
      .put<uint16_t>(48, 0xFFFF);
  {
    auto d = E::decode(tr.data(), tr.size());
    const auto m = msg_as<E::TradeReport>(d);
    CHECK(d.size == 56 && m.MsgSeqNum == 108 && m.SecurityID == sec && m.TransactTime == t0 + 10);
    CHECK(m.LastQty == 0 && m.LastPx == px(18455) && m.TrdMatchID == 0xFFFFFFFFu);
    CHECK(m.MatchType == 7 && m.MatchSubType == 1 && m.AlgorithmicTradeIndicator == 0xFF);
    CHECK(m.TradeCondition == 0xFFFF);
  }

  // Instrument State Change, 56 bytes (8.5, p.83-87).
  Msg isc(56, 13301, 109);
  isc.put<int64_t>(8, sec).put<uint8_t>(16, 1).put<uint8_t>(17, 204).put<uint8_t>(18, 0)
      .put<uint8_t>(19, 0).put<uint8_t>(20, 10).put<uint8_t>(21, 0xFF).put<int64_t>(24, px(18600))
      .put<int64_t>(32, px(18300)).put<uint64_t>(40, t0 + 11).put<uint8_t>(48, 1);
  {
    auto d = E::decode(isc.data(), isc.size());
    const auto m = msg_as<E::InstrumentStateChange>(d);
    CHECK(d.size == 56 && m.MsgSeqNum == 109 && m.SecurityID == sec && m.SecurityStatus == 1);
    CHECK(m.SecurityTradingStatus == 204 && m.MarketCondition == 0 && m.FastMarketIndicator == 0);
    CHECK(m.SecurityTradingEvent == 10 && m.SoldOutIndicator == 0xFF && m.HighPx == px(18600));
    CHECK(m.LowPx == px(18300) && m.TransactTime == t0 + 11 && m.TESSecurityStatus == 1);
  }

  // Snapshot channel: Product Summary (24), Instrument Summary (64 + 32 per
  // entry) and Snapshot Order (40) (8.7, p.96-105).
  Msg ps(24, 13600, 0);
  ps.put<uint32_t>(8, 109).put<uint8_t>(12, 1).put<uint8_t>(13, 3).put<uint8_t>(14, 2)
      .put<uint8_t>(15, 0).put<uint8_t>(16, 0).put<uint8_t>(17, 2);
  {
    auto d = E::decode(ps.data(), ps.size());
    const auto m = msg_as<E::ProductSummary>(d);
    CHECK(d.size == 24 && m.MsgSeqNum == 0 && m.LastMsgSeqNumProcessed == 109);
    CHECK(m.TradingSessionID == 1 && m.TradingSessionSubID == 3 && m.TradSesStatus == 2);
    CHECK(m.MarketCondition == 0 && m.FastMarketIndicator == 0 && m.TESTradSesStatus == 2);
  }
  Msg is(64 + 2 * 32, 13601, 1);
  is.put<int64_t>(8, sec).put<uint64_t>(16, t0 + 12).put<uint64_t>(24, t0 + 9).put<uint16_t>(32, 1)
      .put<uint8_t>(34, 1).put<uint8_t>(35, 203).put<uint8_t>(36, 0).put<uint8_t>(37, 0)
      .put<uint8_t>(38, 0xFF).put<uint8_t>(39, 0xFF).put<int64_t>(40, NO_PRICE).put<int64_t>(48, NO_PRICE)
      .put<uint8_t>(56, 1).put<uint8_t>(57, 2).put<uint8_t>(58, 1)
      // entry 0 at 64: last trade; entry 1 at 96: trade volume
      .put<int64_t>(64, px(18451)).put<int64_t>(72, qty(4)).put<uint8_t>(80, 0).put<uint8_t>(81, 2)
      .put<uint16_t>(82, 0xFFFF).put<uint16_t>(84, 0xFFFF).put<uint8_t>(86, 0xFF).put<uint8_t>(87, 0xFF)
      .put<int64_t>(88, NO_PRICE)
      .put<int64_t>(96, NO_PRICE).put<int64_t>(104, qty(1250)).put<uint8_t>(112, 0).put<uint8_t>(113, 66);
  {
    auto d = E::decode(is.data(), is.size());
    const auto m = msg_as<E::InstrumentSummary>(d);
    CHECK(d.size == 128 && m.MsgSeqNum == 1 && m.SecurityID == sec && m.LastUpdateTime == t0 + 12);
    CHECK(m.TrdRegTSExecutionTime == t0 + 9 && m.TotNoOrders == 1 && m.SecurityStatus == 1);
    CHECK(m.SecurityTradingStatus == 203 && m.MarketCondition == 0 && m.FastMarketIndicator == 0);
    CHECK(m.SecurityTradingEvent == 0xFF && m.SoldOutIndicator == 0xFF && m.HighPx == NO_PRICE);
    CHECK(m.LowPx == NO_PRICE && m.ProductComplex == 1 && m.NoMDEntries == 2 && m.TESSecurityStatus == 1);
    CHECK(d.tail == is.data() + 64 && d.tail_len == m.NoMDEntries * sizeof(G::MDInstrumentEntry));
    G::MDInstrumentEntry e[2];
    std::memcpy(e, d.tail, sizeof e);
    CHECK(e[0].MDEntryPx == px(18451) && e[0].MDEntrySize == qty(4) && e[0].MDOriginType == 0);
    CHECK(e[0].MDEntryType == 2 && e[0].TradeCondition == 0xFFFF && e[0].TrdType == 0xFFFF);
    CHECK(e[0].MultiLegReportingType == 0xFF && e[0].MultiLegPriceModel == 0xFF);
    CHECK(e[0].NonDisclosedTradeVolume == NO_PRICE);
    CHECK(e[1].MDEntryPx == NO_PRICE && e[1].MDEntrySize == qty(1250) && e[1].MDEntryType == 66);
  }
  Msg so(40, 13602, 2);
  so.put<uint64_t>(8, prio).put<int64_t>(16, qty(4)).put<uint8_t>(24, 2).put<uint8_t>(25, 0xFF)
      .put<int8_t>(26, 5).put<int64_t>(32, px(18452));
  {
    auto d = E::decode(so.data(), so.size());
    const auto m = msg_as<E::SnapshotOrder>(d);
    CHECK(d.size == 40 && m.MsgSeqNum == 2 && m.TrdRegTSTimePriority == prio && m.DisplayQty == qty(4));
    CHECK(m.Side == 2 && m.OrdType == 0xFF && m.HHIIndicator == 5 && m.Price == px(18452));
  }

  // Trade Reversal with one MDTradeEntry (56 + 24) (8.3, p.52-53).
  Msg rev(80, 13200, 110);
  rev.put<int64_t>(8, sec).put<uint64_t>(16, t0 + 13).put<int64_t>(24, qty(4)).put<int64_t>(32, px(18451))
      .put<uint64_t>(40, t0 + 1).put<uint32_t>(48, 900001).put<uint16_t>(52, 0xFFFF).put<uint8_t>(54, 0)
      .put<uint8_t>(55, 1).put<int64_t>(56, px(18449)).put<int64_t>(64, NO_PRICE).put<uint8_t>(72, 2);
  {
    auto d = E::decode(rev.data(), rev.size());
    const auto m = msg_as<E::TradeReversal>(d);
    CHECK(d.size == 80 && m.SecurityID == sec && m.TransactTime == t0 + 13 && m.LastQty == qty(4));
    CHECK(m.LastPx == px(18451) && m.TrdRegTSExecutionTime == t0 + 1 && m.TrdMatchID == 900001);
    CHECK(m.TradeCondition == 0xFFFF && m.MDOriginType == 0 && m.NoMDEntries == 1 && d.tail_len == 24);
    G::MDTradeEntry e;
    std::memcpy(&e, d.tail, sizeof e);
    CHECK(e.MDEntryPx == px(18449) && e.MDEntrySize == NO_PRICE && e.MDEntryType == 2);
  }

  // A datagram: Packet Header, Order Add, Full Order Execution, walked by size.
  {
    Msg fe(56, 13104, 112);
    fe.put<uint8_t>(8, 2).put<uint32_t>(12, 900002).put<int64_t>(16, px(18452)).put<uint64_t>(24, prio)
        .put<int64_t>(32, sec).put<int64_t>(40, qty(25)).put<int64_t>(48, px(18452));
    Msg a2 = add;
    a2.put<uint32_t>(4, 111);
    std::vector<char> dg;
    for (const Msg *m : {&ph, &a2, &fe})
      dg.insert(dg.end(), m->b.begin(), m->b.end());
    CHECK(dg.size() == 32 + 56 + 56);

    const char *p = dg.data();
    size_t left = dg.size();
    auto h = P::decode(p, left);
    CHECK(h && h.size == 32 && std::get<P::PacketHeader>(h.msg).ApplSeqNum == 4711);
    p += h.size, left -= h.size;
    std::vector<uint32_t> seqs;
    std::vector<std::string_view> names;
    while (left > 0)
    {
      auto d = E::decode(p, left);
      CHECK(d && d.size > 0 && d.size <= left);
      names.push_back(E::name(d.msg));
      std::visit([&](const auto &m) { seqs.push_back(uint32_t(m.MsgSeqNum)); }, d.msg);
      p += d.size, left -= d.size;
    }
    CHECK(names.size() == 2 && names[0] == "OrderAdd" && names[1] == "FullOrderExecution");
    CHECK(seqs[0] == 111 && seqs[1] == 112); // contiguous per product (4, p.12)

    // Truncation, an unknown TemplateID and a BodyLen shorter than the layout.
    CHECK(E::decode(dg.data() + 32, 55).st == status::incomplete);
    CHECK(E::decode(dg.data() + 32, 3).st == status::incomplete);
    Msg unk(16, 13999, 1);
    auto u = E::decode(unk.data(), unk.size());
    CHECK(u.st == status::unknown && u.size == 16);
    Msg shrt = add;
    shrt.put<uint16_t>(0, 40);
    auto s = E::decode(shrt.data(), shrt.size());
    CHECK(s.st == status::malformed && s.size == 40);
  }

  std::cout << "t7 eobi OK\n";
}
