#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * What the MDP3 decoder sends, one 64-byte envelope each (see
 * kfpga/envelope.hpp), in packet order:
 *
 *   BookEntry       one MBP entry of template 46 (MBO entries are skipped)
 *   Trade           one entry of template 48 (the per-order fills are skipped)
 *   SecurityStatus  template 30
 *   ChannelReset    one entry of template 4
 *   EndOfPacket     after the last event of every packet, even an empty one
 *
 * Every field is the raw value from the wire: prices are mantissas with
 * exponent -9, null values are CME's (INT32_MAX for an empty Int32NULL...).
 * Small fields share one 32-bit `flags` word, since a field of 4 bytes or
 * less takes a whole word of the 12 in the payload.
 */

#include <cstdint>

#include "kfpga/envelope.hpp"

namespace mdp3fpga {

// The match event indicator bits (MatchEventIndicator), as on the wire.
constexpr uint8_t kEndOfEvent = 0x80;

struct BookEntry : kfpga::Message<320>
{
  uint32_t seq = 0;            // packet sequence number
  uint64_t transact_time = 0;  // the message's TransactTime
  int64_t price = 0;           // MDEntryPx mantissa
  int32_t size = 0;            // MDEntrySize
  int32_t security_id = 0;
  uint32_t rpt_seq = 0;
  int32_t orders = 0;          // NumberOfOrders
  uint32_t flags = 0;          // level | action << 8 | entry type << 16 | match event << 24
  int32_t tradeable_size = 0;
  KFPGA_FIELDS(seq, transact_time, price, size, security_id, rpt_seq, orders, flags, tradeable_size)

  uint8_t level() const { return flags & 0xff; }
  uint8_t action() const { return (flags >> 8) & 0xff; }        // MDUpdateAction
  uint8_t entry_type() const { return (flags >> 16) & 0xff; }   // MDEntryType: '0' bid, '1' offer, ...
  uint8_t match_event() const { return flags >> 24; }
};

struct Trade : kfpga::Message<321>
{
  uint32_t seq = 0;
  uint64_t transact_time = 0;
  int64_t price = 0;
  int32_t size = 0;
  int32_t security_id = 0;
  uint32_t rpt_seq = 0;
  int32_t orders = 0;
  uint32_t flags = 0;          // aggressor side | action << 8 | match event << 24
  uint32_t trade_id = 0;       // MDTradeEntryID
  KFPGA_FIELDS(seq, transact_time, price, size, security_id, rpt_seq, orders, flags, trade_id)

  uint8_t aggressor() const { return flags & 0xff; }
  uint8_t action() const { return (flags >> 8) & 0xff; }
  uint8_t match_event() const { return flags >> 24; }
};

struct SecurityStatus : kfpga::Message<322>
{
  uint32_t seq = 0;
  uint64_t transact_time = 0;
  int32_t security_id = 0;
  uint32_t trade_date = 0;
  uint32_t flags = 0;          // trading status | halt reason << 8 | trading event << 16 | match event << 24
  KFPGA_FIELDS(seq, transact_time, security_id, trade_date, flags)
};

struct ChannelReset : kfpga::Message<323>
{
  uint32_t seq = 0;
  uint64_t transact_time = 0;
  int32_t appl_id = 0;
  uint32_t match_event = 0;
  KFPGA_FIELDS(seq, transact_time, appl_id, match_event)
};

struct EndOfPacket : kfpga::Message<324>
{
  uint32_t seq = 0;
  uint64_t sending_time = 0;
  uint32_t events = 0;         // events sent for this packet
  uint32_t messages = 0;       // SBE messages in the packet, all templates
  uint32_t bytes = 0;          // packet length
  KFPGA_FIELDS(seq, sending_time, events, messages, bytes)
};

// Actor ids on the card.
constexpr kfpga::ActorId kDecoder = 20;
constexpr kfpga::ActorId kBooks = 21;

} // namespace mdp3fpga
