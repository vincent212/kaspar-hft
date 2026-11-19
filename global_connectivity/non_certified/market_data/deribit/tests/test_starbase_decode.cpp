/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Starbase framing and the generated codecs: bytes laid out by hand from the
// documentation's tables (binary-api-reference "UDP messages",
// order-book-maintenance, trades), round trips through the encoder, and the
// first packets of Deribit's sample capture.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "deribit_md/Starbase.hpp"

using namespace deribit::starbase;

namespace
{
  template <class T>
  void put(std::vector<uint8_t> &b, size_t at, T v)
  {
    if (b.size() < at + sizeof v)
      b.resize(at + sizeof v);
    std::memcpy(&b[at], &v, sizeof v);
  }
}

// Packet header (24 bytes): sendTime int64, sequenceNum int64, channelId
// int32, type uint16, messageCount uint16. Message header (16 bytes):
// messageLength uint16 (header and body), messageTypeId uint16, version uint16,
// flags uint16, transactTime int64. Buy Put body: orderId, instrumentId,
// quantity, price (Price9), sortOrderId, all 8 bytes. Little-endian.
TEST(StarbaseDecode, BuyPutLaidOutFromTheDocumentation)
{
  std::vector<uint8_t> b;
  put<int64_t>(b, 0, 1777450694107489444);   // sendTime
  put<int64_t>(b, 8, 1000);                  // sequenceNum
  put<int32_t>(b, 16, 44853);                // channelId
  put<uint16_t>(b, 20, 1);                   // IncrementalUpdate
  put<uint16_t>(b, 22, 1);                   // messageCount
  put<uint16_t>(b, 24, 56);                  // messageLength = 16 + 40
  put<uint16_t>(b, 26, 20);                  // Buy Put
  put<uint16_t>(b, 28, 0);
  put<uint16_t>(b, 30, 3);                   // start and end of transaction
  put<int64_t>(b, 32, 1777450683052477607);  // transactTime
  put<int64_t>(b, 40, 175146917846581248);   // orderId
  put<int64_t>(b, 48, 557067);               // instrumentId
  put<int64_t>(b, 56, 25);                   // quantity mantissa
  put<int64_t>(b, 64, 9265000000);           // 9.265 at exponent -9
  put<int64_t>(b, 72, 32695605140289);       // sortOrderId

  PacketHeader ph;
  std::vector<Message> m;
  ASSERT_EQ(decode_packet(b.data(), b.size(), ph, m), Status::Ok);
  EXPECT_EQ(ph.seqNum, 1000);
  EXPECT_EQ(ph.channelId, 44853);
  EXPECT_EQ(ph.type, INCREMENTAL);
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].seq, 1000);
  EXPECT_TRUE(m[0].start());
  EXPECT_TRUE(m[0].end());
  EXPECT_EQ(m[0].transactTime, 1777450683052477607);
  auto *p = std::get_if<Put>(&m[0].body);
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(p->bid);
  EXPECT_EQ(p->orderId, 175146917846581248);
  EXPECT_EQ(p->instrumentId, 557067);
  EXPECT_EQ(p->qty, 25);
  EXPECT_EQ(p->price9, 9265000000);
  EXPECT_EQ(p->sortOrderId, 32695605140289);
}

// Trade Summary (30): ..., tradeCount int32, takerFlags uint32 (bit 0 isSell).
// Trade (31): makerOrderId 0x8000000000000000 for an implied or synthetic leg.
TEST(StarbaseDecode, TradesRoundTripAndFlags)
{
  PacketHeader ph{1, 77, 44849, INCREMENTAL, 0};
  Message s;
  s.flags = kStartOfTransaction;
  s.transactTime = 5;
  s.body = TradeSummary{210838, 9001, 30, 63105500000000, 63100000000000, 63101000000000, 2, 1};
  Message t1;
  t1.transactTime = 5;
  t1.body = Trade{1, 210838, 8001, 10, 63105000000000, 0};
  Message t2;
  t2.flags = kEndOfTransaction;
  t2.transactTime = 5;
  t2.body = Trade{2, 210838, kNullInt64, 20, 63105500000000, 0};
  const auto bytes = encode_packet(ph, {s, t1, t2});
  // TradeSummary 16 + 56 = 72; Trade 16 + 44 = 60, padded to 64.
  EXPECT_EQ(bytes.size(), 24u + 72 + 64 + 64);

  PacketHeader out;
  std::vector<Message> m;
  ASSERT_EQ(decode_packet(bytes.data(), bytes.size(), out, m), Status::Ok);
  ASSERT_EQ(m.size(), 3u);
  EXPECT_EQ(m[2].seq, 79);
  auto *ts = std::get_if<TradeSummary>(&m[0].body);
  ASSERT_NE(ts, nullptr);
  EXPECT_EQ(ts->takerFlags & 1, 1u);
  EXPECT_EQ(ts->tradeCount, 2);
  EXPECT_EQ(ts->deepestPrice9, 63105500000000);
  auto *tr = std::get_if<Trade>(&m[2].body);
  ASSERT_NE(tr, nullptr);
  EXPECT_EQ(tr->makerOrderId, kNullInt64);
  EXPECT_EQ(tr->qty, 20);
  EXPECT_TRUE(m[0].start());
  EXPECT_FALSE(m[1].start() || m[1].end());
  EXPECT_TRUE(m[2].end());
}

TEST(StarbaseDecode, InstrumentDefinitionWithGroupsRoundTrips)
{
  InstrumentDefinition d;
  d.instrumentId = 574893;
  d.name = "ETH-29APR26-2425-C";
  d.indexId = 2000033;
  d.quantityAsset = "ETH";
  d.priceAsset = "USD";
  d.expiryTime = INT64_MIN;
  d.strikePrice9 = 2425000000000;
  d.minOrderQuantity = 1;
  d.tickSize9 = 100000;
  d.quantityExponent = 0;
  d.type = 1;
  d.flags = 1;
  d.status = 0;
  d.largeTickSizes = {{500000, 5000000}};
  d.legs = {{127786, -1}, {557061, 1}};
  Message msg;
  msg.body = d;
  const auto bytes = encode_packet(PacketHeader{0, 1, 1, SNAPSHOT, 0}, {msg});
  PacketHeader ph;
  std::vector<Message> m;
  ASSERT_EQ(decode_packet(bytes.data(), bytes.size(), ph, m), Status::Ok);
  auto *o = std::get_if<InstrumentDefinition>(&m[0].body);
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->name, d.name);
  EXPECT_EQ(o->tickSize9, 100000);
  EXPECT_EQ(o->largeTickSizes, d.largeTickSizes);
  EXPECT_EQ(o->legs, d.legs);
  EXPECT_EQ(o->type, 1);
}

TEST(StarbaseDecode, MalformedPacketsAreRejected)
{
  PacketHeader ph;
  std::vector<Message> m;
  std::vector<uint8_t> b(10, 0);
  EXPECT_EQ(decode_packet(b.data(), b.size(), ph, m), Status::Short);

  Message del;
  del.flags = 3;
  del.body = Delete{false, 5, 6};
  auto bytes = encode_packet(PacketHeader{0, 1, 1, INCREMENTAL, 0}, {del, del});
  // The second message cut short.
  m.clear();
  EXPECT_EQ(decode_packet(bytes.data(), bytes.size() - 8, ph, m), Status::Truncated);
  EXPECT_EQ(m.size(), 1u);
  // A messageLength smaller than the header.
  put<uint16_t>(bytes, 24, 8);
  m.clear();
  EXPECT_EQ(decode_packet(bytes.data(), bytes.size(), ph, m), Status::BadLength);
  // A body shorter than the template's block (Delete is 16 bytes).
  put<uint16_t>(bytes, 24, 24);
  m.clear();
  EXPECT_EQ(decode_packet(bytes.data(), bytes.size(), ph, m), Status::BadLength);
}

TEST(StarbaseDecode, HeartbeatHasNoMessages)
{
  const auto bytes = encode_packet(PacketHeader{9, 4461, 44850, INCREMENTAL, 0}, {});
  PacketHeader ph;
  std::vector<Message> m;
  ASSERT_EQ(decode_packet(bytes.data(), bytes.size(), ph, m), Status::Ok);
  EXPECT_EQ(ph.messageCount, 0);
  EXPECT_EQ(ph.seqNum, 4461);
  EXPECT_TRUE(m.empty());
}

TEST(StarbaseDecode, UnknownTemplateIsSkippedByLength)
{
  Message del;
  del.flags = 3;
  del.body = Delete{true, 1, 2};
  auto bytes = encode_packet(PacketHeader{0, 1, 1, INCREMENTAL, 0}, {del, del});
  put<uint16_t>(bytes, 26, 999);   // the first message's template id
  PacketHeader ph;
  std::vector<Message> m;
  ASSERT_EQ(decode_packet(bytes.data(), bytes.size(), ph, m), Status::Ok);
  ASSERT_EQ(m.size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Unknown>(m[0].body));
  EXPECT_TRUE(std::holds_alternative<Delete>(m[1].body));
}

// The first packet of Deribit's sample capture, as the SDK decodes it
// (`make sdk-check` compares all 51,051 lines).
TEST(StarbaseDecode, FirstPacketOfTheSampleCapture)
{
  std::ifstream f(DERIBIT_PCAP, std::ios::binary);
  if (!f)
    GTEST_SKIP() << "sample capture not downloaded (make docs)";
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  // pcap header 24, record header 16, Ethernet 14, IPv4 20, UDP 8.
  const uint8_t *udp = d.data() + 24 + 16 + 14 + 20 + 8;
  PacketHeader ph;
  std::vector<Message> m;
  ASSERT_EQ(decode_packet(udp, 408, ph, m), Status::Ok);
  EXPECT_EQ(dump_packet_header(ph), "P 1777450694107489444 45441225 44852 2 3");
  ASSERT_EQ(m.size(), 3u);
  auto *h = std::get_if<SnapshotBound>(&m[0].body);
  ASSERT_NE(h, nullptr);
  EXPECT_EQ(h->instrumentId, 574893);
  EXPECT_EQ(h->incrementalSeqNum, 2050);
  auto *def = std::get_if<InstrumentDefinition>(&m[1].body);
  ASSERT_NE(def, nullptr);
  EXPECT_EQ(def->name, "ETH-29APR26-2425-C");
  EXPECT_EQ(m[1].length, 304);
  auto *t = std::get_if<SnapshotBound>(&m[2].body);
  ASSERT_NE(t, nullptr);
  EXPECT_FALSE(t->header);
}
