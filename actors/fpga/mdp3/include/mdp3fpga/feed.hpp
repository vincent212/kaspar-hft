#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * What stands between the decoder and the CPU, two actors:
 *
 *   Filter    drops implied entries and every contract not in its table, and
 *             packs what is left to 16 bytes (events.hpp).
 *   Batcher   sends a 64-byte line as soon as it holds three events, or at the
 *             end of the packet with what it has, then a PacketEnd. Nothing
 *             waits for a later packet.
 *
 * Packets with no event for us end here: the CPU never hears of them.
 *
 * The contract table is a parallel compare, one comparator per entry, kContracts
 * of them. Filled from the build for now (MDP3_CONTRACTS); the CPU loads it at
 * start once the instrument definitions reach it.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

constexpr int kContracts = 64;

constexpr uint8_t kFeedBid = '0';
constexpr uint8_t kFeedOffer = '1';
constexpr uint8_t kFeedBookReset = 'J';





#ifndef MDP3_CONTRACTS
#define MDP3_CONTRACTS 203686713
#endif

// Contract -> index in the table, or -1.
inline int contract_index(int32_t id)
{
#pragma HLS INLINE
  static const int32_t kTable[kContracts] = {MDP3_CONTRACTS};
#pragma HLS ARRAY_PARTITION variable=kTable type=complete
  int found = -1;
  for (int i = 0; i < kContracts; ++i)
  {
#pragma HLS UNROLL
    if (id != 0 && kTable[i] == id)
      found = i;
  }
  return found;
}

inline PackedEvent pack_event(int64_t price, int32_t size, int32_t orders, int contract, uint8_t level,
                              uint8_t action, bool side)
{
#pragma HLS INLINE
  PackedEvent m;
  m.lo = static_cast<uint64_t>(price);
  m.hi = static_cast<uint64_t>(static_cast<uint32_t>(size)) | (uint64_t(static_cast<uint16_t>(orders)) << 32) |
         (uint64_t(static_cast<uint8_t>(contract)) << 48) | (uint64_t(level & 15) << 56) |
         (uint64_t(action & 7) << 60) | (uint64_t(side ? 1 : 0) << 63);
  return m;
}

class Filter : public kfpga::Actor<Filter>
{
  void pass(const PackedEvent &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
  }

  void on_book(const BookEntry &m)
  {
#pragma HLS INLINE
    const int c = contract_index(m.security_id);
    if (c < 0)
      return;
    const uint8_t type = m.entry_type();
    if (type == kFeedBookReset)
      pass(pack_event(0, 0, 0, c, kOtherBookReset, kActionOther, false));
    else if (type == kFeedBid || type == kFeedOffer)
      pass(pack_event(m.price, m.size, m.orders, c, m.level(), m.action(), type == kFeedOffer));
    // anything else is implied, ignored
  }

  void on_trade(const Trade &m)
  {
#pragma HLS INLINE
    const int c = contract_index(m.security_id);
    if (c >= 0)
      pass(pack_event(m.price, m.size, m.orders, c, 0, kActionTrade, m.aggressor() == 2));
  }

  void on_status(const SecurityStatus &m)
  {
#pragma HLS INLINE
    const int c = contract_index(m.security_id);
    if (c >= 0)
      pass(pack_event(0, 0, 0, c, kOtherStatus, kActionOther, false));
  }

  void on_reset(const ChannelReset &)
  {
#pragma HLS INLINE
    pass(pack_event(0, 0, 0, 0, kOtherChannelReset, kActionOther, false));
  }

  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    // Every packet's sequence number goes on, for the gap flag.
    batcher.send(m, this);
  }

public:
  static constexpr kfpga::ActorId kId = kFilter;
  kfpga::ActorRef batcher;

  KFPGA_HANDLERS(KFPGA_ON(BookEntry, on_book) KFPGA_ON(Trade, on_trade) KFPGA_ON(SecurityStatus, on_status)
                     KFPGA_ON(ChannelReset, on_reset) KFPGA_ON(EndOfPacket, on_end))
};

class Batcher : public kfpga::Actor<Batcher>
{
  uint64_t lo_[2] = {}, hi_[2] = {};   // events waiting for their line
  ap_uint<2> held_ = 0;

  uint32_t events_ = 0;                // of this packet
  bool gap_ = false, seen_ = false;
  uint32_t last_seq_ = 0;

  template <int N>
  void line(uint64_t lo2, uint64_t hi2)
  {
#pragma HLS INLINE
    EventLine<N> m;
    m.lo0 = lo_[0];
    m.hi0 = hi_[0];
    m.lo1 = lo_[1];
    m.hi1 = hi_[1];
    m.lo2 = lo2;
    m.hi2 = hi2;
    feed.send(m, this);
  }

  void on_event(const PackedEvent &m)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=lo_ type=complete
#pragma HLS ARRAY_PARTITION variable=hi_ type=complete
    events_ = events_ + 1;
    if (held_ == 2)
    {
      line<3>(m.lo, m.hi);
      held_ = 0;
    }
    else
    {
      lo_[held_[0]] = m.lo;
      hi_[held_[0]] = m.hi;
      held_ = held_ + 1;
    }
  }

  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    if (seen_ && m.seq != last_seq_ + 1)
      gap_ = true;
    seen_ = true;
    last_seq_ = m.seq;
    if (events_ == 0)
      return;
    if (held_ == 1)
      line<1>(0, 0);
    else if (held_ == 2)
      line<2>(0, 0);
    held_ = 0;
    PacketEnd e;
    e.seq = m.seq;
    e.events = events_;
    e.gap = gap_ ? 1 : 0;
    e.cycle_in = m.cycle_in;
    e.cycle_last = m.cycle_last;
    e.cycle_out = now;
    feed.send(e, this);
    events_ = 0;
  }

public:
  static constexpr kfpga::ActorId kId = kBatcher;
  kfpga::ActorRef feed;
  uint32_t now = 0;                    // the clock, from the process

  KFPGA_HANDLERS(KFPGA_ON(PackedEvent, on_event) KFPGA_ON(EndOfPacket, on_end))
};

} // namespace mdp3fpga
