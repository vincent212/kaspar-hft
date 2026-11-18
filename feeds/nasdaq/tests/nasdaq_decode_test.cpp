// Decode tests for the Nasdaq field tables: TotalView-ITCH 5.0, MoldUDP64,
// SoupBinTCP 3.0 and GLIMPSE 5.0, all four headers in one translation unit.
//
// Each message is written out field by field from the specification's tables
// (offset order, big-endian integers, right-padded Alpha) and checked field by
// field after decoding.
//
//   G=.claude/skills/build-feed-handler/tools/fieldgen.py
//   for t in itch50 moldudp64 soupbintcp glimpse50; do
//     python3 $G feeds/nasdaq/$t.tbl -o $OUT/$t.hpp; done
//   c++ -std=c++20 -Wall -Wextra -Werror -I$OUT feeds/nasdaq/tests/nasdaq_decode_test.cpp
#include "glimpse50.hpp"
#include "itch50.hpp"
#include "moldudp64.hpp"
#include "soupbintcp.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
// Builds one message: the fields in table order, big-endian, text right-padded
// with spaces. size() is checked against the spec's message length.
struct Bytes
{
  std::vector<char> b;
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
  Bytes &s(std::string_view v, size_t n)
  {
    for (size_t i = 0; i < n; ++i)
      b.push_back(i < v.size() ? v[i] : ' ');
    return *this;
  }
  // Header common to every ITCH message: type, locate, tracking, timestamp.
  Bytes &hdr(char type, uint16_t locate, uint16_t tracking, uint64_t ts)
  {
    return c(type).u(locate, 2).u(tracking, 2).u(ts, 6);
  }
  size_t size() const { return b.size(); }
  // The message with the 2-byte exclusive length prefix of MoldUDP64 framing.
  std::vector<char> framed() const
  {
    Bytes f;
    f.u(b.size(), 2);
    f.b.insert(f.b.end(), b.begin(), b.end());
    return f.b;
  }
};

namespace itch = nasdaq::itch::from_nasdaq;

constexpr uint64_t TS = 0x0000123456789ABCull;   // 6-byte timestamp

// Decodes one framed ITCH message and returns it as type M.
template <class M>
M one(const Bytes &m, size_t spec_size)
{
  assert(m.size() == spec_size);
  const auto f = m.framed();
  auto d = itch::decode(f.data(), f.size());
  assert(d && d.size == spec_size + 2);
  assert(std::holds_alternative<M>(d.msg));
  const M &r = std::get<M>(d.msg);
  assert(r.MessageLength == spec_size);
  assert(uint64_t(r.Timestamp) == TS);
  return r;
}
}

int main()
{
  using nasdaq::get_str;

  // 1.1 System Event, 12 bytes.
  {
    Bytes m;
    m.hdr('S', 0, 1, TS).c('Q');
    auto r = one<itch::SystemEvent>(m, 12);
    assert(r.StockLocate == 0 && r.TrackingNumber == 1 && r.EventCode == 'Q');
  }

  // 1.2.1 Stock Directory, 39 bytes.
  {
    Bytes m;
    m.hdr('R', 7, 2, TS).s("AAPL", 8).c('Q').c('N').u(100, 4).c('N').c('C').s("Z", 2).c('P').c('N').c(' ')
        .c('1').c('N').u(3, 4).c('Y');
    auto r = one<itch::StockDirectory>(m, 39);
    assert(r.StockLocate == 7 && r.TrackingNumber == 2 && get_str(r.Stock) == "AAPL");
    assert(r.MarketCategory == 'Q' && r.FinancialStatusIndicator == 'N' && r.RoundLotSize == 100);
    assert(r.RoundLotsOnly == 'N' && r.IssueClassification == 'C' && get_str(r.IssueSubType) == "Z");
    assert(r.Authenticity == 'P' && r.ShortSaleThresholdIndicator == 'N' && r.IPOFlag == ' ');
    assert(r.LULDReferencePriceTier == '1' && r.ETPFlag == 'N' && r.ETPLeverageFactor == 3);
    assert(r.InverseIndicator == 'Y');
  }

  // 1.3.1 Add Order, 36 bytes. Price(4) 123.4500.
  {
    Bytes m;
    m.hdr('A', 7, 3, TS).u(0x0102030405060708ull, 8).c('B').u(300, 4).s("AAPL", 8).u(1234500, 4);
    auto r = one<itch::AddOrder>(m, 36);
    assert(r.StockLocate == 7 && r.OrderReferenceNumber == 0x0102030405060708ull && r.BuySellIndicator == 'B');
    assert(r.Shares == 300 && get_str(r.Stock) == "AAPL" && r.Price == 1234500);
  }

  // 1.3.2 Add Order with MPID Attribution, 40 bytes.
  {
    Bytes m;
    m.hdr('F', 7, 4, TS).u(43, 8).c('S').u(200, 4).s("AAPL", 8).u(1235000, 4).s("GSCO", 4);
    auto r = one<itch::AddOrderMPID>(m, 40);
    assert(r.OrderReferenceNumber == 43 && r.BuySellIndicator == 'S' && r.Shares == 200);
    assert(r.Price == 1235000 && get_str(r.Attribution) == "GSCO" && get_str(r.Stock) == "AAPL");
  }

  // 1.4.1 Order Executed, 31 bytes.
  {
    Bytes m;
    m.hdr('E', 7, 5, TS).u(43, 8).u(50, 4).u(0xA1B2C3D4E5F60718ull, 8);
    auto r = one<itch::OrderExecuted>(m, 31);
    assert(r.OrderReferenceNumber == 43 && r.ExecutedShares == 50 && r.MatchNumber == 0xA1B2C3D4E5F60718ull);
  }

  // 1.4.2 Order Executed With Price, 36 bytes.
  {
    Bytes m;
    m.hdr('C', 7, 6, TS).u(43, 8).u(25, 4).u(9001, 8).c('N').u(1234900, 4);
    auto r = one<itch::OrderExecutedWithPrice>(m, 36);
    assert(r.OrderReferenceNumber == 43 && r.ExecutedShares == 25 && r.MatchNumber == 9001);
    assert(r.Printable == 'N' && r.ExecutionPrice == 1234900);
  }

  // 1.4.3 Order Cancel, 23 bytes.
  {
    Bytes m;
    m.hdr('X', 7, 7, TS).u(43, 8).u(10, 4);
    auto r = one<itch::OrderCancel>(m, 23);
    assert(r.OrderReferenceNumber == 43 && r.CancelledShares == 10);
  }

  // 1.4.4 Order Delete, 19 bytes.
  {
    Bytes m;
    m.hdr('D', 7, 8, TS).u(43, 8);
    auto r = one<itch::OrderDelete>(m, 19);
    assert(r.StockLocate == 7 && r.OrderReferenceNumber == 43);
  }

  // 1.4.5 Order Replace, 35 bytes.
  {
    Bytes m;
    m.hdr('U', 7, 9, TS).u(0x0102030405060708ull, 8).u(44, 8).u(150, 4).u(1234600, 4);
    auto r = one<itch::OrderReplace>(m, 35);
    assert(r.OriginalOrderReferenceNumber == 0x0102030405060708ull && r.NewOrderReferenceNumber == 44);
    assert(r.Shares == 150 && r.Price == 1234600);
  }

  // 1.5.1 Trade (non-cross), 44 bytes; order reference null-filled, side always B.
  {
    Bytes m;
    m.hdr('P', 7, 10, TS).u(0, 8).c('B').u(75, 4).s("AAPL", 8).u(1234700, 4).u(9002, 8);
    auto r = one<itch::Trade>(m, 44);
    assert(r.OrderReferenceNumber == 0 && r.BuySellIndicator == 'B' && r.Shares == 75);
    assert(get_str(r.Stock) == "AAPL" && r.Price == 1234700 && r.MatchNumber == 9002);
  }

  // 1.5.2 Cross Trade, 40 bytes; Shares is 8 bytes.
  {
    Bytes m;
    m.hdr('Q', 7, 11, TS).u(5000000000ull, 8).s("AAPL", 8).u(1234800, 4).u(9003, 8).c('O');
    auto r = one<itch::CrossTrade>(m, 40);
    assert(r.Shares == 5000000000ull && get_str(r.Stock) == "AAPL" && r.CrossPrice == 1234800);
    assert(r.MatchNumber == 9003 && r.CrossType == 'O');
  }

  // 1.5.3 Broken Trade, 19 bytes.
  {
    Bytes m;
    m.hdr('B', 7, 12, TS).u(9002, 8);
    auto r = one<itch::BrokenTrade>(m, 19);
    assert(r.MatchNumber == 9002);
  }

  // 1.6 NOII, 50 bytes.
  {
    Bytes m;
    m.hdr('I', 7, 13, TS).u(100000, 8).u(2500, 8).c('S').s("AAPL", 8).u(1230000, 4).u(1233000, 4)
        .u(1234000, 4).c('C').c('L');
    auto r = one<itch::NOII>(m, 50);
    assert(r.PairedShares == 100000 && r.ImbalanceShares == 2500 && r.ImbalanceDirection == 'S');
    assert(get_str(r.Stock) == "AAPL" && r.FarPrice == 1230000 && r.NearPrice == 1233000);
    assert(r.CurrentReferencePrice == 1234000 && r.CrossType == 'C' && r.PriceVariationIndicator == 'L');
  }

  // 1.2.5.1 MWCB Decline Level, 35 bytes; Price(8).
  {
    Bytes m;
    m.hdr('V', 0, 14, TS).u(400000000000ull, 8).u(370000000000ull, 8).u(340000000000ull, 8);
    auto r = one<itch::MWCBDeclineLevel>(m, 35);
    assert(r.Level1 == 400000000000ull && r.Level2 == 370000000000ull && r.Level3 == 340000000000ull);
  }

  // A MoldUDP64 packet with two messages, walked by size.
  {
    namespace mold = nasdaq::mold::from_nasdaq;
    Bytes add, del;
    add.hdr('A', 7, 0, TS).u(45, 8).c('S').u(100, 4).s("MSFT", 8).u(4000000, 4);
    del.hdr('D', 7, 0, TS).u(45, 8);
    Bytes pkt;
    pkt.s("000001234A", 10).u(1000, 8).u(2, 2);
    for (auto &m : {add, del})
    {
      auto f = m.framed();
      pkt.b.insert(pkt.b.end(), f.begin(), f.end());
    }
    assert(pkt.size() == 20 + 38 + 21);

    mold::DownstreamPacketHeader h;
    static_assert(sizeof h == 20);
    std::memcpy(&h, pkt.b.data(), sizeof h);
    assert(get_str(h.Session) == "000001234A" && h.SequenceNumber == 1000 && h.MessageCount == 2);

    const char *p = pkt.b.data() + sizeof h;
    size_t left = pkt.size() - sizeof h;
    uint64_t seq = h.SequenceNumber;
    std::vector<uint64_t> seqs;
    std::vector<char> types;
    for (unsigned i = 0; i < h.MessageCount; ++i)
    {
      auto d = itch::decode(p, left);
      assert(d);
      types.push_back(p[2]);
      seqs.push_back(seq++);
      p += d.size;
      left -= d.size;
    }
    assert(left == 0 && types == std::vector<char>({'A', 'D'}) && seqs == std::vector<uint64_t>({1000, 1001}));

    // A heartbeat (count 0) and the end-of-session marker (0xFFFF) are headers only.
    Bytes hb;
    hb.s("000001234A", 10).u(1002, 8).u(0xFFFF, 2);
    std::memcpy(&h, hb.b.data(), sizeof h);
    assert(h.MessageCount == 0xFFFF && h.SequenceNumber == 1002);

    // The retransmission request encodes to the 20 bytes of the spec's table.
    nasdaq::mold::to_nasdaq::RequestPacket rq{};
    rq.set_Session("000001234A");
    rq.SequenceNumber = 1000;
    rq.RequestedMessageCount = 2;
    char out[32];
    assert(nasdaq::mold::to_nasdaq::encode(out, sizeof out, rq) == 20);
    Bytes req;
    req.s("000001234A", 10).u(1000, 8).u(2, 2);
    assert(std::vector<char>(out, out + 20) == req.b);
  }

  // A truncated message is incomplete; a message cut by its length prefix is malformed.
  {
    Bytes m;
    m.hdr('D', 7, 0, TS).u(46, 8);
    auto f = m.framed();
    assert(itch::decode(f.data(), f.size() - 1).st == nasdaq::status::incomplete);
    f[1] = 18;
    auto d = itch::decode(f.data(), f.size());
    assert(d.st == nasdaq::status::malformed && d.size == 20);
  }

  // GLIMPSE over SoupBinTCP: an Add Order and the End of Snapshot, each the payload
  // of a Sequenced Data packet, with no length prefix of their own.
  {
    namespace soup = nasdaq::soup::from_nasdaq;
    namespace gl = nasdaq::glimpse::from_nasdaq;
    Bytes add;
    add.hdr('A', 7, 0, TS).u(47, 8).c('B').u(100, 4).s("AAPL", 8).u(1234500, 4);
    Bytes snap;
    snap.c('G').s(std::string(17, ' ') + "123", 20);
    assert(add.size() == 36 && snap.size() == 21);
    Bytes stream;
    for (auto &m : {add, snap})
      stream.u(m.size() + 1, 2).c('S').b.insert(stream.b.end(), m.b.begin(), m.b.end());

    const char *p = stream.b.data();
    size_t left = stream.size();
    auto s1 = soup::decode(p, left);
    assert(s1 && std::holds_alternative<soup::SequencedData>(s1.msg) && s1.tail_len == 36);
    auto g1 = gl::decode(s1.tail, s1.tail_len);
    assert(g1 && g1.size == 36 && std::get<gl::AddOrder>(g1.msg).OrderReferenceNumber == 47);
    p += s1.size;
    left -= s1.size;
    auto s2 = soup::decode(p, left);
    assert(s2 && s2.tail_len == 21 && s2.size == left);
    auto g2 = gl::decode(s2.tail, s2.tail_len);
    assert(g2 && std::get<gl::EndOfSnapshot>(g2.msg).get_SequenceNumber() == "123");
  }

  std::cout << "nasdaq decode OK\n";
}
