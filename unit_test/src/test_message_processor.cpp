/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit tests for mdp3::MessageProcessor's packet sequencing.
 *
 * Focus: the in-order "fast path" (decode straight from the incoming buffer,
 * skipping the reorder map) vs the buffered slow path (gaps), and dedup. We drive
 * processq_handler directly with minimal MDP3 packets and observe the decode
 * order through a recording feed_handler_if.
 *
 * A minimal packet is just the 12-byte MDP3 binary header (MsgSeqNum + Sending-
 * Time) with zero SBE messages: DataDecoder::mbo_data decodes it as an empty
 * packet and calls cb->EndOfPacket(MsgSeqNum, ...), which we record. The packet's
 * MsgSeqNum is set equal to the message_buffer seqnum so the recorded order maps
 * 1:1 to the seqnums we feed.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"
#include "mdp3/act/MessageProcessor.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"

using namespace unit_test;

namespace {

// A feed_handler_if that records which packets were decoded (by their MsgSeqNum,
// captured at EndOfPacket) and in what order. Every other callback is a no-op.
struct RecordingHandler : public mdp3::feed_handler_if
{
  std::vector<uint32_t> decoded; // MsgSeqNums, in decode order

  // Ingress mailbox depth as it stood when each packet was decoded. The real
  // handler_if holds this in a member that set_ingress_qlen() overwrites and
  // the l3 emitters later read, so a packet that never calls the setter
  // silently inherits whatever the previous packet left there. Recording it
  // per packet is what makes that inheritance visible to a test -- a missing
  // call shows up as a plausible number, never as an absent one.
  std::vector<uint32_t> qlen_at_decode;
  uint32_t ingress_qlen_ = 0;

  void set_ingress_qlen(uint32_t qlen) noexcept override { ingress_qlen_ = qlen; }

  void EndOfPacket(uint32_t msgSeqNum, uint64_t) noexcept override
  {
    decoded.push_back(msgSeqNum);
    qlen_at_decode.push_back(ingress_qlen_);
  }

  // ---- everything else: no-ops (signatures copied from mdp3/mbo_if.hpp) ----
  void disable_mbo(bool) noexcept override {}
  void set_max_mbp_level(uint32_t) noexcept override {}
  void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
      int64_t, int8_t, char, int32_t, int32_t, uint8_t, bool, bool) noexcept override {}
  void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
      int64_t, int8_t, char, int32_t, uint64_t, uint8_t, uint64_t, bool, bool,
      bool) noexcept override {}
  void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
      int32_t, int64_t, int8_t, char, uint8_t, int32_t, int32_t, bool, bool) noexcept override {}
  void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
      int32_t, uint64_t, bool, bool) noexcept override {}
  void MDIncrementalRefreshSessionStatistics(uint32_t, uint64_t, uint64_t, uint32_t,
      uint8_t, int64_t, int64_t, uint8_t, char) noexcept override {}
  void MDIncrementalRefreshDailyStatistics(uint32_t, uint64_t, uint64_t, uint32_t,
      int64_t, int8_t, int32_t, char, bool, bool, uint8_t, uint16_t) noexcept override {}
  void MDInstrumentDefinitionFuture(uint32_t, uint64_t, char*, char*, char*, int64_t,
      int8_t, int64_t, int8_t, int64_t, int8_t, int32_t, char, uint8_t, uint64_t,
      uint64_t, char*, uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t, char,
      int64_t, int8_t, uint8_t, char*, uint8_t, uint16_t, uint16_t, uint16_t, int32_t,
      int32_t, uint16_t, char*, int64_t, uint8_t) noexcept override {}
  void MDInstrumentDefinitionOption(uint32_t, uint64_t, char*, char*, char*, int64_t,
      int8_t, int64_t, int8_t, int32_t, char, uint64_t, uint64_t, char*, uint8_t,
      uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t, int64_t, uint8_t, uint8_t,
      int64_t, uint8_t, char, int8_t, uint8_t, uint32_t*, std::string*, int64_t,
      int8_t, uint8_t, char*, uint8_t, uint16_t, int32_t, int32_t, uint16_t, char*,
      int64_t, uint8_t) noexcept override {}
  void MDInstrumentDefinitionSpread(uint32_t, uint64_t, char*, char*, char*, int64_t,
      int8_t, int64_t, int8_t, int32_t, char, uint64_t, uint64_t, char*, uint8_t,
      uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t, char, int8_t, uint8_t,
      int32_t*, int8_t*, int64_t*, int8_t*, int8_t*, int32_t*, uint8_t*, int64_t,
      int8_t, uint8_t, char*, uint8_t, uint16_t, int32_t, int32_t, uint16_t,
      char*) noexcept override {}
  void ChannelReset(uint32_t, uint64_t, uint64_t, const char*) noexcept override {}
  void SnapshotFullRefreshOrderBook_NR(uint32_t, uint32_t, uint32_t, uint64_t,
      uint64_t, uint32_t, uint32_t, int32_t, int32_t, int64_t, int64_t, char,
      uint64_t, uint64_t) noexcept override {}
  void SnapshotFullRefreshOrderBook(uint64_t, uint64_t, int32_t, int32_t, int64_t,
      int64_t, char, uint64_t, uint64_t) noexcept override {}
  void MDIncrementalRefreshLimitsBanding(uint32_t, uint64_t, uint64_t, int32_t,
      int64_t, int8_t, int64_t, int8_t, int64_t, int8_t, const char*) noexcept override {}
  void SecurityStatus(uint32_t, uint64_t, uint64_t, int32_t, uint8_t, uint8_t,
      uint8_t) noexcept override {}
  void MDIncrementalRefreshVolume(uint32_t, uint64_t, uint64_t, int32_t, int32_t,
      char, uint8_t) noexcept override {}
  void DataReceoveryRestart() noexcept override {}
  void Gap() noexcept override {}
  void BurstEnd(uint32_t) noexcept override {}
  void PrintStats() noexcept override {}
};

// Build a ProcessQ carrying a minimal (header-only) MDP3 packet whose internal
// MsgSeqNum == seqnum, so EndOfPacket records `seqnum`. `buf.seqnum` is the key
// MessageProcessor sequences on.
static mcast_recv::msg::ProcessQ<uint32_t> *make_pkt(uint32_t seqnum, uint32_t qlen = 0)
{
  auto *pq = new mcast_recv::msg::ProcessQ<uint32_t>();
  auto &b = pq->buf;
  std::memset(&b, 0, sizeof(b));
  b.seqnum = seqnum;
  b.qlen = qlen;
  b.len = 12; // MsgSeqNum(4) + SendingTime(8), no SBE messages
  b.recv_ts = 1;
  std::memcpy(&b.message[0], &seqnum, sizeof(seqnum)); // packet MsgSeqNum -> EndOfPacket
  // SendingTime at [4..11] left 0 by the memset above.
  return pq;
}

class MessageProcessorTest : public ::testing::Test
{
protected:
  MockActor recovery{"MockRecovery"};
  RecordingHandler cb;
  // dorecovery=true, data-recovery-on-start=false, mbo enabled, 10 MBP levels.
  // We never deliver Start, so in_instr_recovery stays false and the fast path
  // is active from the first packet.
  mdp3::MessageProcessor mp{"test", &recovery, &cb, true, false, false, 10, false};

  void feed(uint32_t seqnum, uint32_t qlen = 0)
  {
    auto *pq = make_pkt(seqnum, qlen);
    TestHelper::invoke_handler(&mp, pq, nullptr);
    delete pq; // handler copies buf into msg_q (slow path) or decodes it inline
  }
};

// In-order stream: decoded immediately, in order, and never buffered.
TEST_F(MessageProcessorTest, InOrderDecodesInOrderAndSkipsReorderMap)
{
  feed(1);
  feed(2);
  feed(3);
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2, 3}));
  EXPECT_EQ(mp.reorder_q_size(), 0u); // fast path: nothing buffered
  EXPECT_EQ(recovery.message_count(), 0u); // no recovery on a clean stream
}

// Duplicate / already-seen seqnums are dropped, not re-decoded.
TEST_F(MessageProcessorTest, DuplicateAndOldSeqnumsDropped)
{
  feed(1);
  feed(2);
  feed(2); // duplicate (== qseq_num)
  feed(1); // old (< qseq_num)
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2}));
  EXPECT_EQ(mp.reorder_q_size(), 0u);
}

// A gap buffers the out-of-order packet (slow path); filling it drains in order.
TEST_F(MessageProcessorTest, GapBuffersThenDrainsInOrder)
{
  feed(1);
  feed(2);
  feed(4); // expected 3 -> gap: buffered, NOT decoded
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2}));
  EXPECT_EQ(mp.reorder_q_size(), 1u); // 4 sits in the reorder map

  feed(3); // fills the gap -> drains 3 then 4 in order
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2, 3, 4}));
  EXPECT_EQ(mp.reorder_q_size(), 0u);
}

// Cold start: qseq_num == 0 accepts the first packet at any seqnum.
TEST_F(MessageProcessorTest, ColdStartAcceptsFirstSeqnum)
{
  feed(100);
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{100}));
  EXPECT_EQ(mp.reorder_q_size(), 0u);
}

// The fast path must stamp the ingress qlen, exactly as processq() does.
//
// This is the regression test for a real defect in the first cut of the fast
// path: set_ingress_qlen() was called only in processq()'s drain loop, and the
// fast path returns before processq() is ever reached. In steady state -- which
// is precisely when the fast path is taken -- the gauge therefore froze at
// whatever the last slow-path packet had left in it.
//
// The failure mode is what makes it worth a test. A frozen gauge does not read
// as missing: it reads as a small, entirely plausible queue depth, on the large
// majority of book messages. Feeding a DIFFERENT qlen per packet is the point;
// a constant would pass against the bug.
TEST_F(MessageProcessorTest, FastPathStampsIngressQlenPerPacket)
{
  feed(1, 7);
  feed(2, 0);   // 0 is a legitimate depth, and the value most likely to be
                // confused with "never set" -- so it has to round-trip too.
  feed(3, 42);
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2, 3}));
  EXPECT_EQ(mp.reorder_q_size(), 0u); // confirm these really took the fast path
  EXPECT_EQ(cb.qlen_at_decode, (std::vector<uint32_t>{7, 0, 42}));
}

// The slow path stamps the qlen of the packet being DECODED, not of the packet
// that happened to trigger the drain.
//
// 4 arrives early and is buffered carrying qlen=99. It is not decoded until 3
// closes the gap, and 3 carries qlen=5. If the drain loop read the qlen off the
// arriving packet -- the mistake the `ts` argument next to it actually makes --
// packet 4 would be recorded at 5 rather than 99.
TEST_F(MessageProcessorTest, SlowPathStampsTheBufferedPacketsOwnQlen)
{
  feed(1, 1);
  feed(2, 2);
  feed(4, 99); // gap: buffered with its own qlen
  EXPECT_EQ(cb.qlen_at_decode, (std::vector<uint32_t>{1, 2}));
  ASSERT_EQ(mp.reorder_q_size(), 1u);

  feed(3, 5); // closes the gap; drains 3 (qlen 5) then 4 (qlen 99)
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2, 3, 4}));
  EXPECT_EQ(cb.qlen_at_decode, (std::vector<uint32_t>{1, 2, 5, 99}));
}

} // namespace
