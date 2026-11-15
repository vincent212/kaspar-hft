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
  uint32_t cycle_in = 0;       // the decoder's cycle count at the packet's first word
  uint32_t cycle_out = 0;      // and at this EndOfPacket
  uint32_t cycle_last = 0;     // and at the packet's last word
  KFPGA_FIELDS(seq, sending_time, events, messages, bytes, cycle_in, cycle_out, cycle_last)
};

// What BookBank publishes, per packet that touched its book: the packet's events
// (the messages above, implied entries dropped), then the book, then BatchEnd.

constexpr int kLevels = 10;          // per side
constexpr int kLevelsPerPart = 2;    // what one envelope's payload holds
constexpr int kBookParts = 2 * kLevels / kLevelsPerPart;

// Two levels of the book. Part 0 to 4 are the bids from the best, 5 to 9 the offers.
struct BookPart : kfpga::Message<325>
{
  uint32_t seq = 0;
  uint32_t part = 0;
  int64_t price0 = 0;
  int32_t size0 = 0;
  int32_t orders0 = 0;
  int64_t price1 = 0;
  int32_t size1 = 0;
  int32_t orders1 = 0;
  KFPGA_FIELDS(seq, part, price0, size0, orders0, price1, size1, orders1)
};

// The last envelope of a packet's batch: the strategy acts on this one.
struct BatchEnd : kfpga::Message<326>
{
  uint32_t seq = 0;
  int32_t security_id = 0;
  uint32_t events = 0;         // events in the batch
  uint32_t gap = 0;            // 1 once a sequence gap has been seen: the book is suspect
  uint32_t cycle_in = 0;       // the packet's first word
  uint32_t cycle_last = 0;     // its last word
  uint32_t cycle_out = 0;      // this envelope
  KFPGA_FIELDS(seq, security_id, events, gap, cycle_in, cycle_last, cycle_out)
};

// What the card sends the CPU: events packed to 16 bytes, three to a 64-byte line.
// The last line of every packet is an End line, even for a packet with no event
// for us: two events at most, and the packet's sequence number in the third slot.
// The CPU checks the sequence itself.
//
//   bits  63..0    price mantissa; for a status, trading status | halt reason << 8
//                  | trading event << 16 | match event << 24
//         95..64   size
//        111..96   order count
//        119..112  index of the security in the filter's table; kAllSecurities for
//                  a status, which CME sends for a security group
//        123..120  level, or the kind of an action 7 event
//        126..124  action: MDUpdateAction 0 to 5, 6 trade, 7 other
//        127       1 for an offer; for a trade, set if the aggressor is the seller
constexpr int kEventsPerLine = 3;
constexpr int kSecurities = 64;       // entries in the filter's table
constexpr uint8_t kAllSecurities = 0xff;
constexpr uint8_t kActionTrade = 6;
constexpr uint8_t kActionOther = 7;
constexpr uint8_t kOtherStatus = 0;
constexpr uint8_t kOtherChannelReset = 1;
constexpr uint8_t kOtherBookReset = 2;

// One packed event, Filter to Batcher.
struct PackedEvent : kfpga::Message<330>
{
  uint64_t lo = 0;
  uint64_t hi = 0;
  KFPGA_FIELDS(lo, hi)

  uint8_t action() const { return (hi >> 60) & 7; }
  uint8_t level() const { return (hi >> 56) & 15; }
  uint8_t security_index() const { return (hi >> 48) & 0xff; }
};

// A full line of events, in the middle of a packet.
struct EventLine : kfpga::Message<331>
{
  uint64_t lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0, lo2 = 0, hi2 = 0;
  KFPGA_FIELDS(lo0, hi0, lo1, hi1, lo2, hi2)
};

// The last line of a packet: N events, 0 to 2, and its sequence number. The CPU
// acts on this one.
template <int N>
struct EndLine : kfpga::Message<332 + N>   // msgids 332-334: N is 0 to 2
{
  uint64_t lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0;
  uint32_t seq = 0;
  KFPGA_FIELDS(lo0, hi0, lo1, hi1, seq)
};
constexpr kfpga::MsgId kFirstEndLine = 332, kLastEndLine = 334;

// The CPU fills the filter's table at start, one entry at a time.
struct WantSecurity : kfpga::Message<338>
{
  uint32_t index = 0;          // the security index in every event
  int32_t security_id = 0;
  KFPGA_FIELDS(index, security_id)
};

struct WantAck : kfpga::Message<339>
{
  uint32_t index = 0;
  uint32_t ok = 0;             // 1 if stored; 0 if the index is past the table
  KFPGA_FIELDS(index, ok)
};

// Actor ids on the card.
constexpr kfpga::ActorId kDecoder = 20;
constexpr kfpga::ActorId kBooks = 21;
constexpr kfpga::ActorId kFeed = 22;       // the CPU actor that takes the batches
constexpr kfpga::ActorId kFilter = 23;
constexpr kfpga::ActorId kBatcher = 24;
constexpr kfpga::ActorId kFeedRx = 25;

} // namespace mdp3fpga
