// Decode through the header generated from pillar.tbl.
//
// Messages are built byte by byte from the field definitions of the Pillar
// Integrated Feed Client Specification v2.5h (IF) and the Pillar Equities
// Common Client Specification v2.4r (CCS); neither document has byte
// examples. Two packets are copied from NYSE's public sample capture
// 20190122.090000.100000.NYSE.IF.89_100.A.02.pcap (ftp.nyse.com, Real Time
// Data Samples/NYSE XDP/NYSE_IBF), with expected values decoded independently.
//
//   fieldgen.py global_connectivity/non_certified/market_data/nyse/pillar.tbl -o <scratch>/pillar.hpp
//   c++ -std=c++20 -Wall -Wextra -Werror -I<scratch> nyse_decode_test.cpp
#include "pillar.hpp"
#include "../../test_support.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace test_support;

// Little-endian message writer (CCS p.9, section 3: binary fields are
// little-endian, no alignment filler, ASCII left aligned and NUL padded).
using W = le_writer;

// MsgSize (inclusive) and MsgType, then the body (CCS p.9, section 3.1).
static bytes msg(uint16_t type, const W &body)
{
  W w;
  w.u(body.b.size() + 4, 2).u(type, 2);
  w.b.insert(w.b.end(), body.b.begin(), body.b.end());
  return w.b;
}

template <typename M>
static M xdp1(const bytes &b, size_t size)
{
  CHECK(b.size() == size);
  return decode_as<M>(nyse::xdp::from_nyse::decode, cp(b), b.size(), size);
}

template <typename M>
static M ctl1(const bytes &b, size_t size)
{
  CHECK(b.size() == size);
  return decode_as<M>(nyse::ctl::from_nyse::decode, cp(b), b.size(), size);
}

int main()
{
  using namespace nyse;
  namespace x = nyse::xdp::from_nyse;
  namespace c = nyse::ctl::from_nyse;

  // Add Order (IF p.9): 39 bytes.
  {
    W w;
    w.u(123456789, 4).u(1234, 4).u(7, 4).u(0x0102030405060708ull, 8).u(1010000, 4).u(300, 4).c('B').s("MLCO", 5, ' ').u(0, 1);
    auto m = xdp1<x::AddOrder>(msg(100, w), 39);
    CHECK(m.MessageLength == 39 && m.MessageType == 100);
    CHECK(m.SourceTimeNS == 123456789u && m.SymbolIndex == 1234u && m.SymbolSeqNum == 7u);
    CHECK(m.OrderID == 0x0102030405060708ull && m.Price == 1010000 && m.Volume == 300u);
    CHECK(m.Side == 'B' && m.get_FirmID() == "MLCO" && m.Reserved1 == 0);
  }

  // Modify Order (IF p.10): 35 bytes; PositionChange binary 0, Side at 33.
  {
    W w;
    w.u(5, 4).u(1234, 4).u(8, 4).u(42, 8).u(1009900, 4).u(200, 4).u(0, 1).c('S').u(0, 1);
    auto m = xdp1<x::ModifyOrder>(msg(101, w), 35);
    CHECK(m.SourceTimeNS == 5u && m.SymbolIndex == 1234u && m.SymbolSeqNum == 8u && m.OrderID == 42u);
    CHECK(m.Price == 1009900 && m.Volume == 200u && m.PositionChange == 0 && m.Side == 'S' && m.Reserved2 == 0);
  }

  // Delete Order (IF p.11): 25 bytes.
  {
    W w;
    w.u(6, 4).u(1234, 4).u(9, 4).u(42, 8).u(0, 1);
    auto m = xdp1<x::DeleteOrder>(msg(102, w), 25);
    CHECK(m.SourceTimeNS == 6u && m.SymbolSeqNum == 9u && m.OrderID == 42u && m.Reserved1 == 0);
  }

  // Order Execution (IF p.12-13): 42 bytes, trade conditions at 38-41.
  {
    W w;
    w.u(7, 4).u(1234, 4).u(10, 4).u(43, 8).u(0xA1B2C3D4u, 4).u(1010100, 4).u(100, 4).u(1, 1).u(0, 1)
        .c('@').c('F').c(' ').c('I');
    auto m = xdp1<x::OrderExecution>(msg(103, w), 42);
    CHECK(m.OrderID == 43u && m.TradeID == 0xA1B2C3D4u && m.Price == 1010100 && m.Volume == 100u);
    CHECK(m.PrintableFlag == 1 && m.Reserved1 == 0);
    CHECK(m.TradeCond1 == '@' && m.TradeCond2 == 'F' && m.TradeCond3 == ' ' && m.TradeCond4 == 'I');
  }

  // Replace Order (IF p.14): 42 bytes.
  {
    W w;
    w.u(8, 4).u(1234, 4).u(11, 4).u(44, 8).u(45, 8).u(1010200, 4).u(500, 4).c('B').u(0, 1);
    auto m = xdp1<x::ReplaceOrder>(msg(104, w), 42);
    CHECK(m.OrderID == 44u && m.NewOrderID == 45u && m.Price == 1010200 && m.Volume == 500u);
    CHECK(m.Side == 'B' && m.Reserved2 == 0 && m.SymbolSeqNum == 11u);
  }

  // Imbalance (IF p.15-18): 73 bytes, including the NYSE-only trailing fields.
  {
    W w;
    w.u(1700000000, 4).u(9, 4).u(1234, 4).u(12, 4)   // SourceTime, NS, SymbolIndex, SymbolSeqNum
        .u(1010000, 4).u(5000, 4).u(1200, 4).u(0, 4)  // ReferencePrice, PairedQty, TotalImbalanceQty, MarketImbalanceQty
        .u(1600, 2).c('C').c('S')                     // AuctionTime, AuctionType, ImbalanceSide
        .u(1009000, 4).u(1011000, 4).u(1008000, 4)    // ContinuousBook, AuctionInterest clearing, SSRFilingPrice
        .u(0, 4).u(0, 4).u(0, 4)                      // IndicativeMatchPrice, UpperCollar, LowerCollar
        .u(3, 1).u(1, 1).u(0, 1)                      // AuctionStatus, FreezeStatus, NumExtensions
        .u(700, 4).c('B').c('\0');                    // UnpairedQty, UnpairedSide, Reserved
    auto m = xdp1<x::Imbalance>(msg(105, w), 73);
    CHECK(m.SourceTime == 1700000000u && m.SourceTimeNS == 9u && m.SymbolIndex == 1234u && m.SymbolSeqNum == 12u);
    CHECK(m.ReferencePrice == 1010000 && m.PairedQty == 5000u && m.TotalImbalanceQty == 1200u && m.MarketImbalanceQty == 0u);
    CHECK(m.AuctionTime == 1600 && m.AuctionType == 'C' && m.ImbalanceSide == 'S');
    CHECK(m.ContinuousBookClearingPrice == 1009000 && m.AuctionInterestClearingPrice == 1011000 && m.SSRFilingPrice == 1008000);
    CHECK(m.IndicativeMatchPrice == 0 && m.UpperCollar == 0 && m.LowerCollar == 0);
    CHECK(m.AuctionStatus == 3 && m.FreezeStatus == 1 && m.NumExtensions == 0);
    CHECK(m.UnpairedQty == 700u && m.UnpairedSide == 'B' && m.Reserved == '\0');
  }

  // Add Order Refresh (IF p.19): 43 bytes, SourceTime before SourceTimeNS.
  {
    W w;
    w.u(1700000001, 4).u(10, 4).u(1234, 4).u(13, 4).u(46, 8).u(1010000, 4).u(100, 4).c('S').s("", 5, ' ').u(0, 1);
    auto m = xdp1<x::AddOrderRefresh>(msg(106, w), 43);
    CHECK(m.SourceTime == 1700000001u && m.OrderID == 46u && m.Side == 'S' && m.get_FirmID().empty());
  }

  // Non-Displayed Trade (IF p.20-21): 33 bytes.
  {
    W w;
    w.u(11, 4).u(1234, 4).u(14, 4).u(77, 4).u(1010050, 4).u(250, 4).u(1, 1).c('@').c(' ').c('T').c(' ');
    auto m = xdp1<x::NonDisplayedTrade>(msg(110, w), 33);
    CHECK(m.TradeID == 77u && m.Price == 1010050 && m.Volume == 250u && m.PrintableFlag == 1);
    CHECK(m.TradeCond1 == '@' && m.TradeCond2 == ' ' && m.TradeCond3 == 'T' && m.TradeCond4 == ' ');
  }

  // Trade Cancel (IF p.23), Cross Trade (p.22), Stock Summary (p.26).
  {
    W w;
    w.u(12, 4).u(1234, 4).u(15, 4).u(77, 4);
    CHECK(xdp1<x::TradeCancel>(msg(112, w), 20).TradeID == 77u);
    W k;
    k.u(13, 4).u(1234, 4).u(16, 4).u(9, 4).u(1010000, 4).u(1000000, 4).c('6');
    auto ct = xdp1<x::CrossTrade>(msg(111, k), 29);
    CHECK(ct.CrossID == 9u && ct.Volume == 1000000u && ct.CrossType == '6');
    W s;
    s.u(1700000002, 4).u(0, 4).u(1234, 4).u(1020000, 4).u(1000000, 4).u(1005000, 4).u(1010000, 4).u(12345678, 4);
    auto ss = xdp1<x::StockSummary>(msg(223, s), 36);
    CHECK(ss.HighPrice == 1020000 && ss.LowPrice == 1000000 && ss.Open == 1005000 && ss.Close == 1010000 && ss.TotalVolume == 12345678u);
  }

  // Signed prices (CCS p.11, section 3.5).
  {
    W w;
    w.u(0, 4).u(1, 4).u(1, 4).u(1, 8).u(uint32_t(-5), 4).u(1, 4).c('B').s("", 5, '\0').u(0, 1);
    CHECK(xdp1<x::AddOrder>(msg(100, w), 39).Price == -5);
  }

  // Sequence Number Reset (CCS p.13): 14 bytes.
  {
    W w;
    w.u(1700000000, 4).u(500, 4).u(11, 1).u(3, 1);
    auto m = ctl1<c::SequenceNumberReset>(msg(1, w), 14);
    CHECK(m.SourceTime == 1700000000u && m.SourceTimeNS == 500u && m.ProductID == 11 && m.ChannelID == 3);
  }

  // Source Time Reference (CCS p.13): 16 bytes.
  {
    W w;
    w.u(55, 4).u(0, 4).u(1700000003, 4);
    auto m = ctl1<c::SourceTimeReference>(msg(2, w), 16);
    CHECK(m.ID == 55u && m.SymbolSeqNum == 0u && m.SourceTime == 1700000003u);
  }

  // Symbol Index Mapping (CCS p.14-15): 44 bytes.
  {
    W w;
    w.u(1234, 4).s("BRK A", 11, '\0').u(0, 1).u(1, 2).u(55, 1).c('N').u(3, 1).c('C').u(1, 2)
        .u(700000000, 4).u(4321, 4).u(0, 1).c('Y').u(1, 2).u(1, 2).u(1, 1).u(0, 1);
    auto m = ctl1<c::SymbolIndexMapping>(msg(3, w), 44);
    CHECK(m.SymbolIndex == 1234u && m.get_Symbol() == "BRK A" && m.Reserved == 0 && m.MarketID == 1);
    CHECK(m.SystemID == 55 && m.ExchangeCode == 'N' && m.PriceScaleCode == 3 && m.SecurityType == 'C');
    CHECK(m.LotSize == 1 && m.PrevClosePrice == 700000000 && m.PrevCloseVolume == 4321u && m.PriceResolution == 0);
    CHECK(m.RoundLot == 'Y' && m.MPV == 1 && m.UnitOfTrade == 1 && m.LateCloseEligible == 1 && m.ETHEligible == 0);
  }

  // Symbol Clear (CCS p.16): 20 bytes.
  {
    W w;
    w.u(1700000004, 4).u(1, 4).u(1234, 4).u(17, 4);
    auto m = ctl1<c::SymbolClear>(msg(32, w), 20);
    CHECK(m.SymbolIndex == 1234u && m.NextSourceSeqNum == 17u);
  }

  // Security Status (CCS p.16-18): 46 bytes, SSR activation.
  {
    W w;
    w.u(1700000005, 4).u(2, 4).u(1234, 4).u(18, 4).c('A').c('~').u(0, 4).u(950000, 4).u(0, 4)
        .c('N').u(1500, 4).u(103015123, 4).c('E').c('O').u(0, 1);
    auto m = ctl1<c::SecurityStatus>(msg(34, w), 46);
    CHECK(m.SymbolIndex == 1234u && m.SymbolSeqNum == 18u && m.SecurityStatus == 'A' && m.HaltCondition == '~');
    CHECK(m.Reserved == 0u && m.Price1 == 950000 && m.Price2 == 0 && m.SSRTriggeringExchangeID == 'N');
    CHECK(m.SSRTriggeringVolume == 1500u && m.Time == 103015123u && m.SSRState == 'E' && m.MarketState == 'O');
    CHECK(m.SessionState == 0);
  }

  // Message Unavailable (CCS p.25).
  {
    W w;
    w.u(100, 4).u(200, 4).u(157, 1).u(4, 1);
    auto m = ctl1<c::MessageUnavailable>(msg(31, w), 14);
    CHECK(m.BeginSeqNum == 100u && m.EndSeqNum == 200u && m.ProductID == 157 && m.ChannelID == 4);
  }

  // Refresh Header (CCS p.25-26): the full 16-byte form decodes; the 8-byte
  // form is reported malformed with size 8 and read as RefreshHeaderShort.
  {
    W w;
    w.u(1, 2).u(3, 2).u(99999, 4).u(41, 4);
    auto m = ctl1<c::RefreshHeader>(msg(35, w), 16);
    CHECK(m.CurrentRefreshPkt == 1 && m.TotalRefreshPkts == 3 && m.LastSeqNum == 99999u && m.LastSymbolSeqNum == 41u);
    W s;
    s.u(2, 2).u(3, 2);
    auto b = msg(35, s);
    auto d = c::decode(cp(b), b.size());
    CHECK(d.st == status::malformed && d.size == 8);
    pkt::from_nyse::RefreshHeaderShort r;
    static_assert(sizeof(r) == 8);
    std::memcpy(&r, b.data(), sizeof r);
    CHECK(r.MsgSize == 8 && r.MsgType == 35 && r.CurrentRefreshPkt == 2 && r.TotalRefreshPkts == 3);
  }

  // Request Response (CCS p.28), received over TCP.
  {
    W w;
    w.u(5, 4).u(1000, 4).u(1999, 4).s("CLIENT01", 10, '\0').u(11, 1).u(1, 1).c('0');
    auto b = msg(11, w);
    auto d = req::from_nyse::decode(cp(b), b.size());
    CHECK(d && d.size == 29);
    auto m = std::get<req::from_nyse::RequestResponse>(d.msg);
    CHECK(m.RequestSeqNum == 5u && m.BeginSeqNum == 1000u && m.EndSeqNum == 1999u && m.get_SourceID() == "CLIENT01");
    CHECK(m.ProductID == 11 && m.ChannelID == 1 && m.Status == '0');
  }

  // Retransmission Request (CCS p.22) in a request packet: encoded, compared
  // with the bytes the spec's table gives.
  {
    char out[64];
    pkt::to_nyse::PacketHeader h{};
    req::to_nyse::RetransmissionRequest r{};
    r.BeginSeqNum = 1000;
    r.EndSeqNum = 1999;
    r.set_SourceID("ABCDEFGHI");
    r.ProductID = 11;
    r.ChannelID = 1;
    h.PktSize = 16 + 24;
    h.DeliveryFlag = 11;
    h.NumberMsgs = 1;
    h.SeqNum = 5;
    size_t n = pkt::to_nyse::encode(out, sizeof out, h);
    CHECK(n == 16);
    size_t k = req::to_nyse::encode(out + n, sizeof out - n, r);
    CHECK(k == 24);
    W want;
    want.u(40, 2).u(11, 1).u(1, 1).u(5, 4).u(0, 4).u(0, 4)
        .u(24, 2).u(10, 2).u(1000, 4).u(1999, 4).s("ABCDEFGHI", 10, '\0').u(11, 1).u(1, 1);
    CHECK(std::memcmp(out, want.b.data(), 40) == 0);
    W hb;
    hb.u(14, 2).u(12, 2).s("ABCDEFGHIJ", 10, '\0');
    req::to_nyse::HeartbeatResponse hr{};
    hr.set_SourceID("ABCDEFGHIJ");
    CHECK(req::to_nyse::encode(out, sizeof out, hr) == 14 && std::memcmp(out, hb.b.data(), 14) == 0);
  }

  // A packet built from the spec: header, Source Time Reference, Add Order,
  // walked by MsgSize; xdp decode first, ctl for the control messages.
  {
    W body;
    W str;
    str.u(55, 4).u(0, 4).u(1700000000, 4);
    W add;
    add.u(250, 4).u(1234, 4).u(1, 4).u(99, 8).u(5000000, 4).u(100, 4).c('S').s("", 5, ' ').u(0, 1);
    auto m1 = msg(2, str), m2 = msg(100, add);
    W p;
    p.u(16 + m1.size() + m2.size(), 2).u(11, 1).u(2, 1).u(1000, 4).u(1700000000, 4).u(777, 4);
    p.b.insert(p.b.end(), m1.begin(), m1.end());
    p.b.insert(p.b.end(), m2.begin(), m2.end());
    pkt::from_nyse::PacketHeader h;
    std::memcpy(&h, p.b.data(), sizeof h);
    CHECK(h.PktSize == p.b.size() && h.DeliveryFlag == 11 && h.NumberMsgs == 2 && h.SeqNum == 1000u);
    size_t pos = sizeof h;
    uint32_t seq = h.SeqNum, secs = 0;
    int seen = 0;
    for (int i = 0; i < h.NumberMsgs; ++i, ++seq)
    {
      auto d = xdp::from_nyse::decode(cp(p.b) + pos, p.b.size() - pos);
      if (d.st == status::unknown)
      {
        auto e = ctl::from_nyse::decode(cp(p.b) + pos, p.b.size() - pos);
        CHECK(e && seq == 1000u);
        secs = std::get<c::SourceTimeReference>(e.msg).SourceTime;
        pos += e.size;
      }
      else
      {
        CHECK(d && seq == 1001u);
        auto a = std::get<x::AddOrder>(d.msg);
        CHECK(a.OrderID == 99u && a.SourceTimeNS == 250u && secs == 1700000000u);
        pos += d.size;
      }
      ++seen;
    }
    CHECK(seen == 2 && pos == p.b.size());
  }

  // Forward compatibility (CCS p.9-10, section 3.1.1): a longer message is
  // accepted and its size followed; an unknown type is skipped by MsgSize.
  {
    W w;
    w.u(6, 4).u(1234, 4).u(9, 4).u(42, 8).u(0, 1).u(0xDEADBEEF, 4);
    auto b = msg(102, w);
    auto d = xdp::from_nyse::decode(cp(b), b.size());
    CHECK(d && d.size == 29 && std::get<x::DeleteOrder>(d.msg).OrderID == 42u);
    W u;
    u.u(1, 4).u(2, 4);
    auto ub = msg(998, u);
    auto e = xdp::from_nyse::decode(cp(ub), ub.size());
    CHECK(e.st == status::unknown && e.size == 12);
    auto f = xdp::from_nyse::decode(cp(ub), 5);
    CHECK(f.st == status::incomplete);
  }

  // Real packets, NYSE sample capture 2019-01-22 (233.125.89.100:56005).
  // Before IF v2.5 (May 2022) Modify byte 33 was Reserved (0); before v2.4
  // (Q1 2022) the trade condition bytes were not trade conditions (0).
  {
    auto p = hex("47000b02980d0000fb21475c0066d60d100002003700000000000000fb21475c2700640000"
                 "01d60d10a10000080100001866000001003700a0c69d006400000042202020202000");
    pkt::from_nyse::PacketHeader h;
    std::memcpy(&h, p.data(), sizeof h);
    CHECK(h.PktSize == 71 && h.DeliveryFlag == 11 && h.NumberMsgs == 2 && h.SeqNum == 3480u);
    CHECK(h.SendTime == 1548165627u && h.SendTimeNS == 232154624u);
    auto a = c::decode(cp(p) + 16, p.size() - 16);
    CHECK(a && a.size == 16);
    auto s = msg_as<c::SourceTimeReference>(a);
    CHECK(s.ID == 55u && s.SourceTime == 1548165627u);
    auto b = xdp::from_nyse::decode(cp(p) + 32, p.size() - 32);
    CHECK(b && b.size == 39 && 32 + b.size == p.size());
    auto m = msg_as<x::AddOrder>(b);
    CHECK(m.SourceTimeNS == 232128768u && m.SymbolIndex == 41232u && m.SymbolSeqNum == 264u);
    CHECK(m.OrderID == 15481128014079512ull && m.Price == 10340000 && m.Volume == 100u);
    CHECK(m.Side == 'B' && std::string_view(m.FirmID, 5) == "     " && m.get_FirmID().empty());

    auto q = hex("5d000b02521c0000e828475c008a50332a00670000115033321800001e000000223d0000010037"
                 "00980c0000c0cc9703640000000100000000002300650000115033321800001f000000343d0000"
                 "01003700c0cc970364000000000000");
    std::memcpy(&h, q.data(), sizeof h);
    CHECK(h.PktSize == 93 && h.NumberMsgs == 2 && h.SeqNum == 7250u);
    auto e = xdp::from_nyse::decode(cp(q) + 16, q.size() - 16);
    CHECK(e && e.size == 42);
    auto ex = msg_as<x::OrderExecution>(e);
    CHECK(ex.SymbolIndex == 6194u && ex.SymbolSeqNum == 30u && ex.OrderID == 15481128014069026ull);
    CHECK(ex.TradeID == 3224u && ex.Price == 60280000 && ex.Volume == 100u && ex.PrintableFlag == 1);
    auto f = xdp::from_nyse::decode(cp(q) + 58, q.size() - 58);
    CHECK(f && f.size == 35 && 58 + f.size == q.size());
    auto md = msg_as<x::ModifyOrder>(f);
    CHECK(md.SymbolSeqNum == 31u && md.OrderID == 15481128014069044ull && md.Price == 60280000 && md.Volume == 100u);
    CHECK(md.PositionChange == 0 && md.Side == '\0');
  }

  std::puts("nyse_decode_test: ok");
  return 0;
}
