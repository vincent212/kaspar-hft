#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * SbeDecoder: the MDP3 decoder on the card. It takes one CME packet at a time from
 * FeedRx, as 8-byte words, and turns it into messages for Filter: one message per
 * book entry, trade, security status or channel reset, and an EndOfPacket.
 *
 * Where it runs
 *
 *   decoder_task() in hls/feed_top.cpp is its process, an hls::task. Each clock the
 *   task calls
 *
 *     proc.step_stream<SbeDecoder>(ports, in);
 *
 *   which calls on_step(in) once and then writes out whatever on_step sent. on_step()
 *   is the only entry point; every other function here is called from it. `books` is
 *   its reference to the next actor, Filter.
 *
 * An actor with no handlers
 *
 *   SbeDecoder is an actor: it derives from kfpga::Actor, has its own id (kDecoder),
 *   holds an ActorRef and sends through the outbox with books.send(m, this), runs as a
 *   process of its own (kfpga::Process<SbeDecoder>, a group of one), and its state is
 *   touched only by its own steps.
 *
 *   It has no message handlers because it receives no messages. Its input is the raw
 *   word stream from FeedRx: wrapping every 8-byte word in a 64-byte envelope would
 *   waste seven eighths of the link. So its process calls Group::step_stream, which
 *   hands it one word stream per clock, instead of Group::step, which would read a
 *   message and call a handler. It is a source actor: it turns outside data into
 *   messages for the rest of the design, as a CPU actor reading a socket does.
 *
 *   Handlers could be added, for configuration from the CPU, say, but the step would
 *   then have to serve both the stream and the messages, and HLS has removed actors'
 *   state when they dispatched messages from a step of their own (HLS.md section 2).
 *   The decoder needs no configuration, so it has none.
 *
 * What it reads
 *
 *   packet   = MsgSeqNum (4) | SendingTime (8) | message ...
 *   message  = MsgSize (2, counts itself) | SBE header (8) | root block | groups ...
 *   SBE header = block length (2) | template id (2) | schema id (2) | version (2)
 *   group    = group header (block length, count) | entry ...
 *
 *   All little-endian. Nothing is aligned: messages start 4 bytes into a word after
 *   the 12-byte packet header, and anywhere at all after a message of odd length.
 *
 * What it sends to Filter
 *
 *   BookEntry       each entry of template 46, MBP book (outright and implied: Filter
 *                   drops implied)
 *   Trade           each entry of template 48, trade summary
 *   SecurityStatus  template 30
 *   ChannelReset    each entry of template 4
 *   EndOfPacket     after every packet: its sequence number, sending time, counts and
 *                   cycle stamps. The host checks the sequence numbers.
 *
 *   Every other template (47 MBO, 37 volume, 49, 50, 51 statistics and limits, the
 *   instrument definitions) is skipped by its MsgSize, as is the order group of 46
 *   and 48: the book entries come first in the message, so it never reads past them.
 *   At most one message is sent per clock.
 *
 * How it decodes: as the bytes arrive, storing nothing
 *
 *   The window. Two words, prev_ and cur_, and o_, the offset of the next unread byte
 *   in prev_. Each clock it takes the 8 bytes starting at o_, wherever that is, so a
 *   field that straddles two words costs nothing. When reading crosses into cur_,
 *   cur_ becomes prev_ and the next word is read in.
 *
 *   The state machine. state_ says what those bytes are:
 *
 *     kPacket0, kPacket1   the packet header: sequence number, sending time
 *     kMessage0            MsgSize, block length, template, schema
 *     kMessage1            version; decides whether this template is ours
 *     kRoot                the root block, into the entry register
 *     kRootRest            any root block bytes beyond the register, skipped
 *     kGroup               the group header: entry size and count
 *     kEntry               one entry, into the entry register; sends its message
 *     kEntryRest           any entry bytes beyond the register, skipped
 *     kTail                the rest of the message, skipped
 *     kDrain               the rest of a packet with a corrupt MsgSize, skipped
 *     kDone                sends EndOfPacket, then starts over at kPacket0
 *
 *   Each clock: work out how many bytes this state takes (n, at most 8), take them,
 *   act on them, move o_ on by n.
 *
 *   The entry register. reg_ holds up to 32 bytes, filled 8 a clock. When a root block
 *   or entry is complete, the decoders generated from CME's XML (gen_decoder.py ->
 *   generated/mdp3_messages.hpp) read its fields at fixed offsets: price at byte 0,
 *   size at 8, security id at 12, and so on. Fixed offsets are plain wires. So all of
 *   the misalignment is handled once, by the window, and none in the field reads.
 *
 *   Lengths come from the wire. Root blocks and entries are stepped by the block length
 *   in the message, not the schema's, so a newer CME version with longer blocks still
 *   decodes: the fields it knows are read, and the extra bytes skipped (kRootRest,
 *   kEntryRest).
 *
 * Timing
 *
 *   One clock per step (II = 1). The parser costs about 8 clocks per packet beyond the
 *   words themselves: the short headers take a clock each without using all 8 bytes.
 *
 *   The longest path in the design is the loop from "bytes taken this clock" to "where
 *   the next clock reads". To keep it short, n is chosen from the state and from
 *   flags already held in registers (msg_ge8_, rest_ge8_ and their _gt8 pairs), never
 *   from a comparison made in the same clock, and a truncated packet changes the state
 *   only, never n.
 *
 * Malformed packets
 *
 *   The packet's length is known only at its last word, so nothing can be checked
 *   against it in advance. A MsgSize below 10 sends the rest of the packet to kDrain.
 *   A packet that ends in the middle of a message ends the walk there (kDone); any
 *   events of that message already sent stay sent. EndOfPacket goes out either way.
 *
 * Latency stamps
 *
 *   now_ counts clocks. Each packet is stamped at its first word (first_word_) and its
 *   last (last_word_), and both travel in its EndOfPacket with the clock it was sent,
 *   so the tests can measure the card's time per packet.
 */

#include <ap_axi_sdata.h>
#include <ap_int.h>
#include <hls_stream.h>

#include "generated/mdp3_messages.hpp"
#include "kfpga/actor.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

// A word at the top level of the design: 8 bytes, byte 0 in the low bits, `keep` one
// bit per valid byte, `last` set on a packet's last word. The AXI-Stream type HLS uses
// for top-level ports.
using InWord = ap_axiu<64, 0, 0, 0>;
using InStream = hls::stream<InWord>;

// The same word inside the design. HLS accepts the AXI types on top-level ports only,
// so words between actors are this plain struct.
struct Word
{
  ap_uint<64> data;
  ap_uint<8> keep;
  bool last;
};
using WordStream = hls::stream<Word>;

// A top-level word as an internal one.
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
  // Which of our four templates the current message is.
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

  // Copy this clock's 8 bytes into the next 8-byte slot of the entry register.
  // chunk_ says which slot; the loop over slots becomes one write enable per slot.
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

  // Send a message to Filter and count it for the EndOfPacket.
  template <class M>
  void emit(const M &m)
  {
#pragma HLS INLINE
    books.send(m, this);
    ++events_;
  }

  // Set the count of block bytes beyond the register, with its two ready flags:
  // rest_ge8_ (at least 8 left: the next step takes 8) and rest_gt8_ (more than 8:
  // the next step is not the last). Computing them here, when the count changes,
  // keeps the comparison out of the clock that uses them.
  void set_rest(ap_uint<16> v)
  {
#pragma HLS INLINE
    rest_ = v;
    rest_ge8_ = v.range(15, 3) != 0;
    rest_gt8_ = v.range(15, 4) != 0 || (v[3] == 1 && v.range(2, 0) != 0);
  }

  // The same for the bytes left in the current message.
  void set_msg_left(ap_uint<16> v)
  {
#pragma HLS INLINE
    msg_left_ = v;
    msg_ge8_ = v.range(15, 3) != 0;
    msg_gt8_ = v.range(15, 4) != 0 || (v[3] == 1 && v.range(2, 0) != 0);
  }

  // Begin the next entry of the group: reload the register count and the bytes
  // beyond it from the values the group header set (every entry of a group is the
  // same length).
  void start_entry()
  {
#pragma HLS INLINE
    left_ = entry_left_;
    set_rest(entry_rest_);
    chunk_ = 0;
    state_ = kEntry;
  }

  // The root block is complete in the register. For a security status (30) that is
  // the whole message: send SecurityStatus and skip the rest. For the others keep
  // what every entry needs, the transact time and the match event indicator, and go on
  // to the group header, skipping any root bytes beyond the register first.
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

  // An entry is complete in the register. If it lies wholly inside the message
  // (`fits`), decode it with the generated decoder for its template and send the
  // message: BookEntry (46), Trade (48) or ChannelReset (4). Then skip any entry bytes
  // beyond the register, start the next entry, or, after the last, skip the rest of
  // the message. An entry that runs past the message's end means a corrupt length:
  // nothing is sent and the message is skipped.
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

  // One clock, the only entry point, called by decoder_task() through step_stream.
  //
  //   1. In kDone: send EndOfPacket, reset for the next packet, and stop.
  //   2. Take the 8 bytes at o_ from the window (a[0..7]).
  //   3. If enough bytes are there, choose n, the bytes this state takes, and act on
  //      them: read header fields, fill the register, send a message, move state.
  //   4. Count n off the message, and off the packet once its last word is in.
  //   5. Move the window on by n and read the next word if there is room for it.
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

    // The next 8 bytes: byte i of a[] is byte o_ + i of the window, from prev_ or,
    // past its end, from cur_.
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
    // Whether a[] holds 8 real bytes: prev_ is full and either cur_ is too or no bytes
    // of it are needed (o_ == 0). Once the packet's last word is in, take what is
    // there. Otherwise wait a clock for the next word.
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
      {
        // Read the flag before set_rest replaces it: it says whether this step takes
        // the last of the block.
        const bool done = !rest_gt8_;
        set_rest(rest_ - n);
        if (done)
          state_ = kGroup;
        break;
      }
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
      {
        const bool done = !rest_gt8_;   // before set_rest, as in kRootRest
        set_rest(rest_ - n);
        if (done)
        {
          if (remaining_ == 0)
            state_ = kTail;
          else
            start_entry();
        }
        break;
      }
      case kTail:
        if (!msg_gt8_)
          state_ = kMessage0;
        break;
      default:   // kDrain
        if (ended_ && rem_ <= 8)
          state_ = kDone;
        break;
      }

      // Count this step's bytes off the message. In kMessage0 the message has just
      // begun: its MsgSize, less the 8 bytes this step took.
      if (was == kMessage0)
        set_msg_left(ap_uint<16>(le<uint16_t, 2>(a, 0)) - 8);
      else
        set_msg_left(msg_left_ - n);

      // After the last word, rem_ counts the bytes left in the packet. Fewer than this
      // state needs means the packet is truncated: end it.
      if (ended_)
      {
        if (rem_ < n)
          state_ = kDone;   // truncated
        rem_ = rem_ - n;
      }
    }

    // Move the window on by n. If that crosses the end of prev_, cur_ becomes prev_.
    // Then read the next word into cur_ if it is empty and the packet is not over,
    // counting its valid bytes and stamping the packet's first and last words.
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
