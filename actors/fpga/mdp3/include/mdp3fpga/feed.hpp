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
 *   Batcher   sends a 64-byte line when a fourth event shows the line is not the
 *             packet's last, or at the end of the packet with what it has, marked
 *             End. Nothing waits for a later packet. One send a step, so II = 1.
 *
 * Packets with no event for us end here: the CPU never hears of them, but a
 * SeqGap always goes through.
 *
 * The contract table is a parallel compare, one comparator per entry, kContracts
 * of them. Filled from the build for now (MDP3_CONTRACTS); the CPU loads it at
 * start once the instrument definitions reach it.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"

#ifdef MDP3_SW
#include <deque>
#endif

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

#ifdef MDP3_SW
// Test only, plain g++: a packet's stamps, for the testbench to take when its End
// line comes out. Latency is not carried on the wire.
struct EndStamp
{
  uint32_t seq, events, cycle_in, cycle_last, cycle_out;
};
inline std::deque<EndStamp> &end_stamps()
{
  static std::deque<EndStamp> q;
  return q;
}
#endif

class Filter : public kfpga::Actor<Filter>
{
  bool any_ = false;   // this packet had an event for us

  void pass(const PackedEvent &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
    any_ = true;
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
    if (any_)
      batcher.send(m, this);
    any_ = false;
  }

  void on_gap(const SeqGap &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
  }

public:
  static constexpr kfpga::ActorId kId = kFilter;
  kfpga::ActorRef batcher;

  KFPGA_HANDLERS(KFPGA_ON(BookEntry, on_book) KFPGA_ON(Trade, on_trade) KFPGA_ON(SecurityStatus, on_status)
                     KFPGA_ON(ChannelReset, on_reset) KFPGA_ON(EndOfPacket, on_end) KFPGA_ON(SeqGap, on_gap))
};

class Batcher : public kfpga::Actor<Batcher>
{
  uint64_t lo_[kEventsPerLine] = {}, hi_[kEventsPerLine] = {};   // the line being filled
  ap_uint<2> held_ = 0;
#ifdef MDP3_SW
  uint32_t events_ = 0;
#endif

  template <class L>
  void send_line()
  {
#pragma HLS INLINE
    L m;
    m.lo0 = lo_[0];
    m.hi0 = hi_[0];
    m.lo1 = lo_[1];
    m.hi1 = hi_[1];
    m.lo2 = lo_[2];
    m.hi2 = hi_[2];
    feed.send(m, this);
  }

  // The line is full only when a fourth event shows more is coming, so that the
  // last line of a packet is always here for on_end to mark.
  void on_event(const PackedEvent &m)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=lo_ type=complete
#pragma HLS ARRAY_PARTITION variable=hi_ type=complete
    ap_uint<2> at = held_;
    if (held_ == kEventsPerLine)
    {
      send_line<EventLine<3, false>>();
      at = 0;
    }
    lo_[at] = m.lo;
    hi_[at] = m.hi;
    held_ = at + 1;
#ifdef MDP3_SW
    ++events_;
#endif
  }

  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    if (held_ == 1)
      send_line<EventLine<1, true>>();
    else if (held_ == 2)
      send_line<EventLine<2, true>>();
    else
      send_line<EventLine<3, true>>();
    held_ = 0;
#ifdef MDP3_SW
    end_stamps().push_back({m.seq, events_, m.cycle_in, m.cycle_last, now});
    events_ = 0;
#else
    (void)m;
#endif
  }

  void on_gap(const SeqGap &m)
  {
#pragma HLS INLINE
    feed.send(m, this);
  }

public:
  static constexpr kfpga::ActorId kId = kBatcher;
  kfpga::ActorRef feed;
#ifdef MDP3_SW
  uint32_t now = 0;                    // the clock, for the stamps
#endif

  KFPGA_HANDLERS(KFPGA_ON(PackedEvent, on_event) KFPGA_ON(EndOfPacket, on_end) KFPGA_ON(SeqGap, on_gap))
};

} // namespace mdp3fpga
