// Decode through the header generated from omdc.tbl. Messages are built at the
// offsets of the OMD-C Interface Specifications v1.47 (MsgSize at 0, MsgType
// at 2), not at the table's offsets, so the test checks the table against the
// specification. Examples: Currency Rate (p.21), Aggregate Order Book
// Management examples 1-6 (p.50-57), Broker Queue (p.27-28), Appendix B
// (p.63); Developers Guide v1.15 decimals (p.11), retransmission request
// (p.14-15), line arbitration (p.10).
//
//   fieldgen.py feeds/hkex/omdc.tbl -o <scratch>/omdc.hpp
//   c++ -std=c++20 -Wall -Wextra -Werror -I<scratch> hkex_decode_test.cpp
#include "omdc.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using bytes_t = std::vector<unsigned char>;
namespace om = hkex::omdc::from_hkex;
namespace grp = hkex::omdc::from_hkex_group;

// A message of `total` bytes with MsgSize and MsgType set; fields are put at
// the specification's offsets, little-endian.
struct Msg
{
  bytes_t b;
  Msg(uint16_t type, size_t total) : b(total, 0)
  {
    put<uint16_t>(0, uint16_t(total));
    put<uint16_t>(2, type);
  }
  template <typename T>
  Msg &put(size_t off, T v)
  {
    assert(off + sizeof(T) <= b.size());
    for (size_t i = 0; i < sizeof(T); ++i)
      b[off + i] = (unsigned char)((uint64_t)v >> (8 * i));
    return *this;
  }
  Msg &str(size_t off, std::string_view s, size_t n, char pad = ' ')
  {
    assert(off + n <= b.size() && s.size() <= n);
    for (size_t i = 0; i < n; ++i)
      b[off + i] = (unsigned char)(i < s.size() ? s[i] : pad);
    return *this;
  }
};

// A packet: 16-byte header (SPEC p.11), then the messages.
static bytes_t packet(uint32_t seq, const std::vector<bytes_t> &msgs, uint64_t send_time = 0)
{
  size_t n = 16;
  for (auto &m : msgs)
    n += m.size();
  bytes_t p(16, 0);
  auto put = [&](size_t off, uint64_t v, size_t len) {
    for (size_t i = 0; i < len; ++i)
      p[off + i] = (unsigned char)(v >> (8 * i));
  };
  put(0, n, 2);
  put(2, msgs.size(), 1);
  p[3] = ' ';
  put(4, seq, 4);
  put(8, send_time, 8);
  for (auto &m : msgs)
    p.insert(p.end(), m.begin(), m.end());
  return p;
}

static const char *cp(const bytes_t &b) { return (const char *)b.data(); }

template <typename M>
static M one(const bytes_t &b)
{
  auto d = om::decode(cp(b), b.size());
  assert(d && d.size == b.size() && std::holds_alternative<M>(d.msg));
  return std::get<M>(d.msg);
}

static hkex::pkt::from_hkex::PacketHeader header(const bytes_t &p)
{
  hkex::pkt::from_hkex::PacketHeader h;
  assert(p.size() >= sizeof h);
  std::memcpy(&h, p.data(), sizeof h);
  return h;
}

// ---- Aggregate order book (SPEC section 5, p.50-57) ----

struct Level
{
  int32_t px;
  uint64_t qty;
  bool operator==(const Level &) const = default;
};
using side_t = std::vector<Level>;

static bytes_t aob(uint32_t code, const std::vector<std::tuple<uint64_t, int32_t, uint16_t, uint8_t, uint8_t>> &es)
{
  Msg m(53, 12 + 24 * es.size());
  m.put<uint32_t>(4, code).put<uint8_t>(11, uint8_t(es.size()));
  size_t o = 12;
  for (auto &[qty, px, side, lvl, act] : es)
  {
    m.put<uint64_t>(o, qty).put<int32_t>(o + 8, px).put<uint32_t>(o + 12, 1).put<uint16_t>(o + 16, side);
    m.put<uint8_t>(o + 18, lvl).put<uint8_t>(o + 19, act);
    o += 24;
  }
  return m.b;
}

// Apply one message, entry by entry: insert, change or delete at PriceLevel,
// shifting the levels below; a side longer than 10 price levels loses its
// excess (implicit deletion, p.52); 74 clears both sides (p.57).
static void apply(const bytes_t &b, side_t &bid, side_t &ask)
{
  auto d = om::decode(cp(b), b.size());
  assert(d && d.size == b.size());
  const auto m = std::get<om::AggregateOrderBookUpdate>(d.msg);
  assert(d.tail_len == m.NoEntries * sizeof(grp::AggregateOrderBookEntry));
  for (size_t i = 0; i < m.NoEntries; ++i)
  {
    grp::AggregateOrderBookEntry e;
    std::memcpy(&e, d.tail + i * sizeof e, sizeof e);
    if (e.UpdateAction == 74)
    {
      bid.clear();
      ask.clear();
      continue;
    }
    side_t &s = e.Side == 0 ? bid : ask;
    const size_t at = size_t(e.PriceLevel) - 1;
    if (e.UpdateAction == 0)
    {
      assert(at <= s.size());
      s.insert(s.begin() + at, Level{e.Price, e.AggregateQuantity});
    }
    else
    {
      assert(at < s.size() && s[at].px == e.Price); // the level holds the price the entry names
      if (e.UpdateAction == 1)
        s[at].qty = e.AggregateQuantity;
      else
        s.erase(s.begin() + at);
    }
    if (s.size() > 10)
      s.resize(10);
  }
}

int main()
{
  using namespace hkex;

  // Packet header and two messages, walked by size (SPEC p.10-11, 3.2-3.3).
  {
    Msg add(30, 32);
    add.put<uint32_t>(4, 700).put<uint64_t>(8, 0x0123456789abcdefull).put<int32_t>(16, 512500);
    add.put<uint32_t>(20, 1000).put<uint16_t>(24, 1).str(26, "2", 1).put<int32_t>(28, 0);
    Msg trd(50, 32);
    trd.put<uint32_t>(4, 700).put<uint32_t>(8, 42).put<int32_t>(12, 512000).put<uint32_t>(16, 400);
    trd.put<int16_t>(20, 103).put<uint64_t>(24, 1763425800123456000ull);
    auto p = packet(1001, {add.b, trd.b}, 1763425800123000000ull);
    const auto h = header(p);
    assert(h.PktSize == 80 && h.PktSize == p.size() && h.MsgCount == 2 && h.SeqNum == 1001);
    assert(h.SendTime == 1763425800123000000ull);
    size_t pos = sizeof h;
    std::vector<om::message_t> got;
    for (unsigned i = 0; i < h.MsgCount; ++i)
    {
      auto d = om::decode(cp(p) + pos, p.size() - pos);
      assert(d && d.size == 32);
      got.push_back(d.msg);
      pos += d.size;
    }
    assert(pos == h.PktSize);
    const auto a = std::get<om::AddOrder>(got[0]);
    assert(a.MessageLength == 32 && a.MessageType == 30 && a.SecurityCode == 700);
    assert(a.OrderId == 0x0123456789abcdefull && a.Price == 512500 && a.Quantity == 1000);
    assert(a.Side == 1 && a.OrderType == '2' && a.OrderBookPosition == 0);
    const auto t = std::get<om::Trade>(got[1]);
    assert(t.SecurityCode == 700 && t.TradeID == 42 && t.Price == 512000 && t.Quantity == 400);
    assert(t.TrdType == 103 && t.TradeTime == 1763425800123456000ull);
    std::cout << a << "\n" << t << "\n";
  }

  // Heartbeat: header only, MsgCount 0, SeqNum of the previous message (SPEC p.11, 3.4.1).
  {
    auto p = packet(1002, {});
    const auto h = header(p);
    assert(h.PktSize == 16 && h.MsgCount == 0 && h.SeqNum == 1002);
  }

  // Order messages (SPEC p.24-26).
  {
    Msg mo(31, 28);
    mo.put<uint32_t>(4, 5).put<uint64_t>(8, 77).put<uint32_t>(16, 600).put<uint16_t>(20, 0).put<int32_t>(24, 0);
    auto m = one<om::ModifyOrder>(mo.b);
    assert(m.SecurityCode == 5 && m.OrderId == 77 && m.Quantity == 600 && m.Side == 0);
    Msg de(32, 20);
    de.put<uint32_t>(4, 5).put<uint64_t>(8, 77).put<uint16_t>(16, 1);
    auto d = one<om::DeleteOrder>(de.b);
    assert(d.OrderId == 77 && d.Side == 1);
    Msg ao(33, 28);
    ao.put<uint32_t>(4, 5).put<uint64_t>(8, 78).put<int32_t>(16, 3450).put<uint32_t>(20, 37);
    ao.put<uint16_t>(24, 1234).put<uint16_t>(26, 0);
    auto o = one<om::AddOddLotOrder>(ao.b);
    assert(o.Price == 3450 && o.Quantity == 37 && o.BrokerID == 1234 && o.Side == 0);
    Msg dl(34, 20);
    dl.put<uint32_t>(4, 5).put<uint64_t>(8, 78).put<uint16_t>(16, 1234).put<uint16_t>(18, 0);
    auto od = one<om::DeleteOddLotOrder>(dl.b);
    assert(od.OrderId == 78 && od.BrokerID == 1234);
    Msg tc(51, 12);
    tc.put<uint32_t>(4, 5).put<uint32_t>(8, 42);
    assert(one<om::TradeCancel>(tc.b).TradeID == 42);
  }

  // Control and refresh messages (SPEC p.11-12, p.15).
  {
    Msg sr(100, 8);
    sr.put<uint32_t>(4, 1);
    assert(one<om::SequenceReset>(sr.b).NewSeqNo == 1);
    Msg dr(105, 8);
    dr.put<uint32_t>(4, 2);
    assert(one<om::DisasterRecoverySignal>(dr.b).DRStatus == 2);
    Msg rc(203, 8);
    rc.put<uint32_t>(4, 0); // DG p.19: 0 when nothing was published on the real-time channel
    assert(one<om::RefreshComplete>(rc.b).LastSeqNum == 0);
  }

  // Trading Session Status at POS No Cancellation (Appendix B, p.63; field values p.22).
  {
    Msg ts(20, 32);
    ts.str(4, "MAIN", 4).put<uint8_t>(9, 101).put<uint8_t>(10, 20).str(11, "0", 1);
    ts.put<uint64_t>(16, 1763425800000000000ull).put<uint64_t>(24, 0);
    auto s = one<om::TradingSessionStatus>(ts.b);
    assert(s.get_MarketCode() == "MAIN" && s.TradingSessionSubID == 101 && s.TradingSesStatus == 20);
    assert(s.TradingSesControlFlag == '0' && s.StartDateTime == 1763425800000000000ull && s.EndDateTime == 0);
  }

  // Currency Rate examples (SPEC p.21): 1 EUR = 10.22 HKD; 1000 JPY = 90.678 HKD.
  {
    Msg eur(14, 16);
    eur.str(4, "EUR", 3).put<uint16_t>(8, 0).put<uint32_t>(12, 102200);
    auto e = one<om::CurrencyRate>(eur.b);
    assert(e.get_CurrencyCode() == "EUR" && e.CurrencyFactor == 0 && e.CurrencyRate == 102200);
    Msg jpy(14, 16);
    jpy.str(4, "JPY", 3).put<uint16_t>(8, 3).put<uint32_t>(12, 906780);
    auto j = one<om::CurrencyRate>(jpy.b);
    assert(j.CurrencyFactor == 3 && j.CurrencyRate == 906780);
  }

  // Security Definition with one underlying; CallPrice 1234567 with 5 decimals
  // is $12.34567 (DG p.11, 5.2.3.1). Total 544 + 8 (SPEC p.16-19).
  {
    Msg sd(11, 552);
    sd.put<uint32_t>(4, 12345).str(8, "MAIN", 4).str(12, "HK0000123456", 12).str(24, "WRNT", 4);
    sd.put<uint8_t>(28, 11).str(30, "01", 2).str(32, "HSI CBBC", 40).str(72, "HKD", 3);
    sd.put<uint32_t>(195, 10000).put<int32_t>(203, 250).str(207, "Y", 1).str(208, "N", 1).str(209, "Y", 1);
    sd.str(210, "Y", 1).str(211, "N", 1).str(213, "Y", 1).put<uint32_t>(215, 20260105).put<uint32_t>(219, 0);
    sd.str(223, "", 38).str(323, "Y", 1).put<int32_t>(324, 300).put<int32_t>(328, 200);
    sd.put<uint32_t>(332, 0).put<uint32_t>(395, 20300101).str(399, "R", 1).put<uint32_t>(444, 10000);
    sd.put<int32_t>(448, 25000).put<uint32_t>(456, 20270629).str(460, "C", 1).str(461, "E", 1).str(464, "N", 1);
    sd.put<int32_t>(465, 1234567).put<uint8_t>(469, 5).put<int32_t>(470, 1000).put<uint8_t>(474, 3);
    sd.put<uint32_t>(475, 10).put<uint16_t>(542, 1).put<uint32_t>(544, 700);
    auto d = om::decode(cp(sd.b), sd.b.size());
    assert(d && d.size == 552 && d.tail_len == 8);
    const auto s = std::get<om::SecurityDefinition>(d.msg);
    assert(sizeof s == 544);
    assert(s.SecurityCode == 12345 && s.get_MarketCode() == "MAIN" && s.get_ISINCode() == "HK0000123456");
    assert(s.get_InstrumentType() == "WRNT" && s.ProductType == 11 && s.get_SpreadTableCode() == "01");
    assert(s.get_SecurityShortName() == "HSI CBBC" && s.get_CurrencyCode() == "HKD" && s.LotSize == 10000);
    assert(s.PreviousClosingPrice == 250 && s.VCMFlag == 'Y' && s.ShortSellFlag == 'N' && s.CASFlag == 'Y');
    assert(s.CCASSFlag == 'Y' && s.DummySecurityFlag == 'N' && s.StampDutyFlag == 'Y');
    assert(s.ListingDate == 20260105 && s.DelistingDate == 0 && s.get_FreeText().empty());
    assert(s.POSFlag == 'Y' && s.POSUpperLimit == 300 && s.POSLowerLimit == 200 && s.DomainStmtSecurityCode == 0);
    assert(s.MaturityDate395 == 20300101 && s.InvestorType == 'R' && s.ConversionRatio == 10000);
    assert(s.StrikePrice1 == 25000 && s.MaturityDate456 == 20270629 && s.CallPutFlag == 'C' && s.Style == 'E');
    assert(s.WarrantType == 'N' && s.CallPrice == 1234567 && s.DecimalsInCallPrice == 5);
    assert(s.Entitlement == 1000 && s.DecimalsInEntitlement == 3 && s.NoWarrantsPerEntitlement == 10);
    assert(s.NoUnderlyingSecurities == 1);
    grp::UnderlyingSecurity u;
    std::memcpy(&u, d.tail, sizeof u);
    assert(u.UnderlyingSecurityCode == 700);
    double call = s.CallPrice;
    for (int i = 0; i < s.DecimalsInCallPrice; ++i)
      call /= 10;
    assert(call > 12.345669 && call < 12.345671);
  }

  // Liquidity Provider with two brokers (SPEC p.20): total 10 + 2n.
  {
    Msg lp(13, 14);
    lp.put<uint32_t>(4, 12345).put<uint16_t>(8, 2).put<uint16_t>(10, 9001).put<uint16_t>(12, 9002);
    auto d = om::decode(cp(lp.b), lp.b.size());
    const auto l = std::get<om::LiquidityProvider>(d.msg);
    assert(d && l.NoLiquidityProviders == 2 && d.tail_len == 4);
    grp::LiquidityProviderEntry e;
    std::memcpy(&e, d.tail + 2, sizeof e);
    assert(e.LPBrokerNumber == 9002);
  }

  // Broker Queue example, ask side (SPEC p.27-28).
  {
    const std::vector<std::pair<uint16_t, char>> items{{2137, 'B'}, {4138, 'B'}, {1, 'S'}, {2141, 'B'}, {5123, 'B'},
                                                       {2, 'S'}, {0, 'S'}, {3, 'S'}, {3145, 'B'}};
    Msg bq(54, 12 + 4 * items.size());
    bq.put<uint32_t>(4, 1234).put<uint8_t>(8, uint8_t(items.size())).put<uint16_t>(9, 2).str(11, "N", 1);
    for (size_t i = 0; i < items.size(); ++i)
      bq.put<uint16_t>(12 + 4 * i, items[i].first).str(14 + 4 * i, std::string(1, items[i].second), 1);
    auto d = om::decode(cp(bq.b), bq.b.size());
    const auto q = std::get<om::BrokerQueue>(d.msg);
    assert(d && d.size == 48 && q.ItemCount == 9 && q.Side == 2 && q.BQMoreFlag == 'N');
    assert(d.tail_len == q.ItemCount * sizeof(grp::BrokerQueueItem));
    for (size_t i = 0; i < items.size(); ++i)
    {
      grp::BrokerQueueItem it;
      std::memcpy(&it, d.tail + 4 * i, sizeof it);
      assert(it.Item == items[i].first && it.Type == items[i].second);
    }
  }

  // Aggregate order book examples 1-6 (SPEC p.50-57), applied in sequence.
  {
    side_t bid{{9730, 700}, {9720, 350}, {9710, 150}, {9700, 250}, {9690, 100},
               {9680, 150}, {9670, 50},  {9660, 200}, {9650, 100}};
    side_t ask{{9760, 500}, {9770, 300}, {9780, 100}, {9790, 150}};
    // Example 1: MsgSize 60; ask change at level 2, new at level 5.
    auto e1 = aob(1234, {{200, 9770, 1, 2, 1}, {300, 9850, 1, 5, 0}});
    assert(e1.size() == 60);
    apply(e1, bid, ask);
    assert((ask == side_t{{9760, 500}, {9770, 200}, {9780, 100}, {9790, 150}, {9850, 300}}));
    // Example 2: MsgSize 36; bid new at level 1 shifts the others down.
    auto e2 = aob(1234, {{50, 9740, 0, 1, 0}});
    assert(e2.size() == 36);
    apply(e2, bid, ask);
    assert(bid.size() == 10 && bid.front() == (Level{9740, 50}) && bid.back() == (Level{9650, 100}));
    // Example 3: new at 1 pushes 9650 out (implicit deletion); change of 9660 at its new level 10.
    apply(aob(1234, {{250, 9750, 0, 1, 0}, {150, 9660, 0, 10, 1}}), bid, ask);
    assert((bid == side_t{{9750, 250}, {9740, 50}, {9730, 700}, {9720, 350}, {9710, 150},
                          {9700, 250}, {9690, 100}, {9680, 150}, {9670, 50}, {9660, 150}}));
    // Example 4: delete at 1, server re-adds 9650 at 10 (explicit addition). The
    // specification's resulting table shows 9660 at 200, not the 150 left by
    // example 3 (p.54); the entries give 150.
    apply(aob(1234, {{250, 9750, 0, 1, 2}, {100, 9650, 0, 10, 0}}), bid, ask);
    assert((bid == side_t{{9740, 50}, {9730, 700}, {9720, 350}, {9710, 150}, {9700, 250},
                          {9690, 100}, {9680, 150}, {9670, 50}, {9660, 150}, {9650, 100}}));
    // Example 5: ask new at 1; 9850 (now level 6, tick 11) explicitly deleted.
    apply(aob(1234, {{300, 9750, 1, 1, 0}, {300, 9850, 1, 6, 2}}), bid, ask);
    assert((ask == side_t{{9750, 300}, {9760, 500}, {9770, 200}, {9780, 100}, {9790, 150}}));
    // Explicit versus implicit deletions (p.55-57): MsgSize 180, seven entries;
    // 9710 falls to level 11 and is deleted by the client; four explicit
    // deletions each at level 7.
    side_t b2{{9800, 700}, {9790, 350}, {9780, 150}, {9760, 250}, {9750, 100}, {9730, 400}, {9720, 200}, {9710, 300}};
    side_t a2;
    auto e5 = aob(1234, {{450, 9860, 0, 1, 0}, {550, 9850, 0, 2, 0}, {650, 9840, 0, 3, 0}, {250, 9760, 0, 7, 2},
                         {100, 9750, 0, 7, 2}, {400, 9730, 0, 7, 2}, {200, 9720, 0, 7, 2}});
    assert(e5.size() == 180);
    apply(e5, b2, a2);
    assert((b2 == side_t{{9860, 450}, {9850, 550}, {9840, 650}, {9800, 700}, {9790, 350}, {9780, 150}}));
    // Example 6: Orderbook Clear, MsgSize 36, all fields 0 but UpdateAction 74.
    auto e6 = aob(1234, {{0, 0, 0, 0, 74}});
    assert(e6.size() == 36);
    apply(e6, bid, ask);
    assert(bid.empty() && ask.empty());
  }

  // Other fixed-size messages (SPEC p.28-42).
  {
    Msg oi(56, 20);
    oi.put<uint32_t>(4, 5).str(8, "B", 1).put<uint64_t>(10, 12000);
    auto o = one<om::OrderImbalance>(oi.b);
    assert(o.OrderImbalanceDirection == 'B' && o.OrderImbalanceQuantity == 12000);
    Msg iep(41, 20);
    iep.put<uint32_t>(4, 5).put<int32_t>(8, 99500).put<uint64_t>(12, 300000);
    auto i = one<om::IndicativeEquilibriumPrice>(iep.b);
    assert(i.Price == 99500 && i.AggregateQuantity == 300000);
    Msg rp(43, 20);
    rp.put<uint32_t>(4, 5).put<int32_t>(8, 100000).put<int32_t>(12, 95000).put<int32_t>(16, 105000);
    auto r = one<om::ReferencePrice>(rp.b);
    assert(r.ReferencePrice == 100000 && r.LowerPrice == 95000 && r.UpperPrice == 105000);
    Msg vc(23, 36);
    vc.put<uint32_t>(4, 5).put<uint64_t>(8, 1).put<uint64_t>(16, 2).put<int32_t>(24, 100000);
    vc.put<int32_t>(28, 90000).put<int32_t>(32, 110000);
    auto v = one<om::VCMTrigger>(vc.b);
    assert(v.CoolingOffEndTime == 2 && v.VCMLowerPrice == 90000 && v.VCMUpperPrice == 110000);
    Msg st(60, 52);
    st.put<uint32_t>(4, 5).put<uint64_t>(8, 1000).put<int64_t>(16, -1).put<int32_t>(36, 7).put<int64_t>(44, 99);
    auto s = one<om::Statistics>(st.b);
    assert(s.SharesTraded == 1000 && s.Turnover == -1 && s.VWAP == 7 && s.ShortSellTurnover == 99);
    Msg tt(52, 36);
    tt.put<uint32_t>(4, 5).put<uint32_t>(8, 3).put<uint64_t>(16, 800).put<int16_t>(32, 0).str(34, "Y", 1);
    auto k = one<om::TradeTicker>(tt.b);
    assert(k.TickerID == 3 && k.AggregateQuantity == 800 && k.TrdCancelFlag == 'Y');
    Msg ix(71, 112);
    ix.str(4, "0000100", 11).str(15, "T", 1).put<int64_t>(40, INT64_MIN).put<int32_t>(104, -12345).str(108, " ", 1);
    auto x = one<om::IndexData>(ix.b);
    assert(x.get_IndexCode() == "0000100" && x.HighValue == INT64_MIN && x.NetChgPrevDayPct == -12345);
    Msg sc(81, 32);
    sc.str(4, "SH", 2).str(6, "SB", 2).put<int64_t>(24, 5000000);
    assert(one<om::StockConnectMarketTurnover>(sc.b).BuySellTurnover == 5000000);
  }

  // News: fixed part, then market codes, security codes and lines (SPEC p.37-38).
  {
    Msg n(22, 356 + 4 * 1 + 4 * 2 + 160 * 1);
    n.str(4, "EXN", 3).str(7, "001", 3).str(10, "HEADLINE", 320).str(330, "N", 1).str(331, "Y", 1);
    n.put<uint64_t>(336, 1763425800000000000ull).put<uint16_t>(346, 1).str(348, "MAIN", 4);
    n.put<uint16_t>(354, 2).put<uint32_t>(356, 5).put<uint32_t>(360, 700).put<uint16_t>(366, 1);
    n.str(368, "LINE ONE", 160);
    auto d = om::decode(cp(n.b), n.b.size());
    const auto h = std::get<om::News>(d.msg);
    assert(d && d.size == 528 && h.get_NewsType() == "EXN" && h.LastFragment == 'Y' && h.NoMarketCodes == 1);
    const char *p = d.tail;
    grp::NewsMarketCode mc;
    std::memcpy(&mc, p, sizeof mc);
    p += sizeof mc;
    grp::NewsSecurityCodesHeader sh;
    std::memcpy(&sh, p, sizeof sh);
    p += sizeof sh;
    assert(get_str(mc.MarketCode) == "MAIN" && sh.NoSecurityCodes == 2);
    grp::NewsSecurityCode c2;
    std::memcpy(&c2, p + 4, sizeof c2);
    p += 4 * sh.NoSecurityCodes;
    grp::NewsLinesHeader lh;
    std::memcpy(&lh, p, sizeof lh);
    p += sizeof lh;
    assert(c2.SecurityCode == 700 && lh.NoNewsLines == 1);
    assert(std::string_view(p, 8) == "LINE ONE" && p + 160 == d.tail + d.tail_len);
  }

  // Framing errors: unknown type skipped by MsgSize; MsgSize shorter than the
  // message is malformed; a longer one is accepted; a short buffer is incomplete.
  {
    Msg u(999, 10);
    auto d = om::decode(cp(u.b), u.b.size());
    assert(d.st == status::unknown && d.size == 10);
    Msg a(30, 32);
    a.put<uint16_t>(0, 20);
    auto m = om::decode(cp(a.b), a.b.size());
    assert(m.st == status::malformed && m.size == 20);
    Msg l(31, 32); // 4 bytes beyond the 28-byte Modify Order
    auto g = om::decode(cp(l.b), l.b.size());
    assert(g && g.size == 32 && std::holds_alternative<om::ModifyOrder>(g.msg));
    assert(om::decode(cp(l.b), 20).st == status::incomplete);
  }

  // Retransmission session: Logon and the DG request example (channel 1,
  // messages 107-108, DG p.14-15), each after a 16-byte header, PktSize 32.
  {
    char buf[64];
    rts::to_hkex::PacketHeader h{};
    h.PktSize = 32;
    h.MsgCount = 1;
    h.Filler = ' ';
    size_t n = rts::to_hkex::encode(buf, sizeof buf, h);
    rts::to_hkex::Logon lo{};
    lo.set_Username("USER01");
    n += rts::to_hkex::encode(buf + n, sizeof buf - n, lo);
    assert(n == 32 && buf[0] == 32 && buf[1] == 0 && buf[2] == 1);
    assert(buf[16] == 16 && buf[17] == 0 && buf[18] == 101 && buf[19] == 0);
    assert(std::string_view(buf + 20, 6) == "USER01" && buf[26] == 0 && buf[31] == 0);
    rts::to_hkex::RetransmissionRequest rq{};
    rq.ChannelID = 1;
    rq.BeginSeqNum = 107;
    rq.EndSeqNum = 108;
    n = rts::to_hkex::encode(buf, sizeof buf, h);
    n += rts::to_hkex::encode(buf + n, sizeof buf - n, rq);
    const unsigned char want[16] = {16, 0, 201, 0, 1, 0, 0, 0, 107, 0, 0, 0, 108, 0, 0, 0};
    assert(n == 32 && std::memcmp(buf + 16, want, 16) == 0);

    Msg lr(102, 8);
    lr.put<uint8_t>(4, 0);
    auto r = rts::from_hkex::decode(cp(lr.b), lr.b.size());
    assert(r && std::get<rts::from_hkex::LogonResponse>(r.msg).SessionStatus == 0);
    Msg rr(202, 16);
    rr.put<uint16_t>(4, 1).put<uint8_t>(6, 100).put<uint32_t>(8, 107).put<uint32_t>(12, 108);
    auto s = rts::from_hkex::decode(cp(rr.b), rr.b.size());
    const auto rs = std::get<rts::from_hkex::RetransmissionResponse>(s.msg);
    assert(s && rs.ChannelID == 1 && rs.RetransStatus == 100 && rs.BeginSeqNum == 107 && rs.EndSeqNum == 108);
    // A data message on the TCP stream is unknown to the session decoder and
    // decodes with the multicast one.
    Msg sr(100, 8);
    sr.put<uint32_t>(4, 1);
    assert(rts::from_hkex::decode(cp(sr.b), 8).st == status::unknown);
  }

  // Line arbitration by message sequence number (DG p.9-10, Diagram 2): line A
  // packs 101-103, 104-105, 106-107; line B 101-102, 103-105, 106-107. Each
  // message is processed once, in order, whatever line delivers it first.
  {
    auto pk = [](uint32_t seq, unsigned count) {
      std::vector<bytes_t> ms;
      for (unsigned i = 0; i < count; ++i)
      {
        Msg m(32, 20);
        m.put<uint32_t>(4, 1).put<uint64_t>(8, seq + i); // OrderId = sequence number, to check
        ms.push_back(m.b);
      }
      return packet(seq, ms);
    };
    const std::vector<bytes_t> arrivals{pk(101, 2), pk(101, 3), pk(104, 2), pk(103, 3), pk(106, 2), pk(106, 2)};
    uint32_t next = 101;
    std::vector<uint64_t> processed;
    for (const auto &p : arrivals)
    {
      const auto h = header(p);
      if (h.SeqNum > next || h.SeqNum + h.MsgCount <= next)
        continue; // gap (not in this example) or duplicate packet
      size_t pos = sizeof h;
      for (uint32_t i = 0; i < h.MsgCount; ++i)
      {
        auto d = om::decode(cp(p) + pos, p.size() - pos);
        assert(d);
        if (h.SeqNum + i == next)
        {
          processed.push_back(std::get<om::DeleteOrder>(d.msg).OrderId);
          ++next;
        }
        pos += d.size;
      }
    }
    assert((processed == std::vector<uint64_t>{101, 102, 103, 104, 105, 106, 107}) && next == 108);
  }

  std::cout << "hkex OK\n";
  return 0;
}
