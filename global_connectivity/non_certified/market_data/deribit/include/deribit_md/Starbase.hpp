#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Deribit Starbase SBE market data (UDP multicast): packet framing and message
// decoding into plain structs. NOT CERTIFIED.
//
// The message bodies are decoded with codecs generated from Deribit's schema
// (deribit-sbe-market-data-api.xml, version 2) by the real-logic SBE tool, the
// same generator genschema/ runs for CME. Only the framing is written here:
//
//   UDP packet = PacketHeader (24 bytes) + messageCount messages
//   message    = mdMessageHeader (16 bytes, generated as MdMessageHeader)
//                + body; messageLength counts the header and the body and is
//                padded to a multiple of 8 (feed.md 3)
//
// PacketHeader is a composite of the schema that no message references, so the
// SBE tool does not generate it; its layout is checked against Deribit's
// Python SDK in tests/test_starbase_decode.cpp and by `make sdk-check`.
//
// Sequence numbers: only the first message's is in the packet header; message
// i of a packet has seqNum + i. A packet with messageCount 0 is a heartbeat
// carrying the next expected number (feed.md 5).

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace deribit::starbase
{
  // PacketHeader.type is a bit set (schema: set "type", choices 0, 1, 2).
  enum PacketType : uint16_t
  {
    INCREMENTAL = 1,
    SNAPSHOT = 2,
    RETRANSMIT = 4,   // sent together with INCREMENTAL (SDK packet.py)
  };

  struct PacketHeader
  {
    int64_t sendingTime = 0;  // ns since the epoch
    int64_t seqNum = 0;       // sequence number of the first message
    int32_t channelId = 0;
    uint16_t type = 0;        // PacketType bits
    uint16_t messageCount = 0;
  };
  constexpr size_t kPacketHeaderSize = 24;
  constexpr size_t kMessageHeaderSize = 16;

  // mdMessageHeader.flags (schema: set "flags", choices 0 and 1).
  constexpr uint16_t kStartOfTransaction = 1;
  constexpr uint16_t kEndOfTransaction = 2;

  // Null of an optional int64 (makerOrderId on an implied or synthetic leg).
  constexpr int64_t kNullInt64 = INT64_MIN;

  // ---- message bodies ----------------------------------------------------------------

  struct Put           // BidPut (20) / AskPut (21)
  {
    bool bid = true;
    int64_t orderId = 0, instrumentId = 0, qty = 0, price9 = 0, sortOrderId = 0;
  };
  struct QtyReduced    // BidQtyReduced (22) / AskQtyReduced (23)
  {
    bool bid = true;
    int64_t orderId = 0, instrumentId = 0, qty = 0;
  };
  struct Delete        // BidDelete (24) / AskDelete (25)
  {
    bool bid = true;
    int64_t orderId = 0, instrumentId = 0;
  };
  struct TradeSummary  // 30
  {
    int64_t instrumentId = 0, takerOrderId = 0, totalFilled = 0, deepestPrice9 = 0;
    int64_t markPrice9 = 0, indexPrice9 = 0;
    int32_t tradeCount = 0;
    uint32_t takerFlags = 0;   // bit 0 isSell, bit 1 isLiquidation, bit 2 wasDelayed
  };
  struct Trade         // 31
  {
    int64_t matchId = 0, instrumentId = 0, makerOrderId = kNullInt64, qty = 0, price9 = 0;
    uint32_t makerFlags = 0;
  };
  struct BlockTrade    // 33
  {
    int64_t matchId = 0, instrumentId = 0, blockTradeId = 0, qty = 0, price9 = 0;
    uint32_t takerFlags = 0;
  };
  struct InstrumentDefinition   // 10
  {
    int64_t instrumentId = 0;
    std::string name;
    int64_t indexId = 0;
    std::string underlying, quantityAsset, priceAsset;
    int64_t expiryTime = 0, strikePrice9 = 0, minOrderQuantity = 0, tickSize9 = 0;
    int8_t quantityExponent = 0, type = 0, status = 0;
    uint8_t flags = 0;
    std::vector<std::pair<int64_t, int64_t>> largeTickSizes;   // (tick, threshold price)
    std::vector<std::pair<int64_t, int8_t>> legs;              // (instrument, ratio)
  };
  struct InstrumentStatusUpdate // 16
  {
    int64_t instrumentId = 0;
    int8_t tradingStatus = 0;
  };
  struct SnapshotBound          // SnapshotHeader (100) / SnapshotTrailer (101)
  {
    bool header = true;
    int64_t instrumentId = 0, incrementalTimestamp = 0, incrementalSeqNum = 0;
  };
  struct EndOfCycle             // 119
  {
    int32_t activeInstrumentCount = 0;
  };
  struct Other                  // decoded only for the dump (12, 11, 14, 15, 200, 202)
  {
  };
  struct Unknown                // a template id the schema does not define
  {
  };

  using Body = std::variant<Put, QtyReduced, Delete, TradeSummary, Trade, BlockTrade,
                            InstrumentDefinition, InstrumentStatusUpdate, SnapshotBound,
                            EndOfCycle, Other, Unknown>;

  struct Message
  {
    int64_t seq = 0;
    uint16_t length = 0, templateId = 0, version = 0, flags = 0;
    int64_t transactTime = 0;
    Body body;
    bool start() const { return flags & kStartOfTransaction; }
    bool end() const { return flags & kEndOfTransaction; }
  };

  enum class Status
  {
    Ok,
    Short,       // shorter than its packet header
    Truncated,   // a message runs past the end of the packet
    BadLength,   // a messageLength below the header size or the body's fixed block
  };
  const char *to_string(Status s);

  // Decode one UDP payload. On an error the messages decoded before it are
  // kept in `out`.
  Status decode_packet(const uint8_t *buf, size_t len, PacketHeader &ph, std::vector<Message> &out);

  // Decode one message body (after its 16-byte header). false if the body is
  // shorter than the template's fixed block.
  bool decode_body(uint16_t templateId, uint16_t version, const uint8_t *body, size_t len, Body &out);

  // One line per message, every field the schema defines, integers as sent;
  // the format tools/sdk_dump.py writes from Deribit's Python SDK.
  std::string dump(const uint8_t *msg, size_t len, int64_t seq);
  std::string dump_packet_header(const PacketHeader &ph);

  // Encoders for tests: a packet of messages built from these structs.
  std::vector<uint8_t> encode_packet(const PacketHeader &ph, const std::vector<Message> &msgs);
}
