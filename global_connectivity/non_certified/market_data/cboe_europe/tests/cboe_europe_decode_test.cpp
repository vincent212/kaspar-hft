// Decode tests for cboe_europe.tbl (Cboe Titanium Europe Multicast PITCH
// Specification, version 6.69, 06 November 2026; "EU" below). Byte sequences
// are EU Appendix B "Example Messages" (pp.77-84), or built from the section 4
// tables where an example is missing or contradicts its table (noted at each
// place). The unit header, GRP and Spin layers are decoded with the header
// generated from ../cboe/pitch_us.tbl, which EU shares (feed.md section 3).
//
//   python3 fieldgen.py .../cboe_europe/cboe_europe.tbl -o $OUT/cboe_europe.hpp
//   python3 fieldgen.py .../cboe/pitch_us.tbl -o $OUT/pitch_us.hpp
//   c++ -std=c++20 -Wall -Wextra -Werror -I$OUT cboe_europe_decode_test.cpp
#include "cboe_europe.hpp"
#include "pitch_us.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using bytes = std::vector<unsigned char>;
namespace pitch = cboe::pitch::from_cboe_eu;

static int checks = 0;
#define CHECK(c)                                                                                   \
  do                                                                                               \
  {                                                                                                \
    ++checks;                                                                                      \
    if (!(c))                                                                                      \
    {                                                                                              \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #c "\n";                      \
      std::abort();                                                                                \
    }                                                                                              \
  } while (0)

static bytes cat(std::initializer_list<bytes> parts)
{
  bytes out;
  for (const auto &p : parts)
    out.insert(out.end(), p.begin(), p.end());
  return out;
}

// Order and execution ids are 8-byte binaries that EU shows as 12 base-36
// characters (EU 2.5, p.11; the example on p.82 shows Order Id 631WC4000005).
static std::string base36(uint64_t v)
{
  const char *d = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string s;
  while (v)
  {
    s.insert(s.begin(), d[v % 36]);
    v /= 36;
  }
  return s;
}

template <typename M>
static M one(const bytes &b, size_t expect_size)
{
  auto d = pitch::decode(reinterpret_cast<const char *>(b.data()), b.size());
  CHECK(d);
  CHECK(d.size == expect_size);
  CHECK(std::holds_alternative<M>(d.msg));
  return std::get<M>(d.msg);
}

static const bytes TO_447000 = {0x18, 0xD2, 0x06, 0x00};
static const bytes TO_102189000 = {0xC8, 0x47, 0x17, 0x06};
static const bytes ORDER_ID = {0x05, 0x40, 0x5B, 0x77, 0x8F, 0x56, 0x1D, 0x0B};
static const bytes EXEC_ID = {0xC8, 0x00, 0x00, 0x00, 0x01, 0x40, 0x57, 0x3A};
static const bytes VODL8 = {0x56, 0x4F, 0x44, 0x6C, 0x20, 0x20, 0x20, 0x20};
static const bytes VODL6 = {0x56, 0x4F, 0x44, 0x6C, 0x20, 0x20};
static const bytes LEMDL8 = {0x4C, 0x45, 0x4D, 0x44, 0x6C, 0x20, 0x20, 0x20};
static const bytes PX_102_50 = {0xE8, 0xA3, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00};
static const bytes PX_10_0475 = {0x7B, 0x88, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

static void sequenced_unit_with_two_messages()
{
  // EU Appendix B, "Sequenced Unit Header with 2 Messages" (pp.82-83). Hdr
  // Length 0x31 = 49 = 8 + 25 + 16, consistent with EU's 25-byte Add Order
  // Short (the U.S. example, with a 26-byte add, is inconsistent).
  bytes add = cat({{0x19, 0x22}, TO_447000, ORDER_ID, {0x42, 0xE1, 0x02}, VODL6, {0x01, 0x00}});
  bytes red = cat({{0x10, 0x26}, {0x18, 0xD9, 0x06, 0x00}, ORDER_ID, {0x64, 0x00}});
  bytes pkt = cat({{0x31, 0x00, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00}, add, red});
  CHECK(pkt.size() == 49);

  cboe::unit::from_cboe::SequencedUnitHeader h;
  std::memcpy(&h, pkt.data(), sizeof h);
  CHECK(h.HdrLength == 49 && h.HdrCount == 2 && h.HdrUnit == 1 && h.HdrSequence == 1);

  const char *p = reinterpret_cast<const char *>(pkt.data()) + sizeof h;
  size_t left = h.HdrLength - sizeof h;
  for (unsigned i = 0; i < h.HdrCount; ++i)
  {
    auto d = pitch::decode(p, left);
    CHECK(d);
    if (i == 0)
    {
      const auto &m = std::get<pitch::AddOrderShort>(d.msg);
      CHECK(d.size == 25 && m.TimeOffset == 447000u && m.SideIndicator == 'B' && m.Quantity == 737);
      CHECK(m.get_Symbol() == "VODl" && m.Price == 1);
      CHECK(base36(m.OrderId) == "631WC4000005");
    }
    else
    {
      const auto &m = std::get<pitch::ReduceSizeShort>(d.msg);
      // The example's offset bytes 18 D9 06 00 are 448,792 ns; the text says 449,000.
      CHECK(d.size == 16 && m.TimeOffset == 448792u && m.CancelledShares == 100);
    }
    p += d.size;
    left -= d.size;
  }
  CHECK(left == 0);
}

static void time_and_session()
{
  // EU p.78: Time 34,200 s = 09:30 UK time (the example labels the field "Sequence").
  CHECK(one<pitch::TimeMessage>({0x06, 0x20, 0x98, 0x85, 0x00, 0x00}, 6).Time == 34200u);
  // Unit Clear: no example in EU; built from EU 4.2, p.14.
  CHECK(one<pitch::UnitClear>(cat({{0x06, 0x97}, TO_447000}), 6).TimeOffset == 447000u);
  // EU p.81: End of Session.
  CHECK(one<pitch::EndOfSession>(cat({{0x06, 0x2D}, TO_447000}), 6).TimeOffset == 447000u);
  // Transaction Begin and End: no example; EU 4.11, 4.12, p.26.
  CHECK(one<pitch::TransactionBegin>(cat({{0x06, 0xBC}, TO_447000}), 6).TimeOffset == 447000u);
  CHECK(one<pitch::TransactionEnd>(cat({{0x06, 0xBD}, TO_447000}), 6).TimeOffset == 447000u);
}

static void orders()
{
  // EU p.78: Add Order Long, 35 bytes, type 0x40.
  auto al = one<pitch::AddOrderLong>(cat({{0x23, 0x40}, TO_447000, ORDER_ID, {0x42}, {0x20, 0x4E, 0x00, 0x00},
                                          {0x5A, 0x56, 0x5A, 0x5A, 0x54, 0x6C, 0x20, 0x20},
                                          {0x5A, 0x23, 0, 0, 0, 0, 0, 0}}),
                                     35);
  CHECK(al.TimeOffset == 447000u && base36(al.OrderId) == "631WC4000005" && al.SideIndicator == 'B');
  CHECK(al.Quantity == 20000u && al.get_Symbol() == "ZVZZTl" && al.Price == 9050u);

  // EU p.78: Add Order Short, 25 bytes, price 102.50 with 2 decimals.
  auto as = one<pitch::AddOrderShort>(
      cat({{0x19, 0x22}, TO_447000, ORDER_ID, {0x42}, {0x20, 0x4E}, {0x46, 0x50, 0x70, 0x20, 0x20, 0x20}, {0x0A, 0x28}}),
      25);
  CHECK(as.Quantity == 20000 && as.get_Symbol() == "FPp" && as.Price == 10250);

  // EU p.81: Expanded Add Order (labelled "End of Session" in the example), SI quote.
  auto ax = one<pitch::AddOrderExpanded>(cat({{0x28, 0x2F}, TO_447000, ORDER_ID, {0x42}, {0x64, 0, 0, 0}, VODL8,
                                              {0xA0, 0x06, 0x1C, 0x06, 0, 0, 0, 0}, {0x02}, {0x41, 0x42, 0x43, 0x44}}),
                                         40);
  CHECK(ax.Quantity == 100u && ax.Price == 102500000u && (ax.AddFlags & 0x02) && ax.get_ParticipantID() == "ABCD");

  // Order Executed. EU p.78-79 shows Length 0x1D (29) and 3 flag characters;
  // the table (EU 4.4, p.17) has 4 (MMT 1, 2, 3.6, 3.9) and 30 bytes. Built
  // from the table with the example's values and '-' for level 3.9.
  bytes ox = cat({{0x1E, 0x23}, TO_447000, ORDER_ID, {0x64, 0, 0, 0}, EXEC_ID, {0x31, 0x32, 0x2D, 0x2D}});
  auto oe = one<pitch::OrderExecuted>(ox, 30);
  CHECK(oe.ExecutedShares == 100u && std::string(oe.ExecutionFlags, 4) == "12--");
  CHECK(base36(oe.ExecutionId).size() == 12);
  // The example as printed (29 bytes) is shorter than the table's fixed part.
  bytes ox29 = cat({{0x1D, 0x23}, TO_447000, ORDER_ID, {0x64, 0, 0, 0}, EXEC_ID, {0x31, 0x32, 0x2D}});
  CHECK(pitch::decode(reinterpret_cast<const char *>(ox29.data()), ox29.size()).st == cboe::status::malformed);

  // Order Executed at Price/Size: EU p.79 Length 0x29 (41), 3 flags; table 42 (EU 4.5, p.18).
  auto op = one<pitch::OrderExecutedAtPriceSize>(cat({{0x2A, 0x24}, TO_447000, ORDER_ID, {0x64, 0, 0, 0},
                                                      {0xBC, 0x4D, 0, 0}, EXEC_ID, PX_102_50, {0x31, 0x4B, 0x2D, 0x2D}}),
                                                 42);
  CHECK(op.ExecutedShares == 100u && op.RemainingShares == 19900u && op.Price == 1025000u && op.ExecutionFlags[1] == 'K');

  // EU p.79: Reduce Size Long and Short.
  CHECK(one<pitch::ReduceSizeLong>(cat({{0x12, 0x25}, TO_447000, ORDER_ID, {0xF8, 0x24, 0x01, 0x00}}), 18)
            .CancelledShares == 75000u);
  CHECK(one<pitch::ReduceSizeShort>(cat({{0x10, 0x26}, TO_447000, ORDER_ID, {0x64, 0x00}}), 16).CancelledShares == 100);

  // EU p.79-80: Modify Order Long and Short (no Modify Flags).
  auto ml = one<pitch::ModifyOrderLong>(cat({{0x1A, 0x27}, TO_447000, ORDER_ID, {0xF8, 0x24, 0x01, 0x00}, PX_102_50}), 26);
  CHECK(ml.Shares == 75000u && ml.Price == 1025000u);
  auto ms = one<pitch::ModifyOrderShort>(cat({{0x12, 0x28}, TO_447000, ORDER_ID, {0x64, 0x00}, {0x0A, 0x28}}), 18);
  CHECK(ms.Shares == 100 && ms.Price == 10250);

  // EU p.80: Delete Order.
  CHECK(one<pitch::DeleteOrder>(cat({{0x0E, 0x29}, TO_447000, ORDER_ID}), 14).OrderId == 0x0B1D568F775B4005ull);
}

static void trades()
{
  // Trade Long: EU p.80 Length 0x2F (47), 4 flags; table 48 bytes, 5 flags
  // (EU 4.9.1, p.21). Built from the table, '-' for level 3.9.
  auto tl = one<pitch::TradeLong>(cat({{0x30, 0x41}, TO_447000, ORDER_ID, {0x42}, {0xF8, 0x24, 0x01, 0x00}, VODL8,
                                       PX_102_50, EXEC_ID, {0x33, 0x32, 0x44, 0x45, 0x2D}}),
                                  48);
  CHECK(tl.Shares == 75000u && tl.get_Symbol() == "VODl" && tl.Price == 1025000u && tl.TradeFlags[0] == '3');

  // Trade Short: EU p.80 Length 0x25 (37); table 38 (EU 4.9.2, p.21).
  auto ts = one<pitch::TradeShort>(
      cat({{0x26, 0x2B}, TO_447000, ORDER_ID, {0x42}, {0x64, 0x00}, VODL6, {0x0A, 0x28}, EXEC_ID, {0x31, 0x4F, 0x50, 0x2D, 0x2D}}),
      38);
  CHECK(ts.Shares == 100 && ts.Price == 10250 && ts.TradeFlags[1] == 'O');

  // Trade Extended: EU p.80-81 Length 0x43 (67) but 68 bytes listed; table 68
  // (EU 4.9.4, p.23). The example's Cboe Flag 'R' is not a value of EU 4.9.7.
  bytes ext = cat({{0x44, 0x32}, TO_447000, {0xF8, 0x24, 0x01, 0, 0, 0, 0, 0}, VODL8, PX_102_50, EXEC_ID,
                   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}, {0x47, 0x42, 0x58}, {0x52}, bytes(14, 0x2D)});
  CHECK(ext.size() == 68);
  auto te = one<pitch::TradeExtended>(ext, 68);
  CHECK(te.Shares == 75000u && te.Price == 1025000u && te.get_Currency() == "GBX" && te.CboeTradeFlags == 'R');
  CHECK(std::string(te.ExtendedTradeFlags, 14) == std::string(14, '-'));

  // Trade Unknown Symbol: no example; built from EU 4.9.5, p.24.
  bytes tu = cat({{0x48, 0x35}, TO_447000, {0x10, 0x27, 0, 0, 0, 0, 0, 0},
                  {'G', 'B', '0', '0', '0', '7', '1', '8', '8', '7', '5', '7'}, {0xA0, 0x06, 0x1C, 0x06, 0, 0, 0, 0},
                  EXEC_ID, {0, 0, 0, 0, 0, 0, 0, 0}, {'X', 'O', 'F', 'F'}, {'G', 'B', 'P'}, {'4'}, bytes(14, '-')});
  auto tus = one<pitch::TradeUnknownSymbol>(tu, 72);
  CHECK(tus.Shares == 10000u && tus.get_Symbol() == "GB0007188757" && tus.get_ExecutionVenue() == "XOFF");

  // EU p.81: Trade Break (no table in EU section 4).
  CHECK(one<pitch::TradeBreak>(cat({{0x0E, 0x2C}, TO_447000, EXEC_ID}), 14).ExecutionId == 0x3A574001000000C8ull);
}

static void status_and_auctions()
{
  // EU p.81: Trading Status; Reserved bytes 00 in the example (Alpha would be spaces).
  auto st = one<pitch::TradingStatus>(cat({{0x12, 0x31}, TO_447000, VODL8, {0x54}, {0, 0, 0}}), 18);
  CHECK(st.get_Symbol() == "VODl" && st.Status == 'T');

  // EU p.81-82: Statistics.
  auto sx = one<pitch::Statistics>(cat({{0x18, 0x34}, TO_447000, VODL8, {0x5A, 0x23, 0, 0, 0, 0, 0, 0}, {0x4F}, {0x30}}), 24);
  CHECK(sx.Price == 9050u && sx.StatisticType == 'O' && sx.PriceDetermination == '0');

  // EU p.82: Auction Update, type 0xAC.
  auto au = one<pitch::AuctionUpdate>(cat({{0x25, 0xAC}, TO_102189000, LEMDL8, {0x50}, PX_10_0475, PX_10_0475,
                                           {0x88, 0x13, 0, 0}, {0x49}, {0x50}}),
                                      37);
  CHECK(au.TimeOffset == 102189000u && au.get_Symbol() == "LEMDl" && au.AuctionType == 'P');
  CHECK(au.ReferencePrice == 100475u && au.IndicativeShares == 5000u && au.OutsideTolerance == 'I' && au.IncludesPrimary == 'P');

  // EU p.82: Auction Summary.
  auto sm = one<pitch::AuctionSummary>(cat({{0x1B, 0x96}, TO_102189000, LEMDL8, {0x4F}, PX_10_0475, {0x88, 0x13, 0, 0}}), 27);
  CHECK(sm.AuctionType == 'O' && sm.Price == 100475u && sm.Shares == 5000u);
}

static void unknown_grown_and_partial()
{
  // Index Quote (0xD8) is not tabled: unknown, skipped by its Length (EU p.84 example).
  bytes iq = {0x19, 0xD8, 0x00, 0x68, 0x64, 0x73, 0x8F, 0x21, 0x42, 0x41, 0x54, 0x32, 0x30, 0x4E, 0, 0, 0, 0,
              0xE8, 0xA3, 0x0F, 0, 0, 0, 0, 0, 0x49};
  auto d = pitch::decode(reinterpret_cast<const char *>(iq.data()), iq.size());
  CHECK(d.st == cboe::status::unknown && d.size == 0x19);

  // U.S.-only codes are unknown here: 0x21 (U.S. Add Order Long), 0x2A, 0x30, 0x95, 0xB1.
  for (uint8_t code : {0x21, 0x2A, 0x30, 0x95, 0xB1, 0x98})
  {
    bytes b(10, 0);
    b[0] = 10;
    b[1] = code;
    auto u = pitch::decode(reinterpret_cast<const char *>(b.data()), b.size());
    CHECK(u.st == cboe::status::unknown && u.size == 10);
  }

  // A message grown at its end is accepted and the extra bytes skipped (EU 2.1, p.10).
  bytes grown = cat({{0x10, 0x29}, TO_447000, ORDER_ID, {0xAA, 0xBB}});
  auto g = pitch::decode(reinterpret_cast<const char *>(grown.data()), grown.size());
  CHECK(g && g.size == 16 && std::get<pitch::DeleteOrder>(g.msg).OrderId == 0x0B1D568F775B4005ull);

  // Incomplete: the buffer ends inside the message.
  bytes part = cat({{0x0E, 0x29}, TO_447000, {0x05, 0x40}});
  CHECK(pitch::decode(reinterpret_cast<const char *>(part.data()), part.size()).st == cboe::status::incomplete);
}

static void gap_request_proxy()
{
  namespace to = cboe::grp::to_cboe;
  namespace from = cboe::grp::from_cboe;
  char buf[64];

  // EU p.77: Login, 22 bytes.
  bytes login = {0x16, 0x01, 0x30, 0x30, 0x30, 0x31, 0x46, 0x49, 0x52, 0x4D, 0x20, 0x20,
                 0x41, 0x42, 0x43, 0x44, 0x30, 0x30, 0x20, 0x20, 0x20, 0x20};
  to::Login l{};
  l.set_SessionSubId("0001");
  l.set_Username("FIRM");
  l.set_Filler("");
  l.set_Password("ABCD00");
  CHECK(to::encode(buf, sizeof buf, l) == 22 && std::memcmp(buf, login.data(), 22) == 0);

  // EU p.77: Login Response.
  bytes lr = {0x03, 0x02, 0x41};
  auto r = from::decode(reinterpret_cast<const char *>(lr.data()), lr.size());
  CHECK(r && std::get<from::LoginResponse>(r.msg).Status == 'A');

  // EU p.77: Gap Request, unit 1, 4155, 50 messages.
  bytes gap = {0x09, 0x03, 0x01, 0x3B, 0x10, 0x00, 0x00, 0x32, 0x00};
  to::GapRequest g{};
  g.Unit = 1;
  g.Sequence = 4155;
  g.Count = 50;
  CHECK(to::encode(buf, sizeof buf, g) == 9 && std::memcmp(buf, gap.data(), 9) == 0);

  // Gap Response: EU p.77 example has Length 0x0A but omits Count; built from
  // the table (EU 3.4, p.13).
  bytes gr = {0x0A, 0x04, 0x01, 0x3B, 0x10, 0x00, 0x00, 0x32, 0x00, 0x41};
  auto rr = from::decode(reinterpret_cast<const char *>(gr.data()), gr.size());
  CHECK(rr);
  const auto &grm = std::get<from::GapResponse>(rr.msg);
  CHECK(grm.Unit == 1 && grm.Sequence == 4155u && grm.Count == 50 && grm.Status == 'A');

  // Client messages go inside an unsequenced unit header (EU 3, p.12).
  cboe::unit::to_cboe::SequencedUnitHeader h{};
  h.HdrLength = 8 + 9;
  h.HdrCount = 1;
  CHECK(cboe::unit::to_cboe::encode(buf, sizeof buf, h) == 8 && buf[0] == 17 && buf[2] == 1 && buf[3] == 0);
}

static void spin_server()
{
  namespace to = cboe::spin::to_cboe;
  namespace from = cboe::spin::from_cboe;
  char buf[16];

  // EU p.77: Spin Request 4155.
  bytes sr = {0x06, 0x81, 0x3B, 0x10, 0x00, 0x00};
  to::SpinRequest q{};
  q.Sequence = 4155;
  CHECK(to::encode(buf, sizeof buf, q) == 6 && std::memcmp(buf, sr.data(), 6) == 0);

  // EU p.77-78: Spin Image Available, Spin Response (66 orders), Spin Finished.
  // The Spin Finished example adds a Status byte after a Length of 6; decoded by
  // the Length, the extra byte is not part of the message.
  bytes stream = {0x06, 0x80, 0x3B, 0x10, 0x00, 0x00,
                  0x0B, 0x82, 0x3B, 0x10, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x41,
                  0x06, 0x83, 0x3B, 0x10, 0x00, 0x00};
  const char *p = reinterpret_cast<const char *>(stream.data());
  size_t left = stream.size();
  auto a = from::decode(p, left);
  CHECK(a && std::get<from::SpinImageAvailable>(a.msg).Sequence == 4155u);
  p += a.size, left -= a.size;
  auto b = from::decode(p, left);
  CHECK(b && std::get<from::SpinResponse>(b.msg).OrderCount == 66u && std::get<from::SpinResponse>(b.msg).Status == 'A');
  p += b.size, left -= b.size;
  auto c = from::decode(p, left);
  CHECK(c && std::get<from::SpinFinished>(c.msg).Sequence == 4155u && c.size == 6);
  CHECK(left == 6);
}

int main()
{
  sequenced_unit_with_two_messages();
  time_and_session();
  orders();
  trades();
  status_and_auctions();
  unknown_grown_and_partial();
  gap_request_proxy();
  spin_server();
  std::cout << "cboe_europe_decode_test: " << checks << " checks OK\n";
}
