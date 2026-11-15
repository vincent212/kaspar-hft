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
 *   FeedRx takes and sends words, not messages, so it has no handlers and is in
 *   no kfpga::Group. (Held in kfpga::instance<T>() instead, HLS took the object
 *   for a constant and removed its output: HLS.md section 2.)
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
 *   group, port  channel A's multicast address, set by rx_task every clock. They
 *                come from the build for now (MDP3_GROUP, MDP3_PORT).
 *   accepted,    frames kept and dropped.
 *   dropped
 *
 * Not checked: IP and UDP checksums (the MAC drops damaged frames), and IP
 * fragments (CME does not fragment this feed).
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

constexpr int kHeaderWords = 6;

class FeedRx : public kfpga::Actor<FeedRx>
{
  enum State : uint8_t { kHeader, kBody, kDrain };

  State state_ = kHeader;
  ap_uint<64> h_[kHeaderWords] = {};   // the first six words of the frame, every header
  ap_uint<3> n_ = 0;          // header words held so far
  ap_uint<64> prev_ = 0;      // the input word the next payload bytes start in
  ap_uint<3> o_ = 0;          // where in prev_ they start: 2, or 6 with a VLAN tag
  ap_uint<16> left_ = 0;      // payload bytes still to send, from the UDP length
  bool ended_ = false;        // the frame's last word has been read

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
  // least its own 8 bytes. A VLAN tag (EtherType 0x8100 at byte 12) moves every later
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
    const bool ours = type == 0x0800 && version == 0x45 && proto == 17 && dst == group && dport == port && ulen >= 8;
    n_ = 0;
    if (ours)
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

public:
  static constexpr kfpga::ActorId kId = kFeedRx;
  ap_uint<32> group = 0;
  ap_uint<16> port = 0;
  uint32_t accepted = 0, dropped = 0;

  // One clock: the only entry point, called by rx_task() every cycle.
  //
  // First, if a kept frame's remaining payload already sits in prev_, send it as the
  // last word and finish, without reading. Otherwise read one word, if there is one,
  // and act on it by state: store a header word (and decide at the sixth), send one
  // payload word, or discard a word of a dropped frame.
  void on_step(WordStream &in, WordStream &out)
  {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=h_ type=complete
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
    default:   // kDrain
      if (w.last)
        state_ = kHeader;
      break;
    }
  }
};

} // namespace mdp3fpga
