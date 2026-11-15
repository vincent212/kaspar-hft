#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * Igmp: the actor that subscribes the card's port to the multicast group.
 *
 * Why it is on the card
 *
 *   A switch with IGMP snooping forwards a multicast group only to the ports from
 *   which a host has asked for it, with an IGMP membership report. On an ordinary NIC
 *   the kernel sends that report when a socket joins the group. The card's port is
 *   not the kernel's: the FPGA owns it, so nothing would ever ask for the group, and
 *   the switch would send the card nothing. A report sent through another NIC does
 *   not help, since the switch would then forward the group to that NIC's port. The
 *   report has to leave through the card's own port, so the card sends it.
 *
 * What it does (IGMPv2, RFC 2236)
 *
 *   JoinGroup    from the CPU: the group and UDP port to receive, and the card's own
 *                MAC address, IP address and VLAN. Igmp stores them, tells FeedRx to
 *                accept that group and port (Listen), sends a membership report for
 *                the group, and once it is out sends the CPU an IgmpAck. The report is sent twice more,
 *                `repeat_clocks` apart (10 s by default, RFC 2236's Unsolicited
 *                Report Interval), in case the first is lost: until the switch has
 *                one, no data arrives.
 *   LeaveGroup   from the CPU: tells FeedRx to accept nothing, sends a leave to
 *                224.0.0.2 (all routers), and then an IgmpAck.
 *   IgmpQuery    from FeedRx, which passes up every IGMP membership query it sees. The
 *                switch, or the router it stands for, queries every 125 s by default
 *                and forgets a port that does not answer within about 260 s. A general
 *                query (group 0), or one for our group, is answered with a report.
 *                RFC 2236 lets a host wait any time up to the query's maximum response
 *                time before it answers, so that hosts on a shared segment can hear
 *                each other and keep quiet; the card is alone on its switch port, and
 *                answers at once, which the RFC allows.
 *
 *   One group at a time: a JoinGroup while joined is refused (ok = 0); leave first.
 *   Several channels need a table of groups (feed_rx.hpp, "One channel").
 *
 * How it runs
 *
 *   A message actor that also writes frames to the MAC. Its task (igmp_task in
 *   hls/feed_top.cpp) does one of two things each clock:
 *
 *     - if busy(), on_step(tx), through proc.step_stream: build the frame a handler
 *       asked for, write its next word, send the ack once a join's or leave's frame
 *       is out, or start a repeat;
 *     - otherwise proc.step(ports) handles one message, as for any message actor.
 *
 *   A frame takes ten clocks: one to build, eight words of 60 bytes, one for the ack.
 *   Messages wait in their FIFOs meanwhile. Every handler, and every clock of
 *   on_step, sends at most one message, so that each output FIFO has one write per
 *   clock and the task runs at II = 1.
 *
 *   Ports: 0, from and to the CPU (JoinGroup, LeaveGroup; IgmpAck). 1, from and to
 *   FeedRx (IgmpQuery; Listen). The frames go out on `tx`, a word stream to the MAC's
 *   transmit side, in the same format FeedRx reads: 8 bytes a word, byte 0 low, `keep`
 *   marking the valid bytes of the last word, which the MAC follows with the FCS.
 *
 * The frame (build())
 *
 *   Ethernet  destination 01:00:5e and the low 23 bits of the IP destination, the
 *             multicast MAC address; source the card's MAC; with the card's VLAN tag
 *             (802.1Q, priority 0) if it has one; EtherType 0x0800.
 *   IPv4      24 bytes: version 4, header length 6 words, for the Router Alert option
 *             (RFC 2113) that RFC 2236 requires; TOS 0xc0 (internetwork control, as
 *             Linux sends); total length 32; DF; TTL 1, so it never leaves the
 *             segment; protocol 2; header checksum; source the card's IP;
 *             destination the group (a report) or 224.0.0.2 (a leave).
 *   IGMP      8 bytes: type 0x16 (v2 report) or 0x17 (leave), max response time 0,
 *             checksum, group.
 *   padding   zeros to 60 bytes, Ethernet's minimum without the FCS.
 *
 *   The frame is built in a clock of its own, from registers, since the checksums
 *   are adds that would not fit in the clock that decodes the message.
 */

#include <ap_int.h>
#include <hls_stream.h>

#include "kfpga/actor.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

constexpr kfpga::ActorId kIgmp = 26;

// From the CPU (FpgaFeed, at start): receive this group and port. The card's own addresses come with it,
// since the reports carry them.
struct JoinGroup : kfpga::Message<340>
{
  uint32_t group = 0;          // IPv4, host order: 224.0.31.68 is 0xE0001F44
  uint32_t port = 0;           // UDP destination port
  uint64_t mac = 0;            // the card's MAC address, low 48 bits, first byte highest
  uint32_t ip = 0;             // the card's IPv4 address
  uint32_t vlan = 0;           // the VLAN id, 1 to 4094; 0 for none
  KFPGA_FIELDS(group, port, mac, ip, vlan)
};

// From the CPU: stop receiving the group.
struct LeaveGroup : kfpga::Message<341>
{
  uint32_t unused = 0;
  KFPGA_FIELDS(unused)
};

// To the CPU, for each JoinGroup and LeaveGroup.
struct IgmpAck : kfpga::Message<342>
{
  uint32_t group = 0;          // the group joined or left
  uint32_t ok = 0;             // 0: a join while joined, or a leave while not
  KFPGA_FIELDS(group, ok)
};

// From FeedRx: a membership query seen on the wire.
struct IgmpQuery : kfpga::Message<343>
{
  uint32_t group = 0;          // 0 for a general query
  uint32_t max_resp = 0;       // the maximum response time, tenths of a second
  KFPGA_FIELDS(group, max_resp)
};

// To FeedRx: the group and port to accept; 0 and 0 to accept nothing.
struct Listen : kfpga::Message<344>
{
  uint32_t group = 0;
  uint32_t port = 0;
  KFPGA_FIELDS(group, port)
};

constexpr int kIgmpFrameWords = 8;               // 60 bytes, the last word half full
constexpr uint32_t kAllRouters = 0xE0000002;     // 224.0.0.2, where leaves go
constexpr uint8_t kIgmpQuery = 0x11, kIgmpReport = 0x16, kIgmpLeave = 0x17;

class Igmp : public kfpga::Actor<Igmp>
{
  // The interface, from JoinGroup.
  ap_uint<32> group_ = 0;
  ap_uint<48> mac_ = 0;
  ap_uint<32> ip_ = 0;
  ap_uint<12> vlan_ = 0;
  bool joined_ = false;

  // The frame to build: its type and IP destination, and whether one is waiting.
  ap_uint<8> type_ = 0;
  ap_uint<32> dst_ = 0;
  bool build_ = false;

  // The frame being sent: frame_ holds the words not yet written, the next one low.
  ap_uint<64 * kIgmpFrameWords> frame_ = 0;
  ap_uint<4> left_ = 0;                // words still to write; 0 when idle

  // The ack of a join or leave, sent once its frame is out.
  bool ack_ = false;
  ap_uint<32> ack_group_ = 0;

  // The repeats of the report after a join.
  ap_uint<2> repeats_ = 0;
  ap_uint<32> wait_ = 0;               // clocks until the next

  // Byte `at` of the frame, `n` bytes of `v`, most significant first.
  template <int N>
  static void put(ap_uint<64 * kIgmpFrameWords> &f, int at, ap_uint<8 * N> v)
  {
#pragma HLS INLINE
    for (int i = 0; i < N; ++i)
    {
#pragma HLS UNROLL
      f.range(8 * (at + i) + 7, 8 * (at + i)) = v.range(8 * (N - 1 - i) + 7, 8 * (N - 1 - i));
    }
  }

  // A sum of 16-bit words, folded to 16 bits and inverted: the IP and IGMP checksum.
  static ap_uint<16> fold(ap_uint<32> s)
  {
#pragma HLS INLINE
    s = s.range(15, 0) + s.range(31, 16);
    s = s.range(15, 0) + s.range(31, 16);
    return ~ap_uint<16>(s.range(15, 0));
  }

  // The frame for an IGMP message of `type` about `group`, sent to `dst`. O is where
  // the IP header starts: 14, or 18 after a VLAN tag.
  template <int O>
  ap_uint<64 * kIgmpFrameWords> build(ap_uint<8> type, ap_uint<32> group, ap_uint<32> dst) const
  {
#pragma HLS INLINE
    ap_uint<64 * kIgmpFrameWords> f = 0;
    const ap_uint<48> dmac = (ap_uint<48>(0x01005e) << 24) | ap_uint<48>(dst.range(22, 0));
    put<6>(f, 0, dmac);
    put<6>(f, 6, mac_);
    if (O == 18)
    {
      put<2>(f, 12, 0x8100);
      put<2>(f, 14, ap_uint<16>(vlan_));
    }
    put<2>(f, O - 2, 0x0800);

    // IPv4, with the Router Alert option.
    const ap_uint<32> ip_sum = 0x46c0 + 32 + 0x0000 + 0x4000 + 0x0102 + ip_.range(31, 16) + ip_.range(15, 0) +
                               dst.range(31, 16) + dst.range(15, 0) + 0x9404 + 0x0000;
    put<2>(f, O + 0, 0x46c0);            // version 4, 6 words; TOS 0xc0
    put<2>(f, O + 2, 32);                // total length
    put<2>(f, O + 4, 0);                 // identification
    put<2>(f, O + 6, 0x4000);            // don't fragment
    put<2>(f, O + 8, 0x0102);            // TTL 1, protocol 2 (IGMP)
    put<2>(f, O + 10, fold(ip_sum));
    put<4>(f, O + 12, ip_);
    put<4>(f, O + 16, dst);
    put<4>(f, O + 20, 0x94040000);       // Router Alert

    // IGMP.
    const ap_uint<32> igmp_sum = (ap_uint<32>(type) << 8) + group.range(31, 16) + group.range(15, 0);
    put<1>(f, O + 24, type);
    put<1>(f, O + 25, 0);                // max response time: 0 in what a host sends
    put<2>(f, O + 26, fold(igmp_sum));
    put<4>(f, O + 28, group);
    return f;                            // the rest, to 60 bytes, is zero
  }

  // A frame to send: on_step builds it on the next clock and writes it on the eight
  // after.
  void start(ap_uint<8> type, ap_uint<32> dst)
  {
#pragma HLS INLINE
    type_ = type;
    dst_ = dst;
    build_ = true;
  }

  // A refused join or leave: answered at once, with nothing sent.
  void refuse(ap_uint<32> group)
  {
#pragma HLS INLINE
    IgmpAck a;
    a.group = group;
    a.ok = 0;
    reply(a);
  }

  // Join: store the interface, tell FeedRx the group and port, and send a report.
  // The ack follows the report.
  void on_join(const JoinGroup &m)
  {
#pragma HLS INLINE
    if (joined_)
    {
      refuse(m.group);
      return;
    }
    group_ = m.group;
    mac_ = m.mac;
    ip_ = m.ip;
    vlan_ = m.vlan;
    joined_ = true;
    Listen l;
    l.group = m.group;
    l.port = m.port;
    feed_rx.send(l, this);
    start(kIgmpReport, m.group);
    ack_ = true;
    ack_group_ = m.group;
    repeats_ = 2;
    wait_ = repeat_clocks;
  }

  // Leave: FeedRx accepts nothing from now on, and a leave goes to all routers. The
  // ack follows the leave.
  void on_leave(const LeaveGroup &)
  {
#pragma HLS INLINE
    if (!joined_)
    {
      refuse(group_);
      return;
    }
    Listen l;   // 0 and 0: accept nothing
    feed_rx.send(l, this);
    start(kIgmpLeave, kAllRouters);
    ack_ = true;
    ack_group_ = group_;
    joined_ = false;
    repeats_ = 0;
  }

  // A query for all groups or for ours: report.
  void on_query(const IgmpQuery &m)
  {
#pragma HLS INLINE
    ++queries;
    if (joined_ && (m.group == 0 || m.group == group_))
      start(kIgmpReport, group_);
  }

  bool repeat_due() const { return joined_ && repeats_ != 0 && wait_ == 0; }

public:
  static constexpr kfpga::ActorId kId = kIgmp;
  kfpga::ActorRef cpu;                     // the CPU actor that joins, on port 0
  kfpga::ActorRef feed_rx;                 // FeedRx, on port 1
  ap_uint<32> repeat_clocks = 2500000000u;  // 10 s at 250 MHz, between the join's reports
  uint32_t reports = 0, leaves = 0, queries = 0;

  // Something to do besides messages: a frame to build or write, an ack to send, or a
  // repeat due. The task then calls on_step instead of handling a message.
  bool busy() const { return build_ || left_ != 0 || ack_ || repeat_due(); }

  // A clock with nothing to do but messages: count down to the next repeat.
  void on_idle()
  {
#pragma HLS INLINE
    if (joined_ && repeats_ != 0 && wait_ != 0)
      wait_ = wait_ - 1;
  }

  // One clock of the work busy() says is waiting, in this order: build the frame; write
  // its next word; send the ack of the join or leave whose frame is now out; start a
  // repeat. Each clock sends at most one message.
  void on_step(WordStream &tx)
  {
#pragma HLS INLINE
    if (build_)
    {
      // From registers only, so the checksums' adds have the clock to themselves.
      frame_ = vlan_ != 0 ? build<18>(type_, group_, dst_) : build<14>(type_, group_, dst_);
      left_ = kIgmpFrameWords;
      build_ = false;
      if (type_ == kIgmpLeave)
        ++leaves;
      else
        ++reports;
      return;
    }
    if (left_ != 0)
    {
      Word w;
      w.data = frame_.range(63, 0);
      w.last = left_ == 1;
      w.keep = left_ == 1 ? ap_uint<8>(0x0f) : ap_uint<8>(0xff);   // 60 = 7 * 8 + 4
      tx.write(w);
      frame_ = frame_ >> 64;
      left_ = left_ - 1;
      return;
    }
    if (ack_)
    {
      IgmpAck a;
      a.group = ack_group_;
      a.ok = 1;
      cpu.send(a, this);
      ack_ = false;
      return;
    }
    start(kIgmpReport, group_);   // a repeat
    repeats_ = repeats_ - 1;
    wait_ = repeat_clocks;
  }

  KFPGA_HANDLERS(KFPGA_ON(JoinGroup, on_join) KFPGA_ON(LeaveGroup, on_leave) KFPGA_ON(IgmpQuery, on_query))
};

} // namespace mdp3fpga
