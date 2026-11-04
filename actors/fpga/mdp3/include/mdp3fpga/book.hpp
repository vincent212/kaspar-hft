#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The book on the card, an actor: events in from the decoder, and per packet
 * that touched the book, one batch out: the packet's events, the book, then
 * BatchEnd. Nothing leaves mid-packet.
 *
 * Phase 2 of ../../../MDP3_PLAN.md: one instrument, outright prices. Implied
 * entries are dropped, from the book and the events. The book is registers, 10
 * levels a side; one MBP entry is a fixed shift, one step of the process.
 *
 *   new         insert at the level, deeper levels move down
 *   change,
 *   overlay     overwrite the level
 *   delete      remove the level, deeper levels move up
 *   delete thru clear the side
 *   delete from remove that many levels from the top
 *
 * Between a handler and the next message the process can be publishing, one
 * envelope a step, so it is written on on_step (kfpga::Group::step_stream),
 * which runs the handlers through the actor's dispatch when it takes a message.
 *
 * No recovery: a sequence gap sets a flag that stays, on every batch after it.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

struct Level
{
  int64_t price;
  int32_t size;
  int32_t orders;
};

// A level as stored: one register. HLS took an array of Level kept in the actor
// object for a constant (SYNCHK 200-51).
using LevelBits = ap_uint<128>;

inline LevelBits pack(const Level &l)
{
#pragma HLS INLINE
  LevelBits b = 0;
  b.range(63, 0) = static_cast<uint64_t>(l.price);
  b.range(95, 64) = static_cast<uint32_t>(l.size);
  b.range(127, 96) = static_cast<uint32_t>(l.orders);
  return b;
}

inline Level unpack(const LevelBits &b)
{
#pragma HLS INLINE
  Level l = {};
  l.price = static_cast<int64_t>(static_cast<uint64_t>(b.range(63, 0)));
  l.size = static_cast<int32_t>(static_cast<uint32_t>(b.range(95, 64)));
  l.orders = static_cast<int32_t>(static_cast<uint32_t>(b.range(127, 96)));
  return l;
}

constexpr uint8_t kBid = '0';
constexpr uint8_t kOffer = '1';
constexpr uint8_t kBookReset = 'J';

constexpr int kBatchQueue = 64;      // a packet holds about 44 entries at most

// BookBank's batch queue, `held`, is a FIFO from its process to itself, depth
// kBatchQueue. The top declares it, as every FIFO: HLS builds none for a stream
// kept in an actor object of an hls::task, and ties its ports off to "not empty,
// data zero".

class BookBank : public kfpga::Actor<BookBank>
{
  enum Stage : uint8_t { kTaking, kEvents, kBook, kEnd };

  LevelBits bid_[kLevels], ask_[kLevels];
  kfpga::Envelope pending_;      // a handler's event, for on_step to queue
  bool have_pending_ = false;
  Stage stage_ = kTaking;
  ap_uint<4> part_ = 0;
  bool touched_ = false, gap_ = false, seen_ = false;
  uint32_t last_seq_ = 0;
  uint32_t events_ = 0;
  EndOfPacket eop_;
  uint32_t now_ = 0;
  uint32_t unhandled_ = 0;   // messages with no handler

  template <class M>
  void hold(const M &m)
  {
#pragma HLS INLINE
    pending_ = kfpga::encode(m, kId, kFeed);
    have_pending_ = true;
    ++events_;
  }

  // The side `s` after one MBP entry, in `n`.
  static void apply(const Level (&s)[kLevels], Level (&n)[kLevels], int i, uint8_t action, const Level &l)
  {
#pragma HLS INLINE
    const Level none = {};
    for (int k = 0; k < kLevels; ++k)
    {
#pragma HLS UNROLL
      const Level below = k + 1 < kLevels ? s[k + 1] : none;
      const Level above = k > 0 ? s[k - 1] : none;
      switch (action)
      {
      case 0:   // new
        n[k] = k < i ? s[k] : k == i ? l : above;
        break;
      case 2:   // delete
        n[k] = k < i ? s[k] : below;
        break;
      case 3:   // delete thru
        n[k] = none;
        break;
      case 4:   // delete from: the top i + 1 levels
        n[k] = k + i + 1 < kLevels ? s[k + i + 1] : none;
        break;
      default:  // change, overlay
        n[k] = k == i ? l : s[k];
        break;
      }
    }
  }

  void clear()
  {
#pragma HLS INLINE
    for (int k = 0; k < kLevels; ++k)
    {
#pragma HLS UNROLL
      bid_[k] = 0;
      ask_[k] = 0;
    }
  }

  void on_book(const BookEntry &m)
  {
#pragma HLS INLINE
    if (m.security_id != security)
      return;
    const uint8_t type = m.entry_type();
    if (type == kBookReset)
    {
      clear();
      touched_ = true;
      hold(m);
      return;
    }
    if (type != kBid && type != kOffer)
      return;   // implied, ignored
    hold(m);
    touched_ = true;
    const int i = static_cast<int>(m.level()) - 1;
    if (i < 0 || i >= kLevels)
      return;
    Level l = {};
    l.price = m.price;
    l.size = m.size;
    l.orders = m.orders;
    Level now[kLevels], n[kLevels];
#pragma HLS ARRAY_PARTITION variable=now type=complete
#pragma HLS ARRAY_PARTITION variable=n type=complete
    for (int k = 0; k < kLevels; ++k)
    {
#pragma HLS UNROLL
      now[k] = unpack(type == kBid ? bid_[k] : ask_[k]);
    }
    apply(now, n, i, m.action(), l);
    for (int k = 0; k < kLevels; ++k)
    {
#pragma HLS UNROLL
      if (type == kBid)
        bid_[k] = pack(n[k]);
      else
        ask_[k] = pack(n[k]);
    }
  }

  void on_trade(const Trade &m)
  {
#pragma HLS INLINE
    if (m.security_id == security)
    {
      hold(m);
      touched_ = true;
    }
  }

  void on_status(const SecurityStatus &m)
  {
#pragma HLS INLINE
    if (m.security_id == security)
    {
      hold(m);
      touched_ = true;
    }
  }

  void on_reset(const ChannelReset &m)
  {
#pragma HLS INLINE
    clear();
    hold(m);
    touched_ = true;
  }

  void on_end(const EndOfPacket &m)
  {
#pragma HLS INLINE
    if (seen_ && m.seq != last_seq_ + 1)
      gap_ = true;
    seen_ = true;
    last_seq_ = m.seq;
    if (touched_)
    {
      eop_ = m;
      stage_ = kEvents;
    }
  }

public:
  static constexpr kfpga::ActorId kId = kBooks;
  kfpga::ActorRef feed;      // the batches go here
  int32_t security = 0;      // the one instrument of phase 2

  KFPGA_HANDLERS(KFPGA_ON(BookEntry, on_book) KFPGA_ON(Trade, on_trade) KFPGA_ON(SecurityStatus, on_status)
                     KFPGA_ON(ChannelReset, on_reset) KFPGA_ON(EndOfPacket, on_end))

  // One clock: take one event, or publish one envelope of a batch.
  void on_step(kfpga::Fifo &in, kfpga::Fifo &held_in, kfpga::Fifo &held_out)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=bid_ type=complete
#pragma HLS ARRAY_PARTITION variable=ask_ type=complete
    now_ = now_ + 1;
    switch (stage_)
    {
    case kTaking:
    {
      kfpga::Envelope e;
      if (!in.empty())
      {
        e = in.read();
        if (!kfpga_dispatch(e))
          ++unhandled_;
      }
      if (have_pending_)
        held_out.write(pending_);   // never full: a packet holds under kBatchQueue events
      have_pending_ = false;
      break;
    }
    case kEvents:
    {
      if (!held_in.empty())
        kfpga_emit(held_in.read(), feed.port());
      else
      {
        part_ = 0;
        stage_ = kBook;
      }
      break;
    }
    case kBook:
    {
      const bool offers = part_ >= kBookParts / 2;
      const int k = (offers ? int(part_) - kBookParts / 2 : int(part_)) * kLevelsPerPart;
      const Level a = unpack(offers ? ask_[k] : bid_[k]);
      const Level b = unpack(offers ? ask_[k + 1] : bid_[k + 1]);
      BookPart m;
      m.seq = eop_.seq;
      m.part = part_;
      m.price0 = a.price;
      m.size0 = a.size;
      m.orders0 = a.orders;
      m.price1 = b.price;
      m.size1 = b.size;
      m.orders1 = b.orders;
      feed.send(m, this);
      part_ = part_ + 1;
      if (part_ == kBookParts)
        stage_ = kEnd;
      break;
    }
    default:
    {
      BatchEnd m;
      m.seq = eop_.seq;
      m.security_id = security;
      m.events = events_;
      m.gap = gap_ ? 1 : 0;
      m.cycle_in = eop_.cycle_in;
      m.cycle_last = eop_.cycle_last;
      m.cycle_out = now_;
      feed.send(m, this);
      events_ = 0;
      touched_ = false;
      stage_ = kTaking;
      break;
    }
    }
  }
};

} // namespace mdp3fpga
