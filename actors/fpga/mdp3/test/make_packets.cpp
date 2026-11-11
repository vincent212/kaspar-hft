/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Test packets for the decoder, encoded with CME's real-logic codec
// (mdp3_sbe/): random but valid MDP3 packets mixing the templates the card
// decodes (46 MBP book, 48 trades, 30 status, 4 channel reset) with ones it
// must skip (47 MBO, 49 statistics), MBO order entries inside 46 and 48, and
// empty groups. Until we have recorded packets, these exercise every path of
// the walk; recorded packets replace them later.
//
//   make_packets <count> <seed> packets.bin

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "mdp3_sbe/ChannelReset4.h"
#include "mdp3_sbe/MDIncrementalRefreshBook46.h"
#include "mdp3_sbe/MDIncrementalRefreshDailyStatistics49.h"
#include "mdp3_sbe/MDIncrementalRefreshOrderBook47.h"
#include "mdp3_sbe/MDIncrementalRefreshTradeSummary48.h"
#include "mdp3_sbe/MessageHeader.h"
#include "mdp3_sbe/SecurityStatus30.h"

namespace {

std::mt19937_64 rng;

uint64_t any(uint64_t n) { return rng() % n; }

constexpr size_t kBuf = 1400;   // a CME UDP payload is at most about 1.4 KB

// Writes the message header and returns the offset of the message body.
template <class M>
size_t header(char *buf, size_t at)
{
  sbe::MessageHeader h;
  h.wrap(buf, at + 2, 0, kBuf)
      .blockLength(M::sbeBlockLength())
      .templateId(M::sbeTemplateId())
      .schemaId(M::sbeSchemaId())
      .version(M::sbeSchemaVersion());
  return at + 10;
}

void set_size(char *buf, size_t at, size_t body_len)
{
  const uint16_t n = static_cast<uint16_t>(10 + body_len);
  std::memcpy(buf + at, &n, 2);
}

const sbe::MDEntryTypeBook::Value kBookTypes[] = {sbe::MDEntryTypeBook::Bid, sbe::MDEntryTypeBook::Offer,
                                                   sbe::MDEntryTypeBook::ImpliedBid,
                                                   sbe::MDEntryTypeBook::ImpliedOffer};

size_t book46(char *buf, size_t at, size_t room)
{
  const int n = static_cast<int>(any(9));        // 0 to 8 MBP entries
  const int orders = static_cast<int>(any(4));   // 0 to 3 MBO order entries
  if (static_cast<size_t>(11 + 3 + 32 * n + 8 + 24 * orders + 10) > room)
    return 0;
  sbe::MDIncrementalRefreshBook46 m;
  m.wrapForEncode(buf, header<sbe::MDIncrementalRefreshBook46>(buf, at), kBuf);
  m.transactTime(rng());
  m.matchEventIndicator().clear().endOfEvent(any(2) != 0).lastQuoteMsg(any(2) != 0);
  auto &g = m.noMDEntriesCount(static_cast<uint8_t>(n));
  for (int i = 0; i < n; ++i)
  {
    g.next();
    g.mDEntryPx().mantissa(any(4) ? static_cast<int64_t>(4500000000000 + any(100000) * 250000000)
                                  : INT64_MAX);   // sometimes null
    g.mDEntrySize(any(5) ? static_cast<int32_t>(any(5000)) : INT32_MAX)
        .securityID(static_cast<int32_t>(any(50000)))
        .rptSeq(static_cast<uint32_t>(rng()))
        .numberOfOrders(static_cast<int32_t>(any(300)))
        .mDPriceLevel(static_cast<uint8_t>(1 + any(10)))
        .mDUpdateAction(static_cast<sbe::MDUpdateAction::Value>(any(3)))
        .mDEntryType(kBookTypes[any(4)])
        .tradeableSize(static_cast<int32_t>(any(5000)));
  }
  auto &o = m.noOrderIDEntriesCount(static_cast<uint8_t>(orders));
  for (int i = 0; i < orders; ++i)
  {
    o.next();
    o.orderID(rng()).mDOrderPriority(rng()).mDDisplayQty(static_cast<int32_t>(any(100)))
        .referenceID(static_cast<uint8_t>(1 + any(n ? n : 1)))
        .orderUpdateAction(static_cast<sbe::OrderUpdateAction::Value>(any(3)));
  }
  set_size(buf, at, m.encodedLength());
  return 10 + m.encodedLength();
}

size_t trade48(char *buf, size_t at, size_t room)
{
  const int n = static_cast<int>(1 + any(5));
  const int fills = static_cast<int>(any(6));
  if (static_cast<size_t>(11 + 3 + 32 * n + 8 + 16 * fills + 10) > room)
    return 0;
  sbe::MDIncrementalRefreshTradeSummary48 m;
  m.wrapForEncode(buf, header<sbe::MDIncrementalRefreshTradeSummary48>(buf, at), kBuf);
  m.transactTime(rng());
  m.matchEventIndicator().clear().endOfEvent(any(2) != 0).lastTradeMsg(true);
  auto &g = m.noMDEntriesCount(static_cast<uint8_t>(n));
  for (int i = 0; i < n; ++i)
  {
    g.next();
    g.mDEntryPx().mantissa(static_cast<int64_t>(4500000000000 + any(100000) * 250000000));
    g.mDEntrySize(static_cast<int32_t>(1 + any(500)))
        .securityID(static_cast<int32_t>(any(50000)))
        .rptSeq(static_cast<uint32_t>(rng()))
        .numberOfOrders(static_cast<int32_t>(1 + any(20)))
        .aggressorSide(static_cast<sbe::AggressorSide::Value>(any(3)))
        .mDUpdateAction(sbe::MDUpdateAction::New)
        .mDTradeEntryID(static_cast<uint32_t>(rng()));
  }
  auto &o = m.noOrderIDEntriesCount(static_cast<uint8_t>(fills));
  for (int i = 0; i < fills; ++i)
  {
    o.next();
    o.orderID(rng()).lastQty(static_cast<int32_t>(any(100)));
  }
  set_size(buf, at, m.encodedLength());
  return 10 + m.encodedLength();
}

size_t status30(char *buf, size_t at, size_t room)
{
  if (static_cast<size_t>(30 + 10) > room)
    return 0;
  sbe::SecurityStatus30 m;
  m.wrapForEncode(buf, header<sbe::SecurityStatus30>(buf, at), kBuf);
  m.transactTime(rng()).putSecurityGroup("ES    ").putAsset("ES    ");
  m.securityID(static_cast<int32_t>(any(50000))).tradeDate(static_cast<uint16_t>(any(30000)));
  m.matchEventIndicator().clear().endOfEvent(true);
  const sbe::SecurityTradingStatus::Value st[] = {sbe::SecurityTradingStatus::TradingHalt,
                                                  sbe::SecurityTradingStatus::Close,
                                                  sbe::SecurityTradingStatus::ReadyToTrade,
                                                  sbe::SecurityTradingStatus::PreOpen};
  m.securityTradingStatus(st[any(4)])
      .haltReason(static_cast<sbe::HaltReason::Value>(any(6)))
      .securityTradingEvent(any(2) ? sbe::SecurityTradingEvent::NoEvent : sbe::SecurityTradingEvent::NoCancel);
  set_size(buf, at, m.encodedLength());
  return 10 + m.encodedLength();
}

size_t reset4(char *buf, size_t at, size_t room)
{
  if (static_cast<size_t>(9 + 3 + 2 + 10) > room)
    return 0;
  sbe::ChannelReset4 m;
  m.wrapForEncode(buf, header<sbe::ChannelReset4>(buf, at), kBuf);
  m.transactTime(rng());
  m.matchEventIndicator().clear().endOfEvent(true);
  m.noMDEntriesCount(1).next().applID(static_cast<int16_t>(any(400)));
  set_size(buf, at, m.encodedLength());
  return 10 + m.encodedLength();
}

// Skipped by the decoder: an MBO book message.
size_t orderbook47(char *buf, size_t at, size_t room)
{
  const int n = static_cast<int>(1 + any(6));
  if (static_cast<size_t>(11 + 8 + 40 * n + 10) > room)
    return 0;
  sbe::MDIncrementalRefreshOrderBook47 m;
  m.wrapForEncode(buf, header<sbe::MDIncrementalRefreshOrderBook47>(buf, at), kBuf);
  m.transactTime(rng());
  m.matchEventIndicator().clear().endOfEvent(true);
  auto &g = m.noMDEntriesCount(static_cast<uint8_t>(n));
  for (int i = 0; i < n; ++i)
  {
    g.next();
    g.orderID(rng()).mDOrderPriority(rng());
    g.mDEntryPx().mantissa(static_cast<int64_t>(4500000000000));
    g.mDDisplayQty(1).securityID(1).mDUpdateAction(sbe::MDUpdateAction::New).mDEntryType(sbe::MDEntryTypeBook::Bid);
  }
  set_size(buf, at, m.encodedLength());
  return 10 + m.encodedLength();
}

// Skipped by the decoder: daily statistics, with no entries.
size_t stats49(char *buf, size_t at, size_t room)
{
  if (static_cast<size_t>(11 + 3 + 10) > room)
    return 0;
  sbe::MDIncrementalRefreshDailyStatistics49 m;
  m.wrapForEncode(buf, header<sbe::MDIncrementalRefreshDailyStatistics49>(buf, at), kBuf);
  m.transactTime(rng());
  m.matchEventIndicator().clear().lastStatsMsg(true);
  m.noMDEntriesCount(0);
  set_size(buf, at, m.encodedLength());
  return 10 + m.encodedLength();
}

std::vector<uint8_t> make_packet(uint32_t seq)
{
  char buf[kBuf] = {};
  const uint64_t sending = rng();
  std::memcpy(buf, &seq, 4);
  std::memcpy(buf + 4, &sending, 8);
  size_t at = 12;
  const int messages = static_cast<int>(any(7));   // 0 to 6: an empty packet is a heartbeat
  for (int i = 0; i < messages; ++i)
  {
    const uint64_t pick = any(100);
    size_t n;
    if (pick < 55)
      n = book46(buf, at, kBuf - at);
    else if (pick < 75)
      n = trade48(buf, at, kBuf - at);
    else if (pick < 82)
      n = status30(buf, at, kBuf - at);
    else if (pick < 84)
      n = reset4(buf, at, kBuf - at);
    else if (pick < 94)
      n = orderbook47(buf, at, kBuf - at);
    else
      n = stats49(buf, at, kBuf - at);
    if (n == 0)
      break;
    at += n;
  }
  return std::vector<uint8_t>(reinterpret_cast<uint8_t *>(buf), reinterpret_cast<uint8_t *>(buf) + at);
}

} // namespace

int main(int argc, char **argv)
{
  if (argc != 4)
  {
    std::fprintf(stderr, "usage: make_packets <count> <seed> packets.bin\n");
    return 2;
  }
  const long count = std::atol(argv[1]);
  rng.seed(std::strtoull(argv[2], nullptr, 10));
  FILE *f = std::fopen(argv[3], "wb");
  if (!f)
  {
    std::perror(argv[3]);
    return 1;
  }
  for (long i = 0; i < count; ++i)
  {
    const std::vector<uint8_t> p = make_packet(static_cast<uint32_t>(1000 + i));
    const uint32_t n = static_cast<uint32_t>(p.size());
    const uint8_t l[4] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
    std::fwrite(l, 1, 4, f);
    std::fwrite(p.data(), 1, p.size(), f);
  }
  std::fclose(f);
  return 0;
}
