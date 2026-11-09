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
 *   Filter    drops implied entries and every security not in its table, and
 *             packs what is left to 16 bytes (events.hpp).
 *   Batcher   sends a 64-byte line as soon as it holds three events, and at the
 *             end of every packet an End line: what it has left, 0 to 2 events,
 *             and the sequence number. One send a step, so II = 1.
 *
 * The CPU sees the end of every packet, so it checks the sequence itself. CME sends
 * SecurityStatus for a security group, with no security id, so a status always
 * passes, with its security set to kAllSecurities.
 *
 * The security table is a parallel compare, one comparator per entry, kSecurities
 * of them. The CPU fills it at start over its own link, WantSecurity answered by
 * WantAck, and sends no packets until every entry is acked.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"


namespace mdp3fpga {

constexpr uint8_t kFeedBid = '0';
constexpr uint8_t kFeedOffer = '1';
constexpr uint8_t kFeedBookReset = 'J';





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
  int32_t table_[kSecurities] = {};

  // Security -> index in the table, or -1.
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

  void pass(const PackedEvent &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
  }

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

  void on_trade(const Trade &m)
  {
#pragma HLS INLINE
    const int c = security_index(m.security_id);
    if (c >= 0)
      pass(pack_event(m.price, m.size, m.orders, c, 0, kActionTrade, m.aggressor() == 2));
  }

  void on_status(const SecurityStatus &m)
  {
#pragma HLS INLINE
    pass(pack_event(m.flags, 0, 0, kAllSecurities, kOtherStatus, kActionOther, false));
  }

  void on_reset(const ChannelReset &)
  {
#pragma HLS INLINE
    pass(pack_event(0, 0, 0, 0, kOtherChannelReset, kActionOther, false));
  }

  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    batcher.send(m, this);
  }

  void on_want(const WantSecurity &m)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=table_ type=complete
    if (m.index < kSecurities)
      table_[m.index] = m.security_id;
    WantAck a;
    a.index = m.index;
    reply(a);
  }

public:
  static constexpr kfpga::ActorId kId = kFilter;
  kfpga::ActorRef batcher;

  KFPGA_HANDLERS(KFPGA_ON(BookEntry, on_book) KFPGA_ON(Trade, on_trade) KFPGA_ON(SecurityStatus, on_status)
                     KFPGA_ON(ChannelReset, on_reset) KFPGA_ON(EndOfPacket, on_end)
                     KFPGA_ON(WantSecurity, on_want))
};

class Batcher : public kfpga::Actor<Batcher>
{
  uint64_t lo_[2] = {}, hi_[2] = {};   // events waiting for their line
  ap_uint<2> held_ = 0;

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
  kfpga::ActorRef feed;

  KFPGA_HANDLERS(KFPGA_ON(PackedEvent, on_event) KFPGA_ON(EndOfPacket, on_end))
};

} // namespace mdp3fpga
