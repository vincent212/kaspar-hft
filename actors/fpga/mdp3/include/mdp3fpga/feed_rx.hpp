#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * FeedRx: the first actor on the card. It takes Ethernet frames from the MAC and
 * passes on only the MDP3 packets of one multicast group and port, channel A's,
 * stripped of their Ethernet, IP and UDP headers, to SbeDecoder. Every other
 * frame is dropped.
 *
 * Where it runs
 *
 *   rx_task() in hls/feed_top.cpp is FeedRx's process: an hls::task, so a circuit
 *   of its own that runs from reset and never returns. Each clock it calls
 *
 *     rx.on_step(frames, words);
 *
 *   on_step() is the only entry point. It reads at most one word from `frames`,
 *   writes at most one word to `words`, and returns. Everything else in this
 *   class is called from it. The task holds the object as `static FeedRx rx`, so
 *   its members are registers that keep their values from one clock to the next.
 *
 * A stream actor that also has messages
 *
 *   Its main sides are raw word streams: frames in, packets out. Besides them it
 *   talks to one actor, Igmp: it receives Listen, which says what to accept, and
 *   sends IgmpQuery. So it runs as SbeDecoder does, in a group of one,
 *   kfpga::Process<FeedRx>, whose step_stream calls on_step and then writes what it
 *   sent. on_step reads the Listen FIFO itself, at most one message a clock, and
 *   hands it to its handler through kfpga_dispatch.
 *
 * Input and output
 *
 *   frames  one Ethernet frame after another, 8 bytes a word, byte 0 of the frame
 *           in the low byte of the first word; `last` marks a frame's last word.
 *           The MAC is assumed to have removed the FCS.
 *   words   the UDP payload of each frame we keep, which is one MDP3 packet,
 *           realigned so that the packet's byte 0 is the low byte of the first
 *           word; `keep` marks the valid bytes of the last word, `last` marks it.
 *           This is exactly what SbeDecoder reads.
 *
 * The frame
 *
 *   Ethernet  14 bytes, or 18 with an 802.1Q (VLAN) tag
 *   IPv4      20 bytes: options are not expected on this feed, and such a frame
 *             is dropped
 *   UDP        8 bytes
 *   payload   the MDP3 packet
 *
 *   Every header lies in the first 48 bytes, the first six words, so the decision
 *   to keep a frame is taken once, when the sixth word arrives (decide()).
 *
 * The states
 *
 *   kHeader  store words 0 to 5 in h_. At the sixth, decide(): keep the frame
 *            (kBody) or drop it (kDrain). A frame shorter than six words is
 *            dropped.
 *   kBody    send the payload, one word per clock. It starts 2 bytes into word 5
 *            (6 with a VLAN tag), so it is not aligned: each word sent is the
 *            rest of the previous word (prev_, from offset o_) joined to the
 *            start of the word just read. The UDP length (left_) says where the
 *            payload ends, so Ethernet padding after it is never sent.
 *   kIgmp    the word after the headers of an IGMP frame: read the IGMP type, group
 *            and maximum response time from it and the header words, send a query
 *            to Igmp, then drain the rest.
 *   kDrain   read and discard the rest of a frame that is not ours, up to `last`.
 *
 *   Example, no VLAN tag (o_ = 2):
 *
 *     word 5 in:   frame bytes 40-47: the last 2 bytes of the UDP header, then
 *                  payload bytes 0-5; held in prev_
 *     word 6 in:   payload 6-13
 *     word 0 out:  payload 0-7  = prev_ bytes 2-7  | word 6 bytes 0-1
 *     word 1 out:  payload 8-15 = word 6 bytes 2-7 | word 7 bytes 0-1
 *
 *   When the bytes still to send are all in prev_, the last word goes out without
 *   reading another, so a frame's last word costs no extra clock.
 *
 * Timing
 *
 *   One word in and at most one word out per clock (II = 1), so FeedRx keeps up
 *   with the wire. A kept frame is delayed by about one word, the join of two input
 *   words.
 *
 * Settings and counters
 *
 *   group, port  the multicast group and UDP port accepted, set by Listen from Igmp;
 *                0 and 0, nothing, until then.
 *   accepted,    frames kept and dropped.
 *   dropped
 *
 * Not checked: IP and UDP checksums, which the Ethernet FCS covers, and IP fragments
 * (CME does not fragment this feed). The MAC checks the FCS, but only at a frame's
 * last word, while FeedRx passes words on as they arrive; carrying its verdict to the
 * End line, so that the host treats a damaged frame as a gap, is not built yet
 * (ACTORS.md, "The MAC").
 *
 * What arrives on the port, and joining the multicast group
 *
 *   What reaches the card's port depends on the switch in front of it. A switch
 *   with IGMP snooping forwards a multicast group only to the ports that have
 *   joined it; one without floods every group on the VLAN. FeedRx is correct
 *   either way, since it keeps one group and port and drops the rest, but flooding
 *   spends the card's input on traffic it discards. This box takes CME on VLANs
 *   (feed A on 3008 in pcap/record_310.sh), hence the 802.1Q handling.
 *
 *   The card joins the group itself, through the Igmp actor (igmp.hpp): the port
 *   belongs to the FPGA, so the kernel cannot send the membership report for it.
 *   FeedRx has two parts in that:
 *
 *     - Listen, from Igmp, sets the group and port it accepts. Igmp sends it when
 *       the CPU joins a group, and 0 and 0, accept nothing, when it leaves. Until
 *       the first Listen nothing is accepted.
 *     - IGMP frames (IPv4 protocol 2) are not dropped: a membership query (type
 *       0x11) goes to Igmp as an IgmpQuery, with its group (0 for a general query)
 *       and maximum response time, so that Igmp can answer it. A query's IP
 *       header carries the 4-byte Router Alert option, so its IGMP fields can end
 *       at byte 49 with a VLAN tag, past the six header words: kIgmp reads one
 *       word more.
 *
 *   The alternative needs no card logic: the switch is configured to send the
 *   group to the card's port statically. That depends on whoever runs the switch,
 *   and is lost silently if the configuration changes.
 *
 * One channel
 *
 *   FeedRx keeps one group and port: channel A of one CME channel. Several channels
 *   need:
 *
 *     - FeedRx: a table of group and port pairs, compared in parallel as Filter
 *       compares security ids, giving each kept frame a channel index;
 *     - the channel index carried with the packet through SbeDecoder, Filter and
 *       Batcher, and in the closing line, since the host checks sequence numbers
 *       per channel and a channel reset applies to that channel's securities only;
 *     - Igmp: one membership report per group;
 *     - the host: a sequence check and gap flag per channel.
 *
 *   SbeDecoder needs no change of its own: it holds no state from one packet to the
 *   next, so packets of different channels may interleave freely. Throughput is not
 *   the limit: at 8 bytes a clock and 250 MHz the decoder takes 16 Gbit/s, more than
 *   one 10 GbE port delivers. Channels arriving on different ports would need a
 *   FeedRx per port and an actor that merges whole frames into the decoder.
 *
 *   Not built yet: more than one channel.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/events.hpp"
#include "mdp3fpga/igmp.hpp"

namespace mdp3fpga {

constexpr int kHeaderWords = 6;

class FeedRx : public kfpga::Actor<FeedRx>
{
  enum State : uint8_t { kHeader, kBody, kIgmp, kDrain };

  State state_ = kHeader;
  ap_uint<64> h_[kHeaderWords] = {};   // the first six words of the frame, every header
  ap_uint<3> n_ = 0;          // header words held so far
  ap_uint<64> prev_ = 0;      // the input word the next payload bytes start in
  ap_uint<3> o_ = 0;          // where in prev_ they start: 2, or 6 with a VLAN tag
  ap_uint<16> left_ = 0;      // payload bytes still to send, from the UDP length
  bool ended_ = false;        // the frame's last word has been read
  ap_uint<4> igmp_at_ = 0;    // an IGMP frame: where its IGMP part starts, from byte 32
  bool listening_ = false;    // a group has been set by Listen: accept it

  // A header field: N bytes (1 to 4) at byte `at` of the frame, read from h_. Network
  // headers are big-endian, so the first byte is the most significant. `at` is a
  // constant at every call, so in hardware this is wires, not logic.
  template <int N>
  ap_uint<8 * N> field(int at) const
  {
#pragma HLS INLINE
    ap_uint<8 * N> v = 0;
    for (int i = 0; i < N; ++i)
    {
#pragma HLS UNROLL
      const int b = at + i;
      const ap_uint<8> byte = h_[b / 8].range(8 * (b % 8) + 7, 8 * (b % 8));
      v.range(8 * (N - 1 - i) + 7, 8 * (N - 1 - i)) = byte;
    }
    return v;
  }

  // The payload has been sent. If the frame's last word has already been read, the
  // next word starts a new frame; if not, the rest of this frame is discarded.
  void finish()
  {
#pragma HLS INLINE
    state_ = ended_ ? kHeader : kDrain;
    n_ = 0;
  }

  // Called once per frame, when the sixth header word arrives. Reads every field it
  // needs from h_ in this one step, and keeps the frame only if it is IPv4 with no
  // options (0x45), UDP (17), addressed to `group` and `port`, with a UDP length of at
  // least its own 8 bytes. An IGMP frame (protocol 2, with or without the 4-byte
  // Router Alert option) goes to kIgmp. A VLAN tag (EtherType 0x8100 at byte 12) moves every later
  // field 4 bytes on. For a kept frame it sets up kBody: the payload starts in the last
  // header word, at byte 2 (6 with a tag), and is UDP length - 8 bytes long.
  void decide()
  {
#pragma HLS INLINE
    const bool tag = field<2>(12) == 0x8100;
    const ap_uint<16> type = tag ? field<2>(16) : field<2>(12);
    const ap_uint<8> version = tag ? field<1>(18) : field<1>(14);
    const ap_uint<8> proto = tag ? field<1>(27) : field<1>(23);
    const ap_uint<32> dst = tag ? field<4>(34) : field<4>(30);
    const ap_uint<16> dport = tag ? field<2>(40) : field<2>(36);
    const ap_uint<16> ulen = tag ? field<2>(42) : field<2>(38);
    const bool ours = listening_ && type == 0x0800 && version == 0x45 && proto == 17 && dst == group &&
                      dport == port && ulen >= 8;
    // IGMP with no options, Router Alert (RFC 2236 requires it), or up to 8 bytes of
    // options: its 8 bytes then still lie within header words 4 and 5 and the next.
    const bool is_igmp = type == 0x0800 && version >= 0x45 && version <= 0x47 && proto == 2;
    n_ = 0;
    if (is_igmp && !ended_)
    {
      // 14 + 20 bytes, + 4 for a tag, + 4 per option word, counted from byte 32: 2 to 14.
      igmp_at_ = ap_uint<4>(2 + (tag ? 4 : 0)) + (ap_uint<4>(version.range(3, 0) - 5) << 2);
      state_ = kIgmp;
    }
    else if (ours)
    {
      prev_ = h_[kHeaderWords - 1];
      o_ = tag ? 6 : 2;
      left_ = ulen - 8;
      state_ = kBody;
      ++accepted;
    }
    else
    {
      state_ = ended_ ? kHeader : kDrain;
      ++dropped;
    }
  }

  // The word after the headers of an IGMP frame. The IGMP part starts between byte 34
  // and 46 of the frame, so it lies in bytes 32 to 55: header words 4 and 5 and this
  // one. A membership query goes to Igmp; anything else IGMP (another host's report,
  // a leave) is dropped.
  //
  // A query is control traffic, and must never hold up the data path. The send is a
  // blocking write, but it cannot block: a query takes a whole frame, at least eight
  // clocks, and Igmp takes one off its FIFO every clock or two whatever it is doing
  // (igmp.hpp, "How it runs"), so the FIFO never fills.
  void on_igmp_word(const ap_uint<64> &w)
  {
#pragma HLS INLINE
    const ap_uint<192> b = (ap_uint<192>(w) << 128) | (ap_uint<192>(h_[5]) << 64) | ap_uint<192>(h_[4]);
    const ap_uint<64> part = b >> (8 * igmp_at_);   // the 8 bytes of IGMP
    if (part.range(7, 0) == kIgmpQuery)
    {
      IgmpQuery q;
      q.max_resp = part.range(15, 8);
      q.group = (part.range(39, 32), part.range(47, 40), part.range(55, 48), part.range(63, 56));
      igmp.send(q, this);
      ++queries;
    }
    else
      ++dropped;
  }

  // From Igmp: the group and port to accept from now on. Group 0 means nothing: a
  // separate flag, so that a frame to 0.0.0.0, port 0 is not taken for ours.
  void on_listen(const Listen &m)
  {
#pragma HLS INLINE
    group = m.group;
    port = m.port;
    listening_ = m.group != 0;
  }

public:
  static constexpr kfpga::ActorId kId = kFeedRx;
  kfpga::ActorRef igmp;                 // Igmp, for the queries
  ap_uint<32> group = 0;
  ap_uint<16> port = 0;
  uint32_t accepted = 0, dropped = 0, queries = 0;
  uint32_t unhandled = 0;         // messages on `listen` not for FeedRx, or with no handler

  KFPGA_HANDLERS(KFPGA_ON(Listen, on_listen))

  // One clock: the only entry point, called by rx_task() every cycle.
  //
  // A Listen from Igmp, if one is waiting, is handled first. Then, if a kept frame's
  // remaining payload already sits in prev_, send it as the last word and finish,
  // without reading. Otherwise read one word, if there is one,
  // and act on it by state: store a header word (and decide at the sixth), send one
  // payload word, or discard a word of a dropped frame.
  void on_step(WordStream &in, WordStream &out, kfpga::Fifo &listen)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=h_ type=complete
    kfpga::Envelope e;
    if (listen.read_nb(e) && (e.dst != kId || !kfpga_dispatch(e)))
      ++unhandled;
    Word w;
    const ap_uint<4> head = ap_uint<4>(8) - o_;    // payload bytes in prev_
    const bool from_prev = left_ <= head || ended_;
    if (state_ == kBody && from_prev)
    {
      // What is left is all in prev_.
      Word r;
      r.data = prev_ >> (8 * o_);
      const ap_uint<4> n = left_ <= head ? ap_uint<4>(left_) : head;
      r.keep = (ap_uint<9>(1) << n) - 1;
      r.last = true;
      out.write(r);
      finish();
      return;
    }
    if (!in.read_nb(w))
      return;
    ended_ = w.last;
    switch (state_)
    {
    case kHeader:
      h_[n_] = w.data;
      n_ = n_ + 1;
      if (n_ == kHeaderWords)
        decide();
      else if (w.last)
      {
        n_ = 0;   // too short to be ours
        ++dropped;
      }
      break;
    case kBody:
    {
      Word r;
      r.data = (prev_ >> (8 * o_)) | (w.data << (8 * head));
      const bool last = left_ <= 8;
      r.keep = last ? ap_uint<8>((ap_uint<9>(1) << left_.range(3, 0)) - 1) : ap_uint<8>(0xff);
      r.last = last;
      out.write(r);
      left_ = last ? ap_uint<16>(0) : ap_uint<16>(left_ - 8);
      prev_ = w.data;
      if (last)
        finish();
      break;
    }
    case kIgmp:
      on_igmp_word(w.data);
      state_ = w.last ? kHeader : kDrain;
      break;
    default:   // kDrain
      if (w.last)
        state_ = kHeader;
      break;
    }
  }
};

} // namespace mdp3fpga
