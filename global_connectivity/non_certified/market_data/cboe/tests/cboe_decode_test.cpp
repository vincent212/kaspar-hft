// Decode tests for pitch_us.tbl (Cboe Titanium U.S. Equities/Options Multicast
// PITCH, version 2.41.87, November 10, 2026). Byte sequences are the
// specification's "Example Messages", or built from its tables where the example
// is missing or contradicts the table (noted at each place).
//
//   python3 fieldgen.py global_connectivity/non_certified/market_data/cboe/pitch_us.tbl -o $OUT/pitch_us.hpp
//   c++ -std=c++20 -Wall -Wextra -Werror -I$OUT cboe_decode_test.cpp
#include "pitch_us.hpp"
#include "../../test_support.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace test_support;
namespace pitch = cboe::pitch::from_cboe;

static bytes cat(std::initializer_list<bytes> parts)
{
  bytes out;
  for (const auto &p : parts)
    out.insert(out.end(), p.begin(), p.end());
  return out;
}

// Order and execution ids are shown in the spec in base 36 (US, Execution IDs).
static std::string base36(uint64_t v, size_t width = 0)
{
  const char *d = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string s;
  while (v)
  {
    s.insert(s.begin(), d[v % 36]);
    v /= 36;
  }
  while (s.size() < width)
    s.insert(s.begin(), '0');
  return s;
}

template <typename M>
static M one(const bytes &b, size_t expect_size)
{
  return decode_as<M>(pitch::decode, cp(b), b.size(), expect_size);
}

// Fields shared by the order examples.
static const bytes TO_447000 = {0x18, 0xD2, 0x06, 0x00};
static const bytes ORDER_ID = {0x05, 0x40, 0x5B, 0x77, 0x8F, 0x56, 0x1D, 0x0B};
static const bytes EXEC_ID = {0x34, 0x2B, 0x46, 0xE0, 0xBB, 0x00, 0x00, 0x00};
static const bytes ZVZZT6 = {0x5A, 0x56, 0x5A, 0x5A, 0x54, 0x20};
static const bytes ZVZZT8 = {0x5A, 0x56, 0x5A, 0x5A, 0x54, 0x20, 0x20, 0x20};
static const bytes PX_102_50 = {0xE8, 0xA3, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00};

static void sequenced_unit_with_two_messages()
{
  // US, Example Messages, "Sequenced Unit Header with 2 Messages". The example
  // gives Hdr Length 31 00 (49) and describes it as "50 bytes, including header";
  // 8 + 26 + 16 = 50, so the bytes are built with 0x32.
  bytes add = cat({{0x1A, 0x22}, {0x18, 0xD2, 0x06, 0x00}, ORDER_ID, {0x42, 0xE1, 0x02}, ZVZZT6, {0x01, 0x00}, {0x01}});
  bytes red = cat({{0x10, 0x26}, {0xE8, 0xD9, 0x06, 0x00}, ORDER_ID, {0xE1, 0x02}});
  bytes pkt = cat({{0x32, 0x00, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00}, add, red});
  CHECK(pkt.size() == 50);

  cboe::unit::from_cboe::SequencedUnitHeader h;
  static_assert(sizeof(h) == 8);
  std::memcpy(&h, pkt.data(), sizeof h);
  CHECK(h.HdrLength == pkt.size() && h.HdrCount == 2 && h.HdrUnit == 1 && h.HdrSequence == 1);

  // Walk the HdrCount messages by size; the implied sequence of each is
  // HdrSequence + index.
  const char *p = cp(pkt) + sizeof h;
  size_t left = h.HdrLength - sizeof h;
  std::vector<std::string> names;
  for (unsigned i = 0; i < h.HdrCount; ++i)
  {
    auto d = pitch::decode(p, left);
    CHECK(d);
    names.push_back(pitch::name(d.msg));
    if (i == 0)
    {
      const auto &m = std::get<pitch::AddOrderShort>(d.msg);
      CHECK(d.size == 26 && m.TimeOffset == 447000u && m.SideIndicator == 'B' && m.Quantity == 737);
      CHECK(m.get_Symbol() == "ZVZZT" && m.Price == 1 && m.AddFlags == 1);
      CHECK(base36(m.OrderId) == "631WC4000005");
    }
    else
    {
      const auto &m = std::get<pitch::ReduceSizeShort>(d.msg);
      CHECK(d.size == 16 && m.TimeOffset == 449000u && m.CanceledQuantity == 737);
    }
    p += d.size;
    left -= d.size;
  }
  CHECK(left == 0 && names[0] == "AddOrderShort" && names[1] == "ReduceSizeShort");
  // Next frame's first sequence: HdrSequence + HdrCount (US, Sequenced Unit Header).
  CHECK(h.HdrSequence + h.HdrCount == 3u);
}

static void time_messages()
{
  // US, Time Reference Message Example.
  auto tr = one<pitch::TimeReference>(
      {0x12, 0xB1, 0xD0, 0x8B, 0x34, 0x60, 0x00, 0xE1, 0x00, 0x00, 0, 0, 0, 0, 0x2F, 0x62, 0x34, 0x01}, 18);
  CHECK(tr.MidnightReference == 1614056400u && tr.Time == 57600u && tr.TimeOffset == 0 && tr.TradeDate == 20210223u);

  // US, Time Message Example: 6 bytes (BYX, EDGA).
  bytes t6 = {0x06, 0x20, 0x98, 0x85, 0x00, 0x00};
  auto d6 = pitch::decode(cp(t6), t6.size());
  CHECK(d6 && d6.size == 6 && d6.tail_len == 0 && std::get<pitch::TimeMessage>(d6.msg).Time == 34200u);

  // US, Time Message Example (BZX/EDGX): 10 bytes, Epoch Time in the tail.
  bytes t10 = {0x0A, 0x20, 0x98, 0x85, 0x00, 0x00, 0x68, 0x11, 0x35, 0x60};
  auto d10 = pitch::decode(cp(t10), t10.size());
  CHECK(d10 && d10.size == 10 && d10.tail_len == 4);
  uint32_t epoch;
  std::memcpy(&epoch, d10.tail, 4);
  CHECK(epoch == 1614090600u);
  // Epoch Time and Time agree: 1614090600 is 2021-02-23 14:30 UTC, 09:30 ET.
  CHECK(epoch - tr.MidnightReference == 34200u);

  // US, Unit Clear Message Example.
  CHECK(one<pitch::UnitClear>(cat({{0x06, 0x97}, TO_447000}), 6).TimeOffset == 447000u);
  // US, End of Session Message Example.
  CHECK(one<pitch::EndOfSession>(cat({{0x06, 0x2D}, TO_447000}), 6).Timestamp == 447000u);
}

static void orders()
{
  // US, Add Order - Long Message Example.
  auto al = one<pitch::AddOrderLong>(
      cat({{0x22, 0x21}, TO_447000, ORDER_ID, {0x42, 0x20, 0x4E, 0x00, 0x00}, ZVZZT6,
           {0x5A, 0x23, 0, 0, 0, 0, 0, 0}, {0x01}}),
      34);
  CHECK(al.SideIndicator == 'B' && al.Quantity == 20000u && al.get_Symbol() == "ZVZZT");
  CHECK(al.Price == 9050u && al.AddFlags == 1 && base36(al.OrderId) == "631WC4000005");

  // US, Add Order - Short Message Example gives an 8-byte price (E8 A3 0F 00 00
  // 00 00 00) in a 26-byte message, against the table's 2-byte Binary Short
  // Price. Built from the table: price 102.50 = 0x280A.
  auto as = one<pitch::AddOrderShort>(
      cat({{0x1A, 0x22}, TO_447000, ORDER_ID, {0x42, 0x20, 0x4E}, ZVZZT6, {0x0A, 0x28}, {0x01}}), 26);
  CHECK(as.Quantity == 20000 && as.Price == 10250);

  // US, Add Order - Expanded (Equities) Message Example: 41 bytes.
  auto ax = one<pitch::AddOrderExpanded>(
      cat({{0x29, 0x2F}, TO_447000, ORDER_ID, {0x42, 0x20, 0x4E, 0x00, 0x00}, ZVZZT8,
           {0x5A, 0x23, 0, 0, 0, 0, 0, 0}, {0x01}, {0x4D, 0x50, 0x49, 0x44}, {0x4E}}),
      41);
  CHECK(ax.get_Symbol() == "ZVZZT" && ax.Price == 9050u && ax.get_ParticipantID() == "MPID" && ax.CustomerIndicator == 'N');

  // US, Order Executed (Equities) Message Example: 26 bytes.
  auto oe = one<pitch::OrderExecuted>(cat({{0x1A, 0x23}, TO_447000, ORDER_ID, {0x64, 0, 0, 0}, EXEC_ID}), 26);
  CHECK(oe.ExecutedQuantity == 100u && base36(oe.ExecutionId, 9) == "0AAP09VEC");
  // US, Order Executed (Options) Message Example: 27 bytes; the Trade Condition
  // byte is skipped by its length prefix.
  auto oeo = one<pitch::OrderExecuted>(cat({{0x1B, 0x23}, TO_447000, ORDER_ID, {0x64, 0, 0, 0}, EXEC_ID, {0x53}}), 27);
  CHECK(oeo.ExecutedQuantity == 100u);

  // US, Order Executed at Price/Size (Equities) Message Example: 38 bytes.
  auto ep = one<pitch::OrderExecutedAtPriceSize>(
      cat({{0x26, 0x24}, TO_447000, ORDER_ID, {0x64, 0, 0, 0}, {0xBC, 0x4D, 0, 0}, EXEC_ID, PX_102_50}), 38);
  CHECK(ep.ExecutedQuantity == 100u && ep.RemainingQuantity == 19900u && ep.Price == 1025000u);

  // US, Reduce Size - Long / Short Message Examples.
  CHECK(one<pitch::ReduceSizeLong>(cat({{0x12, 0x25}, TO_447000, ORDER_ID, {0xF8, 0x24, 0x01, 0x00}}), 18)
            .CanceledQuantity == 75000u);
  CHECK(one<pitch::ReduceSizeShort>(cat({{0x10, 0x26}, TO_447000, ORDER_ID, {0x64, 0x00}}), 16).CanceledQuantity == 100);

  // US, Modify Order - Long / Short Message Examples: flags 03, displayed and
  // maintains priority.
  auto ml = one<pitch::ModifyOrderLong>(cat({{0x1B, 0x27}, TO_447000, ORDER_ID, {0xF8, 0x24, 0x01, 0x00}, PX_102_50, {0x03}}), 27);
  CHECK(ml.Quantity == 75000u && ml.Price == 1025000u && (ml.ModifyFlags & 0x02));
  auto ms = one<pitch::ModifyOrderShort>(cat({{0x13, 0x28}, TO_447000, ORDER_ID, {0x64, 0x00}, {0x0A, 0x28}, {0x03}}), 19);
  CHECK(ms.Quantity == 100 && ms.Price == 10250 && ms.ModifyFlags == 3);

  // US, Delete Order Message Example.
  CHECK(base36(one<pitch::DeleteOrder>(cat({{0x0E, 0x29}, TO_447000, ORDER_ID}), 14).OrderId) == "631WC4000005");
}

static void trades()
{
  // US, Trade - Long (Equities) Message Example: 41 bytes.
  auto tl = one<pitch::TradeLong>(cat({{0x29, 0x2A}, TO_447000, ORDER_ID, {0x42, 0xF8, 0x24, 0x01, 0x00}, ZVZZT6, PX_102_50, EXEC_ID}), 41);
  CHECK(tl.SideIndicator == 'B' && tl.Quantity == 75000u && tl.get_Symbol() == "ZVZZT" && tl.Price == 1025000u);
  // US, Trade - Long (Options): 42 bytes, Trade Condition skipped.
  CHECK(one<pitch::TradeLong>(cat({{0x2A, 0x2A}, TO_447000, ORDER_ID, {0x42, 0xF8, 0x24, 0x01, 0x00}, ZVZZT6, PX_102_50, EXEC_ID, {0x53}}), 42)
            .Quantity == 75000u);

  // US, Trade - Short (Equities) Message Example: 33 bytes.
  auto ts = one<pitch::TradeShort>(cat({{0x21, 0x2B}, TO_447000, ORDER_ID, {0x42, 0x64, 0x00}, ZVZZT6, {0x0A, 0x28}, EXEC_ID}), 33);
  CHECK(ts.Quantity == 100 && ts.Price == 10250 && base36(ts.ExecutionId, 9) == "0AAP09VEC");
  // US, Trade - Short (Options): Length byte 22 (34), described as "33 bytes".
  // The bytes are 34 long; the length byte wins.
  CHECK(one<pitch::TradeShort>(cat({{0x22, 0x2B}, TO_447000, ORDER_ID, {0x42, 0x64, 0x00}, ZVZZT6, {0x0A, 0x28}, EXEC_ID, {0x20}}), 34)
            .Quantity == 100);

  // US, Trade - Expanded (Equities) Message Example: 43 bytes.
  auto tx = one<pitch::TradeExpanded>(cat({{0x2B, 0x30}, TO_447000, ORDER_ID, {0x42, 0xF8, 0x24, 0x01, 0x00}, ZVZZT8, PX_102_50, EXEC_ID}), 43);
  CHECK(tx.get_Symbol() == "ZVZZT" && tx.Price == 1025000u && base36(tx.ExecutionId, 9) == "0AAP09VEC");

  // US, Trade Break Message Example.
  CHECK(base36(one<pitch::TradeBreak>(cat({{0x0E, 0x2C}, TO_447000, EXEC_ID}), 14).ExecutionId, 9) == "0AAP09VEC");
}

static void status_and_auctions()
{
  // US, Trading Status Message (Equities) Message Example.
  auto st = one<pitch::TradingStatusMessage>(cat({{0x12, 0x31}, TO_447000, ZVZZT8, {0x54, 0x30, 0x20, 0x20}}), 18);
  CHECK(st.get_Symbol() == "ZVZZT" && st.TradingStatus == 'T' && st.RegSHOAction == '0');

  // US, Auction Update Message Example (BZX Equities).
  auto au = one<pitch::AuctionUpdate>(
      cat({{0x2F, 0x95}, TO_447000, ZVZZT8, {0x49}, PX_102_50, {0xF8, 0x24, 0x01, 0x00}, {0x20, 0x4E, 0x00, 0x00}, PX_102_50, PX_102_50}), 47);
  CHECK(au.AuctionType == 'I' && au.ReferencePrice == 1025000u && au.BuyShares == 75000u && au.SellShares == 20000u);
  CHECK(au.IndicativePrice == 1025000u && au.AuctionOnlyPrice == 1025000u);

  // US, Auction Update Message Example (BYX): Reference Prc bytes A4 0D 03 00..
  // (200100, $20.01) described as "n/a, set to zero"; decoded as sent.
  auto ab = one<pitch::AuctionUpdate>(
      cat({{0x2F, 0x95}, TO_447000, ZVZZT8, {0x50}, {0xA4, 0x0D, 0x03, 0, 0, 0, 0, 0}, {0xF4, 0x01, 0, 0}, {0xF4, 0x01, 0, 0},
           bytes(8, 0), bytes(8, 0)}),
      47);
  CHECK(ab.AuctionType == 'P' && ab.ReferencePrice == 200100u && ab.BuyShares == 500u && ab.IndicativePrice == 0u);

  // US, Auction Summary Message Example. The example's symbol bytes are
  // 30 30 6D 45 56 5F ("00mEV_"), described as 00mEVO.
  auto as = one<pitch::AuctionSummary>(
      cat({{0x1B, 0x96}, TO_447000, {0x30, 0x30, 0x6D, 0x45, 0x56, 0x5F, 0x20, 0x20}, {0x4F}, PX_102_50, {0x4B, 0, 0, 0}}), 27);
  CHECK(as.get_StockSymbol() == "00mEV_" && as.AuctionType == 'O' && as.Price == 1025000u && as.Shares == 75u);

  // US, Retail Price Improvement Message Example.
  auto rpi = one<pitch::RetailPriceImprovementMessage>(cat({{0x0F, 0x98}, TO_447000, ZVZZT8, {0x41}}), 15);
  CHECK(rpi.get_Symbol() == "ZVZZT" && rpi.RetailPriceImprovement == 'A');
}

static void unknown_and_partial()
{
  // Transaction Begin (options only, not tabled): unknown, skipped by its length.
  bytes tb = cat({{0x06, 0xBC}, TO_447000});
  auto u = pitch::decode(cp(tb), tb.size());
  CHECK(u.st == cboe::status::unknown && u.size == 6);
  // Trading Status in its options layout has the same code and length as the
  // equities one: it decodes, with the options fields in the wrong places.
  // Options and equities units are on different feeds (US, Multicast PITCH Feed
  // Descriptions), so the handler must know which it reads.
  // A length shorter than the fixed part is malformed and gives the size to skip.
  bytes shortdel = cat({{0x0D, 0x29}, TO_447000, ORDER_ID});
  auto m = pitch::decode(cp(shortdel), shortdel.size());
  CHECK(m.st == cboe::status::malformed && m.size == 13);
  // A buffer that ends inside a message is incomplete.
  bytes del = cat({{0x0E, 0x29}, TO_447000, ORDER_ID});
  CHECK(pitch::decode(cp(del), 10).st == cboe::status::incomplete);
}

static void gap_request_proxy()
{
  namespace to = cboe::grp::to_cboe;
  namespace from = cboe::grp::from_cboe;
  char buf[64];

  // US, Login Message Example.
  to::Login l{};
  l.set_SessionSubId("0001");
  l.set_Username("FIRM");
  l.set_Filler("");
  l.set_Password("ABCD00");
  const bytes login = {0x16, 0x01, 0x30, 0x30, 0x30, 0x31, 0x46, 0x49, 0x52, 0x4D, 0x20, 0x20,
                       0x41, 0x42, 0x43, 0x44, 0x30, 0x30, 0x20, 0x20, 0x20, 0x20};
  CHECK(to::encode(buf, sizeof buf, l) == 22 && std::memcmp(buf, login.data(), 22) == 0);

  // US, Gap Request Message Example.
  to::GapRequest g{};
  g.Unit = 1;
  g.Sequence = 4155;
  g.Count = 50;
  const bytes gap = {0x09, 0x03, 0x01, 0x3B, 0x10, 0x00, 0x00, 0x32, 0x00};
  CHECK(to::encode(buf, sizeof buf, g) == 9 && std::memcmp(buf, gap.data(), 9) == 0);

  // The client wraps each message in an unsequenced unit header.
  cboe::unit::to_cboe::SequencedUnitHeader h{};
  h.HdrLength = uint16_t(8 + 9);
  h.HdrCount = 1;
  CHECK(cboe::unit::to_cboe::encode(buf, sizeof buf, h) == 8 && buf[0] == 17 && buf[2] == 1 && buf[3] == 0);

  // US, Login Response and Gap Response Message Examples.
  const bytes lr = {0x03, 0x02, 0x41};
  auto d = from::decode(cp(lr), lr.size());
  CHECK(d && d.size == 3 && std::get<from::LoginResponse>(d.msg).Status == 'A');
  const bytes gr = {0x0A, 0x04, 0x01, 0x3B, 0x10, 0x00, 0x00, 0x32, 0x00, 0x41};
  auto e = from::decode(cp(gr), gr.size());
  CHECK(e && e.size == 10);
  const auto &r = std::get<from::GapResponse>(e.msg);
  CHECK(r.Unit == 1 && r.Sequence == 4155u && r.Count == 50 && r.Status == 'A');
}

static void spin_server()
{
  namespace to = cboe::spin::to_cboe;
  namespace from = cboe::spin::from_cboe;
  char buf[64];

  // US, Spin Request and Instrument Definition Request Message Examples.
  to::SpinRequest q{};
  q.Sequence = 4155;
  const bytes sr = {0x06, 0x81, 0x3B, 0x10, 0x00, 0x00};
  CHECK(to::encode(buf, sizeof buf, q) == 6 && std::memcmp(buf, sr.data(), 6) == 0);
  to::InstrumentDefinitionRequest idr{};
  const bytes idq = {0x06, 0x84, 0, 0, 0, 0};
  CHECK(to::encode(buf, sizeof buf, idr) == 6 && std::memcmp(buf, idq.data(), 6) == 0);

  // A spin stream: unsequenced headers carrying Spin Image Available, Spin
  // Response, an Add Order from the spin (a PITCH message), Spin Finished, then
  // Instrument Definition Response and Finished. Examples from US, Example
  // Messages; the Add Order is the Add Order - Long example.
  const bytes sia = {0x06, 0x80, 0x3B, 0x10, 0x00, 0x00};
  const bytes rsp = {0x0B, 0x82, 0x3B, 0x10, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x41};
  const bytes add = cat({{0x22, 0x21}, TO_447000, ORDER_ID, {0x42, 0x20, 0x4E, 0x00, 0x00}, ZVZZT6, {0x5A, 0x23, 0, 0, 0, 0, 0, 0}, {0x01}});
  const bytes fin = {0x06, 0x83, 0x3B, 0x10, 0x00, 0x00};
  const bytes idresp = {0x0B, 0x85, 0, 0, 0, 0, 0xB8, 0x0B, 0x00, 0x00, 0x41};
  const bytes idfin = {0x02, 0x86};
  auto hdr = [](size_t body, uint8_t count) {
    return bytes{uint8_t(8 + body), uint8_t((8 + body) >> 8), count, 0, 0, 0, 0, 0};
  };
  bytes stream = cat({hdr(sia.size(), 1), sia, hdr(rsp.size(), 1), rsp, hdr(add.size(), 1), add, hdr(fin.size(), 1), fin,
                      hdr(idresp.size() + idfin.size(), 2), idresp, idfin});

  // Read the stream as a client would: header, then HdrCount messages, each
  // tried against the spin layer and then the PITCH layer.
  const char *p = cp(stream);
  const char *end = p + stream.size();
  std::vector<std::string> seen;
  uint32_t order_count = 0, add_count = 0;
  while (p < end)
  {
    cboe::unit::from_cboe::SequencedUnitHeader h;
    std::memcpy(&h, p, sizeof h);
    CHECK(h.HdrUnit == 0 && h.HdrSequence == 0);
    const char *m = p + sizeof h;
    for (unsigned i = 0; i < h.HdrCount; ++i)
    {
      size_t left = size_t(p + h.HdrLength - m);
      auto s = from::decode(m, left);
      if (s)
      {
        seen.push_back(from::name(s.msg));
        if (auto *r = std::get_if<from::SpinResponse>(&s.msg))
        {
          CHECK(r->Sequence == 4155u && r->Status == 'A');
          order_count = r->OrderCount;
        }
        if (auto *r = std::get_if<from::InstrumentDefinitionResponse>(&s.msg))
          CHECK(r->InstrumentCount == 3000u && r->Status == 'A');
        m += s.size;
        continue;
      }
      CHECK(s.st == cboe::status::unknown);
      auto d = pitch::decode(m, left);
      CHECK(d);
      seen.push_back(pitch::name(d.msg));
      add_count += std::holds_alternative<pitch::AddOrderLong>(d.msg);
      m += d.size;
    }
    CHECK(m == p + h.HdrLength);
    p += h.HdrLength;
  }
  CHECK(order_count == 66 && add_count == 1);
  const std::vector<std::string> want = {"SpinImageAvailable", "SpinResponse", "AddOrderLong", "SpinFinished",
                                         "InstrumentDefinitionResponse", "InstrumentDefinitionFinished"};
  CHECK(seen == want);
}

int main()
{
  sequenced_unit_with_two_messages();
  time_messages();
  orders();
  trades();
  status_and_auctions();
  unknown_and_partial();
  gap_request_proxy();
  spin_server();
  std::cout << "cboe_decode_test: " << checks << " checks OK\n";
}
