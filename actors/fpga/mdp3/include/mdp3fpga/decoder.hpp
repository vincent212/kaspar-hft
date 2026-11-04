#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The MDP3 decoder on the card, an actor on a raw stream: UDP payload words
 * in, events (events.hpp) out to the actor that holds the books.
 *
 *   packet  = MsgSeqNum (4) | SendingTime (8) | message ...
 *   message = MsgSize (2, counts itself) | SBE header (8: block length,
 *             template id, schema id, version) | root block | groups ...
 *
 * It decodes as the packet arrives, nothing is stored. Each step of the
 * process (one clock) takes up to 8 bytes from a two-word window at any byte
 * offset, so a misaligned message costs nothing, and hands them to the state
 * machine, which sends at most one event a step. A root block or an entry is
 * collected in an entry register; when complete, the generated decoders
 * (generated/mdp3_messages.hpp) read it at fixed offsets, plain wires.
 *
 * Root blocks and group entries are stepped by the lengths on the wire, not
 * the schema's, so a newer CME version with longer blocks still decodes.
 * Templates other than 46, 48, 30 and 4 are skipped by MsgSize, as is the
 * order-entry group of 46 and 48: book entries come first.
 *
 * Nothing can be checked against the packet's length before decoding, since
 * that length is known only at its last word. A corrupt MsgSize ends the walk
 * of that packet, and a truncated message may already have sent events; the
 * EndOfPacket still goes out.
 *
 * Latency: a cycle counter stamps each packet at its first word and at its
 * EndOfPacket. Both travel in the EndOfPacket.
 */

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

#include "generated/mdp3_messages.hpp"
#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

// The UDP payload, 8 bytes a word, little-endian, as the network side
// delivers it: `keep` marks the valid bytes, `last` the packet's last word.
using InWord = ap_axiu<64, 0, 0, 0>;
using InStream = hls::stream<InWord>;

// The same word inside the design: HLS takes the AXI types on top-level ports only.
struct Word
{
  ap_uint<64> data;
  ap_uint<8> keep;
  bool last;
};
using WordStream = hls::stream<Word>;

inline Word to_word(const InWord &w)
{
#pragma HLS INLINE
  Word r;
  r.data = w.data;
  r.keep = w.keep;
  r.last = w.last;
  return r;
}

constexpr int kEntryRegister = 32;   // bytes: the largest block we read fields from

class SbeDecoder : public kfpga::Actor<SbeDecoder>
{
  enum State : uint8_t
  {
    kPacket0, kPacket1,      // the packet header
    kMessage0, kMessage1,    // the message header
    kRoot, kRootRest,        // the root block, and what is longer than we read
    kGroup,                  // the group header
    kEntry, kEntryRest,      // a group entry, and what is longer than we read
    kTail,                   // the rest of the message
    kDrain,                  // the rest of the packet, after a corrupt length
    kDone,                   // send EndOfPacket
  };
  enum Kind : uint8_t { kBook, kTrades, kStatus, kReset };

  // The window: two words, `prev` first. pf_, cf_: each holds a word; o_ the
  // offset of the next byte in prev. Every word but a packet's last has 8
  // bytes. Once the last has been read, rem_ counts the bytes left.
  ap_uint<64> prev_ = 0, cur_ = 0;
  bool pf_ = false, cf_ = false;
  ap_uint<3> o_ = 0;
  bool ended_ = false;       // the packet's last word has been read
  bool started_ = false;     // its first word has
  ap_uint<5> rem_ = 0;

  State state_ = kPacket0;
  Kind kind_ = kBook;
  uint8_t reg_[kEntryRegister] = {};   // the entry register
  ap_uint<3> chunk_ = 0;               // next 8-byte slot of it

  // Counted in bytes, wide enough for any UDP payload. The bytes taken in a
  // step decide where the next step reads, the longest path in the design, so
  // what decides them is a flag kept ready in a register, not a comparison made
  // in the step: _ge8 "at least 8 left", _gt8 "more than 8 left".
  ap_uint<16> msg_left_ = 0;    // bytes left in this message
  bool msg_ge8_ = false, msg_gt8_ = false;
  ap_uint<6> left_ = 0;         // bytes left to read into the register
  ap_uint<16> rest_ = 0;        // bytes of this block beyond the register
  bool rest_ge8_ = false, rest_gt8_ = false;
  ap_uint<6> entry_left_ = 0;   // left_ and rest_ for each entry of this group
  ap_uint<16> entry_rest_ = 0;
  uint16_t entry_ = 0, version_ = 0;
  ap_uint<6> block_left_ = 0;   // and for this message's root block
  ap_uint<16> block_rest_ = 0;
  bool wanted_ = false;         // the message's schema and template are ours, and its root fits
  bool fits_ = false;           // this entry fits in the message
  ap_uint<8> remaining_ = 0;    // group entries left

  uint32_t seq_ = 0;
  uint64_t sending_ = 0, transact_ = 0;
  uint8_t match_ = 0;
  uint32_t events_ = 0, messages_ = 0, bytes_ = 0;
  uint32_t now_ = 0, first_word_ = 0, last_word_ = 0;   // the cycle counter, and this packet's stamps

  void store(const uint8_t a[8])
  {
#pragma HLS INLINE
    for (int s = 0; s < kEntryRegister / 8; ++s)
    {
#pragma HLS UNROLL
      if (chunk_ == s)
        for (int i = 0; i < 8; ++i)
        {
#pragma HLS UNROLL
          reg_[8 * s + i] = a[i];
        }
    }
    chunk_ = chunk_ + 1;
  }

  template <class M>
  void emit(const M &m)
  {
#pragma HLS INLINE
    books.send(m, this);
    ++events_;
  }

  void set_rest(ap_uint<16> v)
  {
#pragma HLS INLINE
    rest_ = v;
    rest_ge8_ = v.range(15, 3) != 0;
    rest_gt8_ = v.range(15, 4) != 0 || (v[3] == 1 && v.range(2, 0) != 0);
  }

  void set_msg_left(ap_uint<16> v)
  {
#pragma HLS INLINE
    msg_left_ = v;
    msg_ge8_ = v.range(15, 3) != 0;
    msg_gt8_ = v.range(15, 4) != 0 || (v[3] == 1 && v.range(2, 0) != 0);
  }

  void start_entry()
  {
#pragma HLS INLINE
    left_ = entry_left_;
    set_rest(entry_rest_);
    chunk_ = 0;
    state_ = kEntry;
  }

  void root_done()
  {
#pragma HLS INLINE
    using namespace mdp3gen;
    if (kind_ == kStatus)
    {
      const SecurityStatus30::Root r = SecurityStatus30::root(reg_, 0, version_);
      SecurityStatus m;
      m.seq = seq_;
      m.transact_time = r.TransactTime;
      m.security_id = r.SecurityID;
      m.trade_date = r.TradeDate;
      m.flags = r.SecurityTradingStatus | (uint32_t(r.HaltReason) << 8) | (uint32_t(r.SecurityTradingEvent) << 16) |
                (uint32_t(r.MatchEventIndicator) << 24);
      emit(m);
      state_ = kTail;
      return;
    }
    if (kind_ == kReset)
    {
      const ChannelReset4::Root r = ChannelReset4::root(reg_, 0, version_);
      transact_ = r.TransactTime;
      match_ = r.MatchEventIndicator;
    }
    else   // 46 and 48 share this layout; the generated decoders agree
    {
      const MDIncrementalRefreshBook46::Root r = MDIncrementalRefreshBook46::root(reg_, 0, version_);
      transact_ = r.TransactTime;
      match_ = r.MatchEventIndicator;
    }
    state_ = rest_ != 0 ? kRootRest : kGroup;
  }

  void entry_done(bool fits)
  {
#pragma HLS INLINE
    using namespace mdp3gen;
    if (!fits)
    {
      state_ = kTail;
      return;
    }
    if (kind_ == kBook)
    {
      const auto e = MDIncrementalRefreshBook46::NoMDEntries_entry(reg_, 0, version_);
      BookEntry m;
      m.seq = seq_;
      m.transact_time = transact_;
      m.price = e.MDEntryPx;
      m.size = e.MDEntrySize;
      m.security_id = e.SecurityID;
      m.rpt_seq = e.RptSeq;
      m.orders = e.NumberOfOrders;
      m.flags = e.MDPriceLevel | (uint32_t(e.MDUpdateAction) << 8) | (uint32_t(e.MDEntryType) << 16) |
                (uint32_t(match_) << 24);
      m.tradeable_size = e.TradeableSize;
      emit(m);
    }
    else if (kind_ == kTrades)
    {
      const auto e = MDIncrementalRefreshTradeSummary48::NoMDEntries_entry(reg_, 0, version_);
      Trade m;
      m.seq = seq_;
      m.transact_time = transact_;
      m.price = e.MDEntryPx;
      m.size = e.MDEntrySize;
      m.security_id = e.SecurityID;
      m.rpt_seq = e.RptSeq;
      m.orders = e.NumberOfOrders;
      m.flags = e.AggressorSide | (uint32_t(e.MDUpdateAction) << 8) | (uint32_t(match_) << 24);
      m.trade_id = e.MDTradeEntryID;
      emit(m);
    }
    else
    {
      const auto e = ChannelReset4::NoMDEntries_entry(reg_, 0, version_);
      ChannelReset m;
      m.seq = seq_;
      m.transact_time = transact_;
      m.appl_id = e.ApplID;
      m.match_event = match_;
      emit(m);
    }
    remaining_ = remaining_ - 1;
    if (rest_ != 0)
      state_ = kEntryRest;
    else if (remaining_ == 0)
      state_ = kTail;
    else
      start_entry();
  }

public:
  static constexpr kfpga::ActorId kId = kDecoder;
  kfpga::ActorRef books;

  // One clock: consume up to 8 bytes, take in the next word if there is room.
  void on_step(WordStream &in)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=reg_ type=complete
    using namespace mdp3gen;
    now_ = now_ + 1;

    if (state_ == kDone)
    {
      EndOfPacket m;
      m.seq = seq_;
      m.sending_time = sending_;
      m.events = events_;
      m.messages = messages_;
      m.bytes = bytes_;
      m.cycle_in = first_word_;
      m.cycle_out = now_;
      m.cycle_last = last_word_;
      books.send(m, this);
      seq_ = 0;
      sending_ = 0;
      events_ = 0;
      messages_ = 0;
      bytes_ = 0;
      pf_ = false;
      cf_ = false;
      o_ = 0;
      rem_ = 0;
      ended_ = false;
      started_ = false;
      state_ = kPacket0;
      return;
    }

    // The next 8 bytes.
    uint8_t a[8];
#pragma HLS ARRAY_PARTITION variable=a type=complete
    for (int i = 0; i < 8; ++i)
    {
#pragma HLS UNROLL
      const ap_uint<4> at = ap_uint<4>(o_) + i;
      const ap_uint<3> byte = at.range(2, 0);
      const ap_uint<64> from = at[3] ? cur_ : prev_;
      a[i] = from.range(8 * byte + 7, 8 * byte);
    }
    const bool can = (pf_ && (cf_ || o_ == 0 || ended_)) || (ended_ && !cf_);

    // The bytes this step takes, from the state and ready flags alone. A
    // truncated packet changes the state, not this: the window is thrown away.
    ap_uint<4> n = 0;
    if (can)
    {
      switch (state_)
      {
      case kPacket1: n = 4; break;
      case kMessage1: n = 2; break;
      case kGroup: n = 3; break;
      case kRoot:
      case kEntry: n = left_.range(5, 3) != 0 ? ap_uint<4>(8) : ap_uint<4>(left_.range(2, 0)); break;
      case kRootRest:
      case kEntryRest: n = rest_ge8_ ? ap_uint<4>(8) : ap_uint<4>(rest_.range(2, 0)); break;
      case kTail: n = msg_ge8_ ? ap_uint<4>(8) : ap_uint<4>(msg_left_.range(2, 0)); break;
      default: n = 8; break;
      }

      const bool part_last = left_ <= 8;
      const State was = state_;
      switch (was)
      {
      case kPacket0:
        seq_ = le<uint32_t, 4>(a, 0);
        sending_ = le<uint32_t, 4>(a, 4);
        state_ = ended_ && rem_ < 12 ? kDone : kPacket1;
        break;
      case kPacket1:
        sending_ |= uint64_t(le<uint32_t, 4>(a, 0)) << 32;
        state_ = kMessage0;
        break;
      case kMessage0:
      {
        const ap_uint<16> size = le<uint16_t, 2>(a, 0);
        const ap_uint<16> block = le<uint16_t, 2>(a, 2);
        const uint16_t tid = le<uint16_t, 2>(a, 4);
        const bool book = tid == MDIncrementalRefreshBook46::kTemplateId;
        const bool trades = tid == MDIncrementalRefreshTradeSummary48::kTemplateId;
        const bool status = tid == SecurityStatus30::kTemplateId;
        const bool reset = tid == ChannelReset4::kTemplateId;
        kind_ = book ? kBook : trades ? kTrades : status ? kStatus : kReset;
        wanted_ = le<uint16_t, 2>(a, 6) == kSchemaId && (book || trades || status || reset) &&
                  size >= ap_uint<17>(block) + 10;
        const bool over = block > kEntryRegister;
        block_left_ = over ? ap_uint<6>(kEntryRegister) : ap_uint<6>(block);
        block_rest_ = over ? ap_uint<16>(block - kEntryRegister) : ap_uint<16>(0);
        state_ = ended_ && rem_ < 10 ? kDone : size < 10 ? kDrain : kMessage1;
        break;
      }
      case kMessage1:
        version_ = le<uint16_t, 2>(a, 0);
        messages_ = messages_ + 1;
        if (wanted_)
        {
          left_ = block_left_;
          set_rest(block_rest_);
          chunk_ = 0;
          state_ = kRoot;
        }
        else
          state_ = kTail;
        break;
      case kRoot:
        store(a);
        left_ = left_ - ap_uint<6>(n);
        if (part_last)
          root_done();
        break;
      case kRootRest:
        set_rest(rest_ - n);
        if (!rest_gt8_)
          state_ = kGroup;
        break;
      case kGroup:
      {
        // 46, 48 and 4 use the same group header, groupSize.
        const GroupHeader h = MDIncrementalRefreshBook46::NoMDEntries_header(a, 0);
        const ap_uint<16> size = h.block_length;
        const bool over = size > kEntryRegister;
        entry_ = h.block_length;
        entry_left_ = over ? ap_uint<6>(kEntryRegister) : ap_uint<6>(size);
        entry_rest_ = over ? ap_uint<16>(size - kEntryRegister) : ap_uint<16>(0);
        remaining_ = h.count;
        left_ = over ? ap_uint<6>(kEntryRegister) : ap_uint<6>(size);
        set_rest(over ? ap_uint<16>(size - kEntryRegister) : ap_uint<16>(0));
        chunk_ = 0;
        state_ = h.count == 0 ? kTail : kEntry;
        break;
      }
      case kEntry:
      {
        // Checked as the entry starts, before this step takes its bytes.
        const bool first = chunk_ == 0;
        const bool fits = first ? msg_left_ >= entry_ : fits_;
        fits_ = fits;
        store(a);
        left_ = left_ - ap_uint<6>(n);
        if (part_last)
          entry_done(fits);
        break;
      }
      case kEntryRest:
        set_rest(rest_ - n);
        if (!rest_gt8_)
        {
          if (remaining_ == 0)
            state_ = kTail;
          else
            start_entry();
        }
        break;
      case kTail:
        if (!msg_gt8_)
          state_ = kMessage0;
        break;
      default:   // kDrain
        if (ended_ && rem_ <= 8)
          state_ = kDone;
        break;
      }

      if (was == kMessage0)
        set_msg_left(ap_uint<16>(le<uint16_t, 2>(a, 0)) - 8);
      else
        set_msg_left(msg_left_ - n);

      if (ended_)
      {
        if (rem_ < n)
          state_ = kDone;   // truncated
        rem_ = rem_ - n;
      }
    }

    // Move the window, then take the next word if there is room for it.
    const ap_uint<4> next = ap_uint<4>(o_) + n;
    const bool shift = next[3] == 1 || !pf_;
    o_ = next.range(2, 0);
    bool pf = pf_, cf = cf_;
    if (shift)
    {
      prev_ = cur_;
      pf = cf;
      cf = false;
    }
    if (!cf && !ended_)
    {
      Word w;
      if (in.read_nb(w))
      {
        ap_uint<4> count = 0;
        for (int i = 0; i < 8; ++i)
        {
#pragma HLS UNROLL
          count += w.keep[i] ? 1 : 0;
        }
        cur_ = w.data;
        cf = true;
        ended_ = w.last;
        if (w.last)
          last_word_ = now_;
        const ap_uint<4> left_in_prev = pf ? ap_uint<4>(8 - o_) : ap_uint<4>(0);
        rem_ = left_in_prev + count;
        bytes_ = bytes_ + count;
        if (!started_)
          first_word_ = now_;
        started_ = true;
      }
    }
    pf_ = pf;
    cf_ = cf;
  }
};

} // namespace mdp3fpga
