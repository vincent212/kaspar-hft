// Decode through the header generated from iex.tbl, using the example bytes of
// the IEX Transport Specification v1.25 (p.10-12), the DEEP+ Specification
// v1.06 (p.13-27) and the DEEP+ SNAP Specification v1.07 (p.7-11).
//
//   fieldgen.py global_connectivity/non_certified/market_data/iex/iex.tbl -o <scratch>/iex.hpp
//   c++ -std=c++20 -Wall -Wextra -Werror -I<scratch> iex_decode_test.cpp
#include "iex.hpp"
#include "../../test_support.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace test_support;

// An IEX-TP message block: 2-byte little-endian length (excluding itself), then the data.
static bytes block(const bytes &data)
{
  bytes out{(unsigned char)(data.size() & 0xff), (unsigned char)(data.size() >> 8)};
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

static const char *ZIEXT = "5a 49 45 58 54 20 20 20";
static const char *TS16 = "b2 8f a5 a0 ab 86 6d 14"; // 1471980724912754610 ns
static const char *TS16B = "ac 63 c0 20 96 86 6d 14"; // 1471980632572715948 ns
static const char *PX = "24 1d 0f 00 00 00 00 00"; // 990500 = $99.05

template <typename M>
static M one(const bytes &b, size_t size)
{
  return decode_as<M>(iex::deepplus::from_iex::decode, cp(b), b.size(), size);
}

int main()
{
  using namespace iex;
  namespace dp = iex::deepplus::from_iex;

  // IEX-TP header (TP p.10, multicast publication example).
  {
    auto b = hex("01 00 04 80 01 00 00 00 00 00 87 42 48 00 02 00 "
                 "8c a6 21 00 00 00 00 00 ca c3 00 00 00 00 00 00 ec 45 c2 20 96 86 6d 14");
    CHECK(b.size() == sizeof(tp::from_iex::SegmentHeader));
    tp::from_iex::SegmentHeader h;
    std::memcpy(&h, b.data(), sizeof h);
    CHECK(h.Version == 1 && h.Reserved == 0 && h.MessageProtocolID == 0x8004);
    CHECK(h.ChannelID == 1 && h.SessionID == 0x42870000u && h.PayloadLength == 72 && h.MessageCount == 2);
    CHECK(h.StreamOffset == 2205324 && h.FirstMessageSequenceNumber == 50122);
    CHECK(h.SendTime == 1471980632572839404ll);
  }

  // Gap fill request (TP p.11): header and two sequenced range blocks, encoded.
  {
    tp::to_iex::GapFillRequestHeader g{};
    g.Version = 1;
    g.RequestType = 1;
    g.MessageProtocolID = 0x8004;
    g.ChannelID = 1;
    g.SessionID = 0x42870000u;
    g.RequestRangeCount = 2;
    tp::to_iex::RequestRangeSequenced r1{12340, 12350}, r2{56789, 56799};
    char buf[64];
    size_t n = tp::to_iex::encode(buf, sizeof buf, g);
    n += tp::to_iex::encode(buf + n, sizeof buf - n, r1);
    n += tp::to_iex::encode(buf + n, sizeof buf - n, r2);
    auto want = hex("01 01 04 80 01 00 00 00 00 00 87 42 02 00 00 00 "
                    "34 30 00 00 00 00 00 00 3e 30 00 00 00 00 00 00 "
                    "d5 dd 00 00 00 00 00 00 df dd 00 00 00 00 00 00");
    CHECK(n == want.size() && std::memcmp(buf, want.data(), n) == 0);
  }

  // System Event (DPLS p.13).
  {
    auto m = one<dp::SystemEvent>(block(hex("53 45 00 a0 99 97 e9 3d b6 14")), 12);
    CHECK(m.MessageLength == 10 && m.SystemEvent == 'E' && m.Timestamp == 1492448400000000000ll);
  }
  // Security Directory (p.14).
  {
    auto m = one<dp::SecurityDirectory>(
        block(hex(std::string("44 80 00 20 89 7b 5a 1f b6 14 ") + ZIEXT + " 64 00 00 00 " + PX + " 01")), 33);
    CHECK(m.Flags == 0x80 && m.Timestamp == 1492414800000000000ll && m.get_Symbol() == "ZIEXT");
    CHECK(m.RoundLotSize == 100 && m.AdjustedPOCPrice == 990500 && m.LULDTier == 1);
  }
  // Trading Status (p.16).
  {
    auto m = one<dp::TradingStatus>(
        block(hex(std::string("48 48 ") + TS16B + " " + ZIEXT + " 54 31 20 20")), 24);
    CHECK(m.TradingStatus == 'H' && m.Timestamp == 1471980632572715948ll && m.get_Reason() == "T1");
    CHECK(std::string_view(m.Reason, 4) == "T1  ");
  }
  // Retail Liquidity Indicator (p.17).
  {
    auto m = one<dp::RetailLiquidityIndicator>(block(hex(std::string("49 41 ") + TS16B + " " + ZIEXT)), 20);
    CHECK(m.RetailLiquidityIndicator == 'A' && m.get_Symbol() == "ZIEXT");
  }
  // Operational Halt Status (p.18).
  {
    auto m = one<dp::OperationalHaltStatus>(block(hex(std::string("4f 4f ") + TS16B + " " + ZIEXT)), 20);
    CHECK(m.OperationalHaltStatus == 'O');
  }
  // Short Sale Price Test Status (p.19).
  {
    auto m = one<dp::ShortSalePriceTestStatus>(
        block(hex(std::string("50 01 ") + TS16B + " " + ZIEXT + " 41")), 21);
    CHECK(m.ShortSalePriceTestStatus == 1 && m.Detail == 'A');
  }
  // Security Event (p.20).
  {
    auto m = one<dp::SecurityEvent>(block(hex(std::string("45 4f 00 f0 30 2a 5b 25 b6 14 ") + ZIEXT)), 20);
    CHECK(m.SecurityEvent == 'O' && m.Timestamp == 1492421400000000000ll);
  }
  // Add Order (p.21).
  {
    auto m = one<dp::AddOrder>(
        block(hex(std::string("61 38 ") + TS16 + " " + ZIEXT + " 96 8f 06 00 00 00 00 00 64 00 00 00 " + PX)), 40);
    CHECK(m.MessageType == 'a' && m.Side == '8' && m.Timestamp == 1471980724912754610ll);
    CHECK(m.OrderID == 429974 && m.Size == 100 && m.Price == 990500);
  }
  // Order Modify (p.22).
  {
    auto m = one<dp::OrderModify>(
        block(hex(std::string("4d 00 ") + TS16 + " " + ZIEXT + " 96 8f 06 00 00 00 00 00 64 00 00 00 " + PX)), 40);
    CHECK((m.ModifyFlags & 1) == 0 && m.OrderIDReference == 429974 && m.Size == 100 && m.Price == 990500);
  }
  // Order Delete (p.23).
  {
    auto m = one<dp::OrderDelete>(block(hex(std::string("52 00 ") + TS16 + " " + ZIEXT + " 96 8f 06 00 00 00 00 00")), 28);
    CHECK(m.Reserved == 0 && m.OrderIDReference == 429974);
  }
  // Order Executed (p.24).
  {
    auto m = one<dp::OrderExecuted>(block(hex(std::string("4c 00 ") + TS16 + " " + ZIEXT +
                                              " 96 8f 06 00 00 00 00 00 64 00 00 00 " + PX +
                                              " 96 8f 02 00 00 00 00 00")), 48);
    CHECK(m.SaleConditionFlags == 0 && m.OrderIDReference == 429974 && m.Size == 100);
    CHECK(m.Price == 990500 && m.TradeID == 167830);
  }
  // Trade (p.25) and Trade Break (p.26).
  {
    auto t = one<dp::Trade>(block(hex(std::string("54 00 ") + TS16 + " " + ZIEXT + " 64 00 00 00 " + PX +
                                      " 96 8f 02 00 00 00 00 00")), 40);
    CHECK(t.Size == 100 && t.Price == 990500 && t.TradeID == 167830);
    auto b = one<dp::TradeBreak>(block(hex(std::string("42 00 ") + TS16 + " " + ZIEXT + " 64 00 00 00 " + PX +
                                           " 96 8f 06 00 00 00 00 00")), 40);
    CHECK(b.Size == 100 && b.Price == 990500 && b.TradeID == 429974);
  }
  // Clear Book (p.27).
  {
    auto m = one<dp::ClearBook>(block(hex(std::string("43 00 ") + TS16 + " " + ZIEXT)), 20);
    CHECK(m.get_Symbol() == "ZIEXT");
  }

  // A segment with two message blocks, walked by size; then the framing cases.
  {
    auto a = block(hex(std::string("43 00 ") + TS16 + " " + ZIEXT));
    auto s = block(hex("53 43 00 a0 99 97 e9 3d b6 14"));
    bytes seg = a;
    seg.insert(seg.end(), s.begin(), s.end());
    auto d1 = dp::decode(cp(seg), seg.size());
    CHECK(d1 && std::holds_alternative<dp::ClearBook>(d1.msg));
    auto d2 = dp::decode(cp(seg) + d1.size, seg.size() - d1.size);
    CHECK(d2 && d1.size + d2.size == seg.size() && std::get<dp::SystemEvent>(d2.msg).SystemEvent == 'C');

    // A message grown at the end (DPLS p.4) decodes, and the extra bytes are skipped.
    auto g = hex(std::string("43 00 ") + TS16 + " " + ZIEXT + " ff ff");
    auto dg = dp::decode(cp(block(g)), g.size() + 2);
    CHECK(dg && dg.size == 22);

    // A type the table does not list (DEEP's Auction Information 'A') is skipped by its length.
    auto u = block(bytes(80, 0));
    u[2] = 'A';
    auto du = dp::decode(cp(u), u.size());
    CHECK(du.st == status::unknown && du.size == 82);

    // A length shorter than the message is malformed; a cut buffer is incomplete.
    auto sh = block(hex(std::string("61 38 ") + TS16));
    auto ds = dp::decode(cp(sh), sh.size());
    CHECK(ds.st == status::malformed && ds.size == 12);
    CHECK(dp::decode(cp(a), a.size() - 1).st == status::incomplete);
  }

  // SNAP SnapshotRequest (SNAP p.7), encoded and compared with the example.
  {
    snap::to_iex::SnapshotRequest r{};
    r.set_AuthenticationToken("ATOKEN1");
    r.ChannelID = 1;
    r.SessionID = 1259897328u;
    r.MinimumSequenceNumber = 468;
    char buf[64];
    CHECK(snap::to_iex::encode(buf, sizeof buf, r) == 59);
    auto want = hex("39 00 72 41 54 4F 4B 45 4E 31");
    CHECK(std::memcmp(buf, want.data(), want.size()) == 0);
    CHECK(std::string_view(buf + 10, 33) == std::string(33, ' '));
    auto tail = hex("01 00 00 00 F0 81 18 4B D4 01 00 00 00 00 00 00");
    CHECK(std::memcmp(buf + 43, tail.data(), tail.size()) == 0);
  }
  // ErrorResponse (p.8), SnapshotStart (p.9), SnapshotEnd (p.11).
  {
    auto e = hex("02 00 65 51");
    auto de = snap::from_iex::decode(cp(e), e.size());
    CHECK(de && de.size == 4 && std::get<snap::from_iex::ErrorResponse>(de.msg).RejectReasonCode == 'Q');
    auto s = hex("09 00 73 C7 1E 70 00 00 00 00 00");
    auto ds = snap::from_iex::decode(cp(s), s.size());
    CHECK(ds && ds.size == 11 && std::get<snap::from_iex::SnapshotStart>(ds.msg).SnapshotLength == 7347911);
    auto x = hex("09 00 78 62 AC 00 00 00 00 00 00");
    auto dx = snap::from_iex::decode(cp(x), x.size());
    CHECK(dx && std::get<snap::from_iex::SnapshotEnd>(dx.msg).SnapshotSequenceNumber == 44130);
  }
  // SnapshotData (p.10): the IEX-TP header, the block length 33, then the
  // example's inner length 31 and a 31-byte Security Directory.
  {
    auto b = hex(std::string("4C 00 64 "
                             "01 00 05 80 01 00 00 00 F0 81 18 4B 23 00 01 00 04 4F 0E 00 00 00 00 00 "
                             "62 AC 00 00 00 00 00 00 C1 8F 8B D9 4F 1E 0C 17 "
                             "21 00 1F 00 44 80 51 B8 08 8F 4F 1E 0C 17 ") +
                 ZIEXT + " 64 00 00 00 " + PX + " 01");
    CHECK(b.size() == 78);
    auto d = snap::from_iex::decode(cp(b), b.size());
    CHECK(d && d.size == 78 && std::holds_alternative<snap::from_iex::SnapshotData>(d.msg));
    const auto &sd = std::get<snap::from_iex::SnapshotData>(d.msg);
    CHECK(sd.IEXTPMessageBlockLength == 33 && d.tail_len == 33);
    tp::from_iex::SegmentHeader h;
    std::memcpy(&h, sd.IEXTPHeader, sizeof h);
    CHECK(h.MessageProtocolID == 0x8005 && h.SessionID == 1259897328u && h.PayloadLength == 35);
    CHECK(h.MessageCount == 1 && h.StreamOffset == 937732 && h.FirstMessageSequenceNumber == 44130);
    // As printed, the block data is itself length-prefixed (feed.md section 10):
    // the DEEP+ decoder reads it from the tail.
    auto m = dp::decode(d.tail, d.tail_len);
    CHECK(m && m.size == 33);
    const auto &sdir = std::get<dp::SecurityDirectory>(m.msg);
    CHECK(sdir.get_Symbol() == "ZIEXT" && sdir.RoundLotSize == 100 && sdir.LULDTier == 1);
    std::cout << sdir << "\n";
  }

  std::cout << "iex OK\n";
}
