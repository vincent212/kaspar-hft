#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * FeedRx, the first actor on the wire: Ethernet frames in from the MAC, 8 bytes a
 * word, and the UDP payloads of one multicast group and port out, realigned to
 * byte 0 of a word, for SbeDecoder. Everything else is dropped.
 *
 *   frame = Ethernet (14, 18 with an 802.1Q tag) | IPv4 (20) | UDP (8) | payload
 *
 * The first six words hold every header, so the decision takes one step. The
 * payload starts 2 bytes (6 with a tag) into word 5, so each word out is the tail
 * of one word in and the head of the next, one word a step. The UDP length gives
 * the payload's end, so any padding is dropped. IP options are not expected on
 * this feed: such a frame is dropped.
 *
 * It sends no messages, both sides being raw streams, so it is in no Group: the
 * process holds the object itself, as a static (HLS.md section 2). The group and
 * port come from the build for now, set by the process each step; the MAC is
 * assumed to have removed the FCS.
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
  ap_uint<64> h_[kHeaderWords] = {};
  ap_uint<3> n_ = 0;          // header words held
  ap_uint<64> prev_ = 0;      // the word the next bytes start in
  ap_uint<3> o_ = 0;          // the offset of the payload in a word
  ap_uint<16> left_ = 0;      // payload bytes left
  bool ended_ = false;        // the frame's last word has been read

  // Big-endian field of up to 4 bytes at byte `at` of the header.
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

  void finish()
  {
#pragma HLS INLINE
    state_ = ended_ ? kHeader : kDrain;
    n_ = 0;
  }

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
