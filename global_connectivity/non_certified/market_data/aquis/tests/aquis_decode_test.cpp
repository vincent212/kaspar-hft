// Decode through the headers generated from aquis_md.tbl and aqse_md.tbl.
//
// Neither specification has byte examples, and no public Aquis capture was
// found. Every message is built byte by byte from the specification's field
// table (offset, width, little endian, char(n) NUL padded), independently of
// the generated structs, and every field is read back:
//
//   MD    Aquis Market Data Technical Specification v4.2 (August 2026)
//   AQSE  Aquis Stock Exchange Multicast Market Data Technical Specification
//         v1.2.3 (April 2024)
//
//   fieldgen.py aquis_md.tbl -o <out>/aquis_md.hpp
//   fieldgen.py aqse_md.tbl  -o <out>/aqse_md.hpp
//   c++ -std=c++20 -Wall -Wextra -Werror -I<out> aquis_decode_test.cpp
#include "aqse_md.hpp"
#include "aquis_md.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

using bytes_t = std::vector<unsigned char>;

// Writes fields at the offsets the specification gives (MD p.10, 3.1).
struct W
{
  bytes_t b;
  W &at(size_t off)
  {
    assert(b.size() == off && "field table offset");
    return *this;
  }
  W &u(uint64_t v, int n)
  {
    for (int i = 0; i < n; ++i)
      b.push_back((unsigned char)(v >> (8 * i)));
    return *this;
  }
  W &s(std::string_view v, size_t n)
  {
    for (size_t i = 0; i < n; ++i)
      b.push_back((unsigned char)(i < v.size() ? v[i] : '\0'));
    return *this;
  }
};

// The 6-byte header (MD p.11, 3.3.1): msgType, length including the header,
// seqNo. `len` is the size the specification's table adds up to.
static W hdr(int type, int len, uint32_t seq)
{
  W w;
  w.u(type, 1).u(len, 1).u(seq, 4);
  return w;
}

static const char *cp(const bytes_t &b) { return (const char *)b.data(); }

template <typename M, typename Decode>
static M one(Decode decode, const W &w)
{
  auto d = decode(cp(w.b), w.b.size());
  assert(d && d.size == w.b.size() && std::holds_alternative<M>(d.msg));
  assert(std::get<M>(d.msg).length == w.b.size());
  return std::get<M>(d.msg);
}

static int checks = 0;
#define CHECK(x) (assert(x), ++checks)

static void aquis_md()
{
  namespace m = aquis::md::from_aquis;
  auto dec = [](const char *b, size_t n) { return m::decode(b, n); };
  const uint64_t ts = 1763456400123456789ull, pub = ts + 1500;

  {
    auto x = one<m::Heartbeat>(dec, hdr(1, 6, 1001));
    CHECK(x.seqNo == 1001);
  }
  {
    auto x = one<m::MDBundleStart>(dec, hdr(34, 6, 7));
    CHECK(x.seqNo == 7);
    auto y = one<m::MDBundleEnd>(dec, hdr(35, 6, 9));
    CHECK(y.seqNo == 9);
  }
  {
    // Order Add, MD p.12, 3.4.3: 42 bytes.
    W w = hdr(2, 42, 123456);
    w.at(6).u(9003, 2).at(8).u(2, 1).at(9).u(1500, 4).at(13).u(1462500, 8).at(21).u(0xDEADBEEF, 4)
        .at(25).u(ts, 8).at(33).u(1, 1).at(34).u(pub, 8);
    auto x = one<m::OrderAdd>(dec, w);
    CHECK(x.seqNo == 123456 && x.securityID == 9003 && x.side == 2 && x.quantity == 1500);
    CHECK(x.price == 1462500 && x.orderRef == 0xDEADBEEF && x.timestamp == ts && x.mdFlags == 1 &&
          x.pubTimestamp == pub);
  }
  {
    // Order Cancel, MD p.12, 3.4.4: 29 bytes.
    W w = hdr(3, 29, 5);
    w.at(6).u(77, 2).at(8).u(424242, 4).at(12).u(ts, 8).at(20).u(0, 1).at(21).u(pub, 8);
    auto x = one<m::OrderCancel>(dec, w);
    CHECK(x.securityID == 77 && x.orderRef == 424242 && x.timestamp == ts && x.mdFlags == 0 &&
          x.pubTimestamp == pub);
  }
  {
    // Order Modify, MD p.12-13, 3.4.5: 41 bytes.
    W w = hdr(4, 41, 6);
    w.at(6).u(77, 2).at(8).u(300, 4).at(12).u(10000000, 8).at(20).u(424242, 4).at(24).u(ts, 8)
        .at(32).u(1, 1).at(33).u(pub, 8);
    auto x = one<m::OrderModify>(dec, w);
    CHECK(x.securityID == 77 && x.quantity == 300 && x.price == 10000000 && x.orderRef == 424242 &&
          x.timestamp == ts && x.mdFlags == 1 && x.pubTimestamp == pub);
  }
  {
    // Trade, MD p.13, 3.4.6: 54 bytes, Segment MIC at 37.
    W w = hdr(5, 54, 8);
    w.at(6).u(340, 2).at(8).u(1, 1).at(9).u(25, 4).at(13).u(297500000, 8).at(21).u(99, 4)
        .at(25).u(555, 4).at(29).u(ts, 8).at(37).s("AQXE", 4).at(41).u(0x0ABCDEF1, 4).at(45).u(1, 1)
        .at(46).u(pub, 8);
    auto x = one<m::Trade>(dec, w);
    CHECK(x.securityID == 340 && x.tradeType == 1 && x.quantity == 25 && x.price == 297500000);
    CHECK(x.orderRef == 99 && x.tradeRef == 555 && x.timestamp == ts && x.get_MIC() == "AQXE");
    CHECK(x.BinaryMMT == 0x0ABCDEF1 && x.mdFlags == 1 && x.pubTimestamp == pub);
  }
  {
    // Trade Bust, MD p.14, 3.4.7: 44 bytes, no mdFlags.
    W w = hdr(6, 44, 10);
    w.at(6).u(340, 2).at(8).u(25, 4).at(12).u(297500000, 8).at(20).u(555, 4).at(24).u(ts, 8)
        .at(32).u(7, 4).at(36).u(pub, 8);
    auto x = one<m::TradeBust>(dec, w);
    CHECK(x.securityID == 340 && x.quantity == 25 && x.price == 297500000 && x.tradeRef == 555 &&
          x.timestamp == ts && x.BinaryMMT == 7 && x.pubTimestamp == pub);
  }
  {
    // Tick Table Data, MD p.15, 3.4.8: 33 bytes.
    W w = hdr(7, 33, 11);
    w.at(6).u(4, 1).at(7).s("lse1", 10).at(17).u(500000, 8).at(25).u(250, 8);
    auto x = one<m::TickTableData>(dec, w);
    CHECK(x.tickTableID == 4 && x.get_name() == "lse1" && x.threshold == 500000 && x.tickSize == 250);
  }
  {
    // Security Definition, MD p.15-16, 3.4.9: 75 bytes.
    W w = hdr(8, 75, 12);
    w.at(6).u(340, 2).at(8).s("1U1d", 6).at(14).s("DE0005545503", 12).at(26).s("EUR", 3)
        .at(29).s("XETR", 4).at(33).u(9, 1).at(34).u(0x0091, 2).at(36).s("1U1", 20)
        .at(56).u(1000000000, 8).at(64).u(9, 1).at(65).u(ts, 8).at(73).u(3, 2);
    auto x = one<m::SecurityDefinition>(dec, w);
    CHECK(x.securityID == 340 && x.get_UMTF() == "1U1d" && x.get_ISIN() == "DE0005545503");
    CHECK(x.get_currency() == "EUR" && x.get_MIC() == "XETR" && x.tickTableId == 9 && x.flags == 0x91);
    CHECK(x.get_exchangeSymbol() == "1U1" && x.lotSize == 1000000000 && x.lotSizeDecimal == 9 &&
          x.timestamp == ts && x.marketId == 3);
  }
  {
    // Security Status, MD p.16-17, 3.4.10: 27 bytes.
    W w = hdr(9, 27, 13);
    w.at(6).u(340, 2).at(8).u(2, 1).at(9).u(0x61, 1).at(10).u(ts, 8).at(18).u(1, 1).at(19).u(pub, 8);
    auto x = one<m::SecurityStatus>(dec, w);
    CHECK(x.securityID == 340 && x.tradingStatus == 2 && x.marketFlags == 0x61 && x.timestamp == ts &&
          x.tradingPhase == 1 && x.pubTimestamp == pub);
  }
  {
    // AoD Update, MD p.17-18, 3.5.1: 36 bytes.
    W w = hdr(17, 36, 14);
    w.at(6).u(340, 2).at(8).u(1462500, 8).at(16).u(800, 4).at(20).u(ts, 8).at(28).u(pub, 8);
    auto x = one<m::AoDUpdate>(dec, w);
    CHECK(x.securityID == 340 && x.indicativePrice == 1462500 && x.matchVol == 800 &&
          x.timestamp == ts && x.pubTimestamp == pub);
  }
  {
    // MaC Update, MD p.18, 3.6.1: 32 bytes.
    W w = hdr(16, 32, 15);
    w.at(6).u(340, 2).at(8).u(1462500, 8).at(16).u(1000, 4).at(20).u(700, 4).at(24).u(ts, 8);
    auto x = one<m::MaCUpdate>(dec, w);
    CHECK(x.securityID == 340 && x.indicativePrice == 1462500 && x.closingBuyQty == 1000 &&
          x.closingSellQty == 700 && x.timestamp == ts);
  }
  {
    // A message longer than the table (a later version appending a field) is
    // decoded from its fixed part; the packet reader steps by `length`.
    W w = hdr(3, 33, 5);
    w.at(6).u(77, 2).at(8).u(1, 4).at(12).u(ts, 8).at(20).u(0, 1).at(21).u(pub, 8).u(0xFFFFFFFF, 4);
    auto d = m::decode(cp(w.b), w.b.size());
    CHECK(d && d.size == 29 && std::get<m::OrderCancel>(d.msg).length == 33);
    // A truncated message is incomplete; an unknown type is unknown.
    CHECK(m::decode(cp(w.b), 28).st == aquis::status::incomplete);
    bytes_t u = hdr(99, 6, 1).b;
    CHECK(m::decode(cp(u), u.size()).st == aquis::status::unknown);
  }
}

static void aquis_snap_replay()
{
  namespace s = aquis::snap::from_aquis;
  auto dec = [](const char *b, size_t n) { return s::decode(b, n); };
  const uint64_t ts = 1763456400000000000ull;
  {
    W w = hdr(10, 20, 0);
    w.at(6).u(987654, 4).at(10).u(2, 2).at(12).u(ts, 8);
    auto x = one<s::SnapshotStart>(dec, w);
    CHECK(x.streamSeqNo == 987654 && x.securityCount == 2 && x.timestamp == ts);
  }
  {
    W w = hdr(11, 28, 0);
    w.at(6).u(340, 2).at(8).u(1, 1).at(9).u(1, 1).at(10).u(3, 2).at(12).u(0, 4).at(16).u(0, 4)
        .at(20).u(0, 8);
    auto x = one<s::BookStatus>(dec, w);
    CHECK(x.securityID == 340 && x.tradingStatus == 1 && x.marketFlags == 1 && x.entries == 3 &&
          x.closingBuyQty == 0 && x.closingSellQty == 0 && x.indicativePrice == 0);
  }
  {
    W w = hdr(12, 25, 0);
    w.at(6).u(340, 2).at(8).u(1, 1).at(9).u(100, 4).at(13).u(1462500, 8).at(21).u(31, 4);
    auto x = one<s::BookEntry>(dec, w);
    CHECK(x.securityID == 340 && x.Side == 1 && x.quantity == 100 && x.Price == 1462500 && x.orderRef == 31);
  }
  {
    W w = hdr(18, 25, 0);
    w.at(6).u(340, 2).at(8).u(2, 1).at(9).u(50, 4).at(13).u(1462600, 8).at(21).u(32, 4);
    auto x = one<s::MaCBookEntry>(dec, w);
    CHECK(x.securityID == 340 && x.Side == 2 && x.Quantity == 50 && x.Price == 1462600 && x.orderRef == 32);
  }

  // Replay requests, MD p.20-21, 3.8.1-3.8.2: encoded and compared with the
  // bytes the table gives.
  namespace t = aquis::replay::to_aquis;
  {
    t::Login l{};
    l.length = 26;
    l.seqNo = 0;
    l.set_username("user1");
    l.set_password("pw");
    char buf[64];
    size_t n = t::encode(buf, sizeof(buf), l);
    W w = hdr(13, 26, 0);
    w.at(6).s("user1", 10).at(16).s("pw", 10);
    CHECK(n == 26 && std::memcmp(buf, w.b.data(), n) == 0);
  }
  {
    t::ReplayRequest r{};
    r.length = 14;
    r.seqNo = 0;
    r.beginSeqNo = 100;
    r.endSeqNo = 199;
    char buf[32];
    size_t n = t::encode(buf, sizeof(buf), r);
    W w = hdr(14, 14, 0);
    w.at(6).u(100, 4).at(10).u(199, 4);
    CHECK(n == 14 && std::memcmp(buf, w.b.data(), n) == 0);
  }
  // The replay stream carries the response and the replayed messages.
  namespace r = aquis::replay::from_aquis;
  {
    W w = hdr(15, 7, 0);
    w.at(6).u(2, 1);
    auto x = one<r::ReplayResponse>([](const char *b, size_t n) { return r::decode(b, n); }, w);
    CHECK(x.responseCode == 2);
    W a = hdr(3, 29, 55);
    a.at(6).u(1, 2).at(8).u(2, 4).at(12).u(3, 8).at(20).u(0, 1).at(21).u(4, 8);
    auto d = r::decode(cp(a.b), a.b.size());
    CHECK(d && std::holds_alternative<aquis::md::from_aquis::OrderCancel>(d.msg));
  }
}

static void aquis_packet()
{
  // A packet: 1-byte message count, then the messages (MD p.10, 3.2), walked
  // by each header's length.
  namespace m = aquis::md::from_aquis;
  bytes_t p{3};
  for (auto &w : {hdr(34, 6, 100), [] {
                    W x = hdr(3, 29, 101);
                    x.at(6).u(1, 2).at(8).u(2, 4).at(12).u(3, 8).at(20).u(0, 1).at(21).u(4, 8);
                    return x;
                  }(),
                  hdr(35, 6, 102)})
    p.insert(p.end(), w.b.begin(), w.b.end());
  auto ph = aquis::pkt::from_aquis::PacketHeader{};
  std::memcpy(&ph, p.data(), sizeof(ph));
  CHECK(ph.messageCount == 3);
  size_t off = 1;
  uint32_t seq = 100;
  for (int i = 0; i < ph.messageCount; ++i)
  {
    const uint8_t len = p[off + 1];
    auto d = m::decode(cp(p) + off, p.size() - off);
    CHECK(d);
    std::visit([&](const auto &x) { CHECK(x.seqNo == seq++); }, d.msg);
    off += len;
  }
  CHECK(off == p.size());
}

// AQSE v1.2.3: every message whose layout differs from MD, and the AQSE-only
// messages (quotes, trade reports, statistics, trader definition).
static void aqse_md()
{
  namespace m = aqse::md::from_aqse;
  auto dec = [](const char *b, size_t n) { return m::decode(b, n); };
  const uint64_t ts = 1763456400123456000ull, tx = ts - 5000;
  {
    // Order Add, AQSE p.10, 3.4.1: 33 bytes, no mdFlags.
    W w = hdr(2, 33, 1);
    w.at(6).u(12, 2).at(8).u(1, 1).at(9).u(1000, 4).at(13).u(2500000, 8).at(21).u(7, 4).at(25).u(ts, 8);
    auto x = one<m::OrderAdd>(dec, w);
    CHECK(x.securityID == 12 && x.side == 1 && x.quantity == 1000 && x.price == 2500000 &&
          x.orderRef == 7 && x.timestamp == ts);
  }
  {
    W w = hdr(3, 20, 2);
    w.at(6).u(12, 2).at(8).u(7, 4).at(12).u(ts, 8);
    auto x = one<m::OrderCancel>(dec, w);
    CHECK(x.securityID == 12 && x.orderRef == 7 && x.timestamp == ts);
  }
  {
    W w = hdr(4, 32, 3);
    w.at(6).u(12, 2).at(8).u(400, 4).at(12).u(2510000, 8).at(20).u(7, 4).at(24).u(ts, 8);
    auto x = one<m::OrderModify>(dec, w);
    CHECK(x.securityID == 12 && x.quantity == 400 && x.price == 2510000 && x.orderRef == 7 && x.timestamp == ts);
  }
  {
    // Quote Add/Replace, AQSE p.11-12, 3.4.4: traderID before securityID.
    W w = hdr(20, 42, 4);
    w.at(6).u(31, 2).at(8).u(12, 2).at(10).u(5000, 4).at(14).u(2490000, 8).at(22).u(6000, 4)
        .at(26).u(2520000, 8).at(34).u(ts, 8);
    auto x = one<m::QuoteAddReplace>(dec, w);
    CHECK(x.traderID == 31 && x.securityID == 12 && x.bidQuantity == 5000 && x.bidPrice == 2490000 &&
          x.offerQuantity == 6000 && x.offerPrice == 2520000 && x.timestamp == ts);
  }
  {
    W w = hdr(21, 18, 5);
    w.at(6).u(31, 2).at(8).u(12, 2).at(10).u(ts, 8);
    auto x = one<m::QuoteCancel>(dec, w);
    CHECK(x.traderID == 31 && x.securityID == 12 && x.timestamp == ts);
  }
  {
    // Trade, AQSE p.12, 3.4.6: 41 bytes, MMT at 37, no Segment MIC.
    W w = hdr(5, 41, 6);
    w.at(6).u(12, 2).at(8).u(1, 1).at(9).u(200, 4).at(13).u(2500000, 8).at(21).u(7, 4).at(25).u(88, 4)
        .at(29).u(ts, 8).at(37).u(0x1234, 4);
    auto x = one<m::Trade>(dec, w);
    CHECK(x.securityID == 12 && x.tradeType == 1 && x.quantity == 200 && x.price == 2500000 &&
          x.orderRef == 7 && x.tradeRef == 88 && x.timestamp == ts && x.BinaryMMT == 0x1234);
  }
  {
    W w = hdr(25, 45, 7);
    w.at(6).u(12, 2).at(8).u(8, 1).at(9).u(10000, 4).at(13).u(2400000, 8).at(21).u(901, 4)
        .at(25).u(ts, 8).at(33).u(0x55, 4).at(37).u(tx, 8);
    auto x = one<m::TradeReport>(dec, w);
    CHECK(x.securityID == 12 && x.tradeType == 8 && x.quantity == 10000 && x.price == 2400000 &&
          x.tradeRef == 901 && x.timestamp == ts && x.BinaryMMT == 0x55 && x.transactTime == tx);
  }
  {
    W w = hdr(26, 57, 8);
    w.at(6).u(12, 2).at(8).u(8, 1).at(9).u(9000, 4).at(13).u(2400000, 8).at(21).u(902, 4)
        .at(25).u(ts, 8).at(33).u(0x55, 4).at(37).u(tx, 8).at(45).u(901, 4).at(49).u(ts - 1, 8);
    auto x = one<m::TradeReportModify>(dec, w);
    CHECK(x.quantity == 9000 && x.tradeRef == 902 && x.transactTime == tx && x.origTradeRef == 901 &&
          x.origTimestamp == ts - 1);
  }
  {
    W w = hdr(27, 65, 9);
    w.at(6).u(12, 2).at(8).u(8, 1).at(9).u(10000, 4).at(13).u(2400000, 8).at(21).u(903, 4)
        .at(25).u(ts, 8).at(33).u(0x55, 4).at(37).u(tx, 8).at(45).u(901, 4).at(49).u(ts - 1, 8)
        .at(57).u(tx - 1, 8);
    auto x = one<m::TradeReportCancel>(dec, w);
    CHECK(x.origSecurityID == 12 && x.origTradeType == 8 && x.origQuantity == 10000 &&
          x.origPrice == 2400000 && x.tradeRef == 903 && x.origTradeRef == 901 &&
          x.origTransactTime == tx - 1);
  }
  {
    W w = hdr(6, 36, 10);
    w.at(6).u(12, 2).at(8).u(200, 4).at(12).u(2500000, 8).at(20).u(88, 4).at(24).u(ts, 8).at(32).u(3, 4);
    auto x = one<m::TradeBust>(dec, w);
    CHECK(x.securityID == 12 && x.quantity == 200 && x.tradeRef == 88 && x.BinaryMMT == 3);
  }
  {
    W w = hdr(22, 25, 11);
    w.at(6).u(12, 2).at(8).u(2500000, 8).at(16).u(5, 1).at(17).u(ts, 8);
    auto x = one<m::SecurityStatistics>(dec, w);
    CHECK(x.securityID == 12 && x.price == 2500000 && x.priceType == 5 && x.timestamp == ts);
  }
  {
    W w = hdr(23, 24, 12);
    w.at(6).u(31, 2).at(8).s("MMKR01", 16);
    auto x = one<m::TraderDefinition>(dec, w);
    CHECK(x.traderID == 31 && x.get_senderID() == "MMKR01");
  }
  {
    W w = hdr(7, 33, 13);
    w.at(6).u(2, 1).at(7).s("table2", 10).at(17).u(0, 8).at(25).u(100, 8);
    auto x = one<m::TickTableData>(dec, w);
    CHECK(x.tickTableID == 2 && x.get_name() == "table2" && x.tickSize == 100);
  }
  {
    // Security Definition, AQSE p.16, 3.4.14: 34 bytes, ends at tickTableId.
    W w = hdr(8, 34, 14);
    w.at(6).u(12, 2).at(8).s("", 6).at(14).s("GB00BNBS4S95", 12).at(26).s("GBX", 3).at(29).s("AQSE", 4)
        .at(33).u(2, 1);
    auto x = one<m::SecurityDefinition>(dec, w);
    CHECK(x.securityID == 12 && x.get_UMTF() == "" && x.get_ISIN() == "GB00BNBS4S95" &&
          x.get_currency() == "GBX" && x.get_MIC() == "AQSE" && x.tickTableId == 2);
  }
  {
    W w = hdr(9, 18, 15);
    w.at(6).u(12, 2).at(8).u(1, 1).at(9).u(0x09, 1).at(10).u(ts, 8);
    auto x = one<m::SecurityStatus>(dec, w);
    CHECK(x.securityID == 12 && x.tradingStatus == 1 && x.marketFlags == 0x09 && x.timestamp == ts);
  }
  {
    W w = hdr(17, 28, 16);
    w.at(6).u(12, 2).at(8).u(2500000, 8).at(16).u(300, 4).at(20).u(ts, 8);
    auto x = one<m::AoDUpdate>(dec, w);
    CHECK(x.securityID == 12 && x.indicativePrice == 2500000 && x.matchVol == 300 && x.timestamp == ts);
  }
  {
    auto x = one<m::Heartbeat>(dec, hdr(1, 6, 17));
    CHECK(x.seqNo == 17);
  }
  namespace s = aqse::snap::from_aqse;
  {
    W w = hdr(10, 20, 0);
    w.at(6).u(16, 4).at(10).u(1, 2).at(12).u(ts, 8);
    auto x = one<s::SnapshotStart>([](const char *b, size_t n) { return s::decode(b, n); }, w);
    CHECK(x.streamSeqNo == 16 && x.securityCount == 1);
  }
  namespace r = aqse::replay::from_aqse;
  {
    W w = hdr(15, 7, 0);
    w.at(6).u(0, 1);
    auto x = one<r::ReplayResponse>([](const char *b, size_t n) { return r::decode(b, n); }, w);
    CHECK(x.responseCode == 0);
  }
}

int main()
{
  aquis_md();
  aquis_snap_replay();
  aquis_packet();
  aqse_md();
  std::printf("aquis decode test: %d checks passed\n", checks);
}
