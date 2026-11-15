#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * Filter and Batcher: the two actors between SbeDecoder and the CPU.
 *
 *   SbeDecoder --messages--> Filter --PackedEvent--> Batcher --64-byte lines--> CPU (FpgaFeed)
 *
 * Both are ordinary message actors: they derive from kfpga::Actor, list their
 * handlers in KFPGA_HANDLERS, and each runs as a process of its own
 * (kfpga::Process<Filter>, kfpga::Process<Batcher>) whose task, in hls/feed_top.cpp,
 * calls proc.step(ports) every clock. Each step reads at most one message, runs the
 * handler for its type, and writes what the handler sent. So each actor handles one
 * message per clock (II = 1), and every handler sends at most one message, which the
 * outbox writes in the same step.
 *
 * Filter
 *
 *   Keeps what the strategy trades and drops the rest, so that only those events
 *   cross PCIe. Its table holds up to kSecurities (64) security ids; each lookup
 *   compares the id against every entry at once, one comparator per entry, so it
 *   takes one clock however full the table is. A security's position in the table,
 *   its index, travels in every event instead of the 4-byte id, and the CPU uses it to
 *   find the book.
 *
 *   It packs what it keeps to 16 bytes (PackedEvent, layout in events.hpp): price,
 *   size, order count, index, level, action, side.
 *
 *     BookEntry       outright bid or offer of a security in the table: passed.
 *                     A book reset entry ('J'): passed as a book reset of that
 *                     security. Implied entries, and securities not in the table:
 *                     dropped.
 *     Trade           passed if the security is in the table, with the aggressor side.
 *     SecurityStatus  always passed, as security kAllSecurities: CME sends status for a
 *                     whole security group with a null security id, and every book on
 *                     the CPU needs it for halt and pre-open state. The status fields
 *                     ride in the price field of the event.
 *     ChannelReset    always passed: a reset clears every book of the channel.
 *     EndOfPacket     always passed, so the CPU sees every packet's sequence number and
 *                     checks for gaps itself, even for a packet with nothing for us.
 *     WantSecurity    from the CPU, not the decoder: stores a security id at an index
 *                     and replies WantAck. An index past the table is not stored and
 *                     the ack says so, so the CPU never believes it has a security the
 *                     filter would not pass.
 *
 *   Filter has two ports: port 0 from the decoder and to Batcher, port 1 from and to
 *   the CPU, for WantSecurity and WantAck. The CPU fills the table at start and sends
 *   no packets until every entry is acked.
 *
 * Batcher
 *
 *   Packs the kept events into 64-byte lines, the unit the host reads, three events to
 *   a line, and ends every packet with exactly one End line.
 *
 *     PackedEvent  if two are already held, send a full EventLine with them and this
 *                  one; otherwise hold it in a register.
 *     EndOfPacket  send the End line: the events still held, 0 to 2, and the packet's
 *                  sequence number. Every packet sends one, including packets with no
 *                  event for us; the CPU acts on it.
 *
 *   So a line leaves as soon as it is full, and nothing waits for a later packet. The
 *   lines go to `feed`, the CPU actor FpgaFeed, through the lane in host memory. Each
 *   handler sends at most one line, so Batcher also runs at one message per clock.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"


namespace mdp3fpga {

// MDEntryType values of a book entry that Filter keeps.
constexpr uint8_t kFeedBid = '0';
constexpr uint8_t kFeedOffer = '1';
constexpr uint8_t kFeedBookReset = 'J';

// One event in the 16-byte form the CPU receives (layout in events.hpp). `index` is
// the security's position in Filter's table, or kAllSecurities.
inline PackedEvent pack_event(int64_t price, int32_t size, int32_t orders, int index, uint8_t level,
                              uint8_t action, bool side)
{
#pragma HLS INLINE
  PackedEvent m;
  m.lo = static_cast<uint64_t>(price);
  m.hi = static_cast<uint64_t>(static_cast<uint32_t>(size)) | (uint64_t(static_cast<uint16_t>(orders)) << 32) |
         (uint64_t(static_cast<uint8_t>(index)) << 48) | (uint64_t(level & 15) << 56) |
         (uint64_t(action & 7) << 60) | (uint64_t(side ? 1 : 0) << 63);
  return m;
}

class Filter : public kfpga::Actor<Filter>
{
  int32_t table_[kSecurities] = {};   // security id at each index; 0 is an empty entry

  // The index of a security id in the table, or -1. Unrolled: one comparator per entry,
  // all compared in the same clock.
  int security_index(int32_t id) const
  {
#pragma HLS INLINE
    int found = -1;
    for (int i = 0; i < kSecurities; ++i)
    {
#pragma HLS UNROLL
      if (id != 0 && table_[i] == id)
        found = i;
    }
    return found;
  }

  // Send a kept event on to Batcher.
  void pass(const PackedEvent &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
  }

  // An MBP book entry: keep outright bids and offers and book resets of securities in
  // the table.
  void on_book(const BookEntry &m)
  {
#pragma HLS INLINE
    const int c = security_index(m.security_id);
    if (c < 0)
      return;
    const uint8_t type = m.entry_type();
    if (type == kFeedBookReset)
      pass(pack_event(0, 0, 0, c, kOtherBookReset, kActionOther, false));
    else if (type == kFeedBid || type == kFeedOffer)
      pass(pack_event(m.price, m.size, m.orders, c, m.level(), m.action(), type == kFeedOffer));
    // anything else is implied, ignored
  }

  // A trade of a security in the table. Side bit set if the seller was the aggressor
  // (AggressorSide 2).
  void on_trade(const Trade &m)
  {
#pragma HLS INLINE
    const int c = security_index(m.security_id);
    if (c >= 0)
      pass(pack_event(m.price, m.size, m.orders, c, 0, kActionTrade, m.aggressor() == 2));
  }

  // Security status, for every book: CME sends it per security group with a null id.
  // flags (trading status, halt reason, trading event, match event) go in the price.
  void on_status(const SecurityStatus &m)
  {
#pragma HLS INLINE
    pass(pack_event(m.flags, 0, 0, kAllSecurities, kOtherStatus, kActionOther, false));
  }

  // Channel reset: every book of the channel is cleared.
  void on_reset(const ChannelReset &)
  {
#pragma HLS INLINE
    pass(pack_event(0, 0, 0, 0, kOtherChannelReset, kActionOther, false));
  }

  // End of every packet, passed on so the CPU sees each sequence number.
  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
  }

  // From the CPU at start: store a security id at an index, and acknowledge it on the
  // port it came in on (reply), the CPU's port.
  void on_want(const WantSecurity &m)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=table_ type=complete
    // An index past the table is not stored, and the answer says so: the host must
    // never believe it has a security the filter will not pass.
    const bool ok = m.index < kSecurities;
    if (ok)
      table_[m.index] = m.security_id;
    WantAck a;
    a.index = m.index;
    a.ok = ok ? 1 : 0;
    reply(a);
  }

public:
  static constexpr kfpga::ActorId kId = kFilter;
  kfpga::ActorRef batcher;   // the next actor: Batcher, on port 0

  KFPGA_HANDLERS(KFPGA_ON(BookEntry, on_book) KFPGA_ON(Trade, on_trade) KFPGA_ON(SecurityStatus, on_status)
                     KFPGA_ON(ChannelReset, on_reset) KFPGA_ON(EndOfPacket, on_end)
                     KFPGA_ON(WantSecurity, on_want))
};

class Batcher : public kfpga::Actor<Batcher>
{
  uint64_t lo_[2] = {}, hi_[2] = {};   // up to two events waiting for their line
  ap_uint<2> held_ = 0;                 // how many

  // A kept event. With two already held, this one completes a line: send it. Otherwise
  // hold it.
  void on_event(const PackedEvent &m)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=lo_ type=complete
#pragma HLS ARRAY_PARTITION variable=hi_ type=complete
    if (held_ == 2)
    {
      EventLine l;
      l.lo0 = lo_[0];
      l.hi0 = hi_[0];
      l.lo1 = lo_[1];
      l.hi1 = hi_[1];
      l.lo2 = m.lo;
      l.hi2 = m.hi;
      feed.send(l, this);
      held_ = 0;
    }
    else
    {
      lo_[held_[0]] = m.lo;
      hi_[held_[0]] = m.hi;
      held_ = held_ + 1;
    }
  }

  // Send the End line with the N events held and the packet's sequence number.
  template <int N>
  void send_end(uint32_t seq)
  {
#pragma HLS INLINE
    EndLine<N> l;
    l.lo0 = lo_[0];
    l.hi0 = hi_[0];
    l.lo1 = lo_[1];
    l.hi1 = hi_[1];
    l.seq = seq;
    feed.send(l, this);
  }

  // End of a packet: send its End line, whether or not it had events.
  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    if (held_ == 0)
      send_end<0>(m.seq);
    else if (held_ == 1)
      send_end<1>(m.seq);
    else
      send_end<2>(m.seq);
    held_ = 0;
  }

public:
  static constexpr kfpga::ActorId kId = kBatcher;
  kfpga::ActorRef feed;   // the CPU actor FpgaFeed, through the lane in host memory

  KFPGA_HANDLERS(KFPGA_ON(PackedEvent, on_event) KFPGA_ON(EndOfPacket, on_end))
};

} // namespace mdp3fpga
