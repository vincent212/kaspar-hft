// Decode Japannext ITCH, MoldUDP64 and GLIMPSE/SoupBinTCP bytes through the headers
// generated from itch.tbl, moldudp64.tbl and glimpse.tbl. Byte sequences are written
// from the field tables of JNX ITCH 2.02, JNX MoldUDP64 1.02, JNX SoupBinTCP 1.01 and
// JNX GLIMPSE 2.01 (none of which gives examples), except those marked as taken from
// Japannext's public sample capture Japannext_PTS_GLIMPSE_Equities_v1.4.pcap.
#include "glimpse.hpp"
#include "itch.hpp"
#include "moldudp64.hpp"
#include "../../test_support.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace jnx;
namespace I = jnx::itch::from_jnx;
namespace M = jnx::moldudp64;
namespace S = jnx::soup;
namespace G = jnx::glimpse::from_jnx;

using namespace test_support;

static void itch_messages()
{
  // T: Timestamp - Seconds 27004 (07:30:04), 4.1
  bytes t = {'T', 0x00, 0x00, 0x69, 0x7C};
  auto dt = I::decode(cp(t), t.size());
  CHECK(dt.size == 5 && msg_as<I::TimestampSecondsMessage>(dt).TimestampSeconds == 27004u);

  // S: system-wide Start of System Hours, 4.2
  bytes s = {'S', 0x00, 0x00, 0x01, 0x00, ' ', ' ', ' ', ' ', 'S'};
  auto ds = I::decode(cp(s), s.size());
  const auto se = msg_as<I::SystemEventMessage>(ds);
  CHECK(ds.size == 10 && se.TimestampNanoseconds == 256u && se.get_Group().empty() && se.SystemEvent == 'S');

  // L: table 37, tick 5 (0.5 JPY) from 3000.0 JPY, 4.3
  bytes l = {'L', 0, 0, 0, 0, 0, 0, 0, 37, 0, 0, 0, 5, 0x00, 0x00, 0x75, 0x30};
  auto dl = I::decode(cp(l), l.size());
  const auto pl = msg_as<I::PriceTickSizeMessage>(dl);
  CHECK(dl.size == 17 && pl.PriceTickSizeTableId == 37u && pl.PriceTickSize == 5u && pl.PriceStart == 30000u);

  // R: 7203 JP3633400001 NGHT, lot 100, table 58, 1 decimal, limits 3500.0 / 2500.0, 4.4
  bytes r = {'R', 0, 0, 0, 9, '7', '2', '0', '3', 'J', 'P', '3', '6', '3', '3', '4', '0', '0', '0', '0', '1',
             'N', 'G', 'H', 'T', 0, 0, 0, 100, 0, 0, 0, 58, 0, 0, 0, 1, 0x00, 0x00, 0x88, 0xB8, 0x00, 0x00, 0x61, 0xA8};
  auto dr = I::decode(cp(r), r.size());
  const auto od = msg_as<I::OrderbookDirectoryMessage>(dr);
  CHECK(dr.size == 45 && od.TimestampNanoseconds == 9u && od.get_OrderbookId() == "7203");
  CHECK(od.get_OrderbookCode() == "JP3633400001" && od.get_Group() == "NGHT" && od.RoundLotSize == 100u);
  CHECK(od.PriceTickSizeTableId == 58u && od.PriceDecimals == 1u && od.UpperPriceLimit == 35000u && od.LowerPriceLimit == 25000u);

  // H: 7203 DAY suspended, 4.5
  bytes h = {'H', 0, 0, 0, 1, '7', '2', '0', '3', 'D', 'A', 'Y', ' ', 'V'};
  auto dh = I::decode(cp(h), h.size());
  const auto ts = msg_as<I::TradingStateMessage>(dh);
  CHECK(ts.get_OrderbookId() == "7203" && ts.get_Group() == "DAY" && ts.TradingState == 'V');

  // Y: 7203 DAYX restriction in effect, 4.6
  bytes y = {'Y', 0, 0, 0, 2, '7', '2', '0', '3', 'D', 'A', 'Y', 'X', '1'};
  auto dy = I::decode(cp(y), y.size());
  const auto ss = msg_as<I::ShortSellingPriceRestrictionStateMessage>(dy);
  CHECK(ss.get_Group() == "DAYX" && ss.ShortSellingState == '1');

  // A: order 0x0102030405060708, buy 300 of 7203 DAY at 2875.5 JPY (28755), 4.7.1
  bytes a = {'A', 0x3B, 0x9A, 0xC9, 0xFF, 1, 2, 3, 4, 5, 6, 7, 8, 'B', 0, 0, 0x01, 0x2C,
             '7', '2', '0', '3', 'D', 'A', 'Y', ' ', 0x00, 0x00, 0x70, 0x53};
  auto da = I::decode(cp(a), a.size());
  const auto oa = msg_as<I::OrderAddedMessage>(da);
  CHECK(da.size == 30 && oa.TimestampNanoseconds == 999999999u && oa.OrderNumber == 0x0102030405060708ull);
  CHECK(oa.BuySellIndicator == 'B' && oa.Quantity == 300u && oa.get_OrderbookId() == "7203" && oa.get_Group() == "DAY");
  CHECK(oa.Price == 28755u);

  // A with Order Number 0: a reference price; 0x7FFFFFFF means none available, 4.7.1
  bytes ar = {'A', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ' ', 0, 0, 0, 0,
              '7', '2', '0', '3', 'D', 'A', 'Y', ' ', 0x7F, 0xFF, 0xFF, 0xFF};
  auto dar = I::decode(cp(ar), ar.size());
  const auto rp = msg_as<I::OrderAddedMessage>(dar);
  CHECK(rp.OrderNumber == 0u && rp.Price == 0x7FFFFFFFu);

  // F: sell 100 at 2876.0, blank Attribution, DLP order, 4.7.2
  bytes f = {'F', 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0x10, 0x00, 'S', 0, 0, 0, 100,
             '7', '2', '0', '3', 'D', 'A', 'Y', 'U', 0x00, 0x00, 0x70, 0x58, ' ', ' ', ' ', ' ', 'Q'};
  auto df = I::decode(cp(f), f.size());
  const auto of = msg_as<I::OrderAddedWithAttributesMessage>(df);
  CHECK(df.size == 35 && of.OrderNumber == 4096u && of.BuySellIndicator == 'S' && of.Quantity == 100u);
  CHECK(of.get_Group() == "DAYU" && of.Price == 28760u && of.get_Attribution().empty() && of.OrderType == 'Q');

  // E: 100 of order 0x0102030405060708 executed, match 77, 4.8
  bytes e = {'E', 0, 0, 0, 4, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 77};
  auto de = I::decode(cp(e), e.size());
  const auto oe = msg_as<I::OrderExecutedMessage>(de);
  CHECK(de.size == 25 && oe.OrderNumber == 0x0102030405060708ull && oe.ExecutedQuantity == 100u && oe.MatchNumber == 77u);

  // D: order 4096 deleted, 4.9
  bytes d = {'D', 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0x10, 0x00};
  auto dd = I::decode(cp(d), d.size());
  CHECK(dd.size == 13 && msg_as<I::OrderDeletedMessage>(dd).OrderNumber == 4096u);

  // U: order 4096 replaced by 4097, 200 at 2877.0, 4.10
  bytes u = {'U', 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0x10, 0x00, 0, 0, 0, 0, 0, 0, 0x10, 0x01, 0, 0, 0, 200, 0x00, 0x00, 0x70, 0x62};
  auto du = I::decode(cp(u), u.size());
  const auto ou = msg_as<I::OrderReplacedMessage>(du);
  CHECK(du.size == 29 && ou.OriginalOrderNumber == 4096u && ou.NewOrderNumber == 4097u && ou.Quantity == 200u && ou.Price == 28770u);

  // A buffer that ends inside a message is incomplete; a type the table does not list
  // is unknown, with no size (the MoldUDP64 block length gives it).
  CHECK(I::decode(cp(a), 29).st == status::incomplete);
  bytes x = {'X', 0, 0};
  auto dx = I::decode(cp(x), x.size());
  CHECK(dx.st == status::unknown && dx.size == 0);
  // End of Snapshot is a GLIMPSE message, not an ITCH one.
  bytes g = {'G', 0, 0, 0, 0, 0, 0, 0, 1};
  CHECK(I::decode(cp(g), g.size()).st == status::unknown);
}

static void moldudp64_packet()
{
  // Header: session "1697659284", sequence 1000, 2 messages; then two Message Blocks
  // (T, A). JNX MoldUDP64 1.02, 3.1-3.2.
  bytes p = {'1', '6', '9', '7', '6', '5', '9', '2', '8', '4', 0, 0, 0, 0, 0, 0, 0x03, 0xE8, 0, 2,
             0, 5, 'T', 0, 0, 0x69, 0x7C,
             0, 30, 'A', 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 42, 'S', 0, 0, 0x03, 0xE8,
             '9', '9', '8', '4', 'D', 'A', 'Y', ' ', 0x00, 0x00, 0x27, 0x10};
  M::from_jnx::DownstreamPacketHeader h;
  std::memcpy(&h, p.data(), sizeof h);
  CHECK(sizeof h == 20 && std::string_view(h.Session, 10) == "1697659284" && h.SequenceNumber == 1000u && h.MessageCount == 2);

  std::vector<std::string> seen;
  uint64_t seq = h.SequenceNumber;
  size_t pos = sizeof h;
  for (uint16_t i = 0; i < h.MessageCount; ++i, ++seq)
  {
    M::from_jnx::MessageBlock b;
    std::memcpy(&b, p.data() + pos, sizeof b);
    const size_t len = uint16_t(b.MessageLength);
    auto d = I::decode(cp(p) + pos + sizeof b, len);
    CHECK(d && d.size == len);   // the block length equals the message's fixed size
    seen.push_back(I::name(d.msg));
    if (i == 1)
    {
      const auto &a = std::get<I::OrderAddedMessage>(d.msg);
      CHECK(seq == 1001 && a.OrderNumber == 42u && a.BuySellIndicator == 'S' && a.Quantity == 1000u);
      CHECK(a.get_OrderbookId() == "9984" && a.Price == 10000u);
    }
    pos += sizeof b + len;
  }
  CHECK(pos == p.size() && seen == (std::vector<std::string>{"TimestampSecondsMessage", "OrderAddedMessage"}));

  // Heartbeat (count 0) and End of Session (count 0xFFFF) carry the next expected sequence number.
  bytes hb = {'1', '6', '9', '7', '6', '5', '9', '2', '8', '4', 0, 0, 0, 0, 0, 0, 0x31, 0x12, 0xFF, 0xFF};
  std::memcpy(&h, hb.data(), sizeof h);
  CHECK(h.MessageCount == 0xFFFF && h.SequenceNumber == 0x3112u);

  // Request Packet for 5 messages from 1001, 4.
  M::to_jnx::RequestPacket rq{};
  std::memcpy(rq.Session, "1697659284", 10);
  rq.SequenceNumber = 1001;
  rq.RequestedMessageCount = 5;
  char buf[32];
  CHECK(M::to_jnx::encode(buf, sizeof buf, rq) == 20);
  const unsigned char want[20] = {'1', '6', '9', '7', '6', '5', '9', '2', '8', '4', 0, 0, 0, 0, 0, 0, 0x03, 0xE9, 0, 5};
  CHECK(std::memcmp(buf, want, 20) == 0);
}

static void glimpse_session()
{
  // Login Request: Requested Session must be blank for GLIMPSE (JNX GLIMPSE 2.01, 4);
  // JNX SoupBinTCP 1.01 5.3.1 offsets 3, 9, 19, 29 including the 2-byte length.
  S::to_jnx::LoginRequestPacket lr{};
  lr.set_Username("user01");
  lr.set_Password("pw");
  lr.set_RequestedSession("");
  lr.set_RequestedSequenceNumber("1");
  char out[64];
  CHECK(S::to_jnx::encode(out, sizeof out, lr) == 49);
  CHECK(out[0] == 0 && out[1] == 47 && out[2] == 'L' && std::string_view(out + 3, 6) == "user01");
  CHECK(std::string_view(out + 9, 10) == "pw        " && std::string_view(out + 19, 10) == std::string(10, ' '));
  CHECK(std::string_view(out + 29, 20) == std::string(19, ' ') + "1");
  S::to_jnx::ClientHeartbeatPacket chb{};
  CHECK(S::to_jnx::encode(out, sizeof out, chb) == 3 && out[1] == 1 && out[2] == 'R');

  // Server side. The first three packets are taken from the sample capture: Login
  // Accepted with a blank session and next sequence 1, a Timestamp - Seconds of 34772
  // (09:39:32) and the directory entry of 1301 (JP3257200000, lot 100, table 37,
  // limits 4445.0 / 3045.0). Then a reference price, a resting order, End of Snapshot
  // (ITCH sequence 3168301) and End of Session, written from the tables.
  bytes st = {
      0x00, 0x1f, 0x41, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
      ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '1',
      0x00, 0x06, 0x53, 0x54, 0x00, 0x00, 0x87, 0xd4,
      0x00, 0x2e, 0x53, 0x52, 0x01, 0x99, 0x16, 0x90, 0x31, 0x33, 0x30, 0x31, 0x4a, 0x50, 0x33, 0x32, 0x35, 0x37, 0x32,
      0x30, 0x30, 0x30, 0x30, 0x30, 0x44, 0x41, 0x59, 0x20, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0xad, 0xa2, 0x00, 0x00, 0x76, 0xf2,
      0, 31, 'S', 'A', 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, ' ', 0, 0, 0, 0, '1', '3', '0', '1', 'D', 'A', 'Y', ' ', 0x00, 0x00, 0x92, 0x4A,
      0, 31, 'S', 'A', 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0x30, 0x39, 'B', 0, 0, 0x01, 0xF4, '1', '3', '0', '1', 'D', 'A', 'Y', ' ', 0x00, 0x00, 0x92, 0x40,
      0, 10, 'S', 'G', 0, 0, 0, 0, 0, 0x30, 0x58, 0x2D,
      0, 1, 'Z'};
  std::vector<std::string> names;
  uint64_t resume = 0;
  size_t pos = 0, sequenced = 0;
  while (pos < st.size())
  {
    auto d = S::from_jnx::decode(cp(st) + pos, st.size() - pos);
    CHECK(d);
    if (auto *la = std::get_if<S::from_jnx::LoginAcceptedPacket>(&d.msg))
      CHECK(la->get_Session().empty() && la->get_SequenceNumber() == "1" && d.size == 33);
    if (std::holds_alternative<S::from_jnx::SequencedDataPacket>(d.msg))
    {
      ++sequenced;
      auto g = G::decode(d.tail, d.tail_len);
      CHECK(g && g.size == d.tail_len);
      names.push_back(G::name(g.msg));
      if (auto *t = std::get_if<G::TimestampSecondsMessage>(&g.msg))
        CHECK(t->TimestampSeconds == 34772u);
      if (auto *r = std::get_if<G::OrderbookDirectoryMessage>(&g.msg))
      {
        CHECK(r->TimestampNanoseconds == 0x01991690u && r->get_OrderbookId() == "1301" && r->get_OrderbookCode() == "JP3257200000");
        CHECK(r->get_Group() == "DAY" && r->RoundLotSize == 100u && r->PriceTickSizeTableId == 37u && r->PriceDecimals == 1u);
        CHECK(r->UpperPriceLimit == 44450u && r->LowerPriceLimit == 30450u);
      }
      if (auto *a = std::get_if<G::OrderAddedMessage>(&g.msg); a && a->OrderNumber == 0)
        CHECK(a->Price == 37450u);
      if (auto *a = std::get_if<G::OrderAddedMessage>(&g.msg); a && a->OrderNumber != 0)
        CHECK(a->OrderNumber == 12345u && a->BuySellIndicator == 'B' && a->Quantity == 500u && a->Price == 37440u);
      if (auto *e = std::get_if<G::EndOfSnapshotMessage>(&g.msg))
        resume = e->SequenceNumber;
    }
    names.push_back(S::from_jnx::name(d.msg));
    pos += d.size;
  }
  CHECK(pos == st.size() && sequenced == 5 && resume == 3168301u);
  CHECK(names.back() == "EndOfSessionPacket");

  // GLIMPSE carries no executions, deletes or replaces.
  bytes e = {'E', 0, 0, 0, 4, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 77};
  CHECK(G::decode(cp(e), e.size()).st == status::unknown);

  // A login rejection, and a packet split across TCP reads.
  bytes rj = {0, 2, 'J', 'S'};
  auto drj = S::from_jnx::decode(cp(rj), rj.size());
  CHECK(msg_as<S::from_jnx::LoginRejectedPacket>(drj).RejectReasonCode == 'S');
  CHECK(S::from_jnx::decode(cp(st), 20).st == status::incomplete);
}

int main()
{
  itch_messages();
  moldudp64_packet();
  glimpse_session();
  std::cout << "jnx decode OK\n";
}
