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

  void EndOfPacket(uint32_t msgSeqNum, uint64_t) noexcept override
  {
    decoded.push_back(msgSeqNum);
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
static mcast_recv::msg::ProcessQ<uint32_t> *make_pkt(uint32_t seqnum)
{
  auto *pq = new mcast_recv::msg::ProcessQ<uint32_t>();
  auto &b = pq->buf;
  std::memset(&b, 0, sizeof(b));
  b.seqnum = seqnum;
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

  void feed(uint32_t seqnum)
  {
    auto *pq = make_pkt(seqnum);
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

} // namespace
