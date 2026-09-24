/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * MsgBuf::processq_handler -- where the ingress depth moves from the Message
 * onto the packet buffer. The first hop of the qlen chain, and the last one
 * that had no test.
 *
 *     Actor::add_message_to_queue   stamps Message::qlen  (test_msg_qlen.cpp)
 *  -> MsgBuf::processq_handler      copies it onto message_buffer.qlen   <-- HERE
 *  -> MessageProcessor::processq    -> DecodePacket.qlen  (test_processq_qlen.cpp)
 *  -> DecodeReq / DecodeSink        -> l3.ingress_qlen    (test_decode_qlen.cpp)
 *
 * The copy is one line and it is not optional. MessageProcessor's reorder map
 * stores packets BY VALUE (msg_q.try_emplace(seqnum, m->buf)), so anything
 * still living on the Message is dropped at that copy and never reaches the
 * decoder, the book, or the probe. Delete the line and every l3 record in the
 * system reports depth 0 -- an empty mailbox, which is a legal reading, so the
 * feed looks healthy and uncontended all the way to the regression.
 *
 * Why this is the depth worth measuring: both feeds A and B send() into the
 * same MsgBuf, so this mailbox is the A/B merge point -- the one place in the
 * ingress path where two feed threads contend.
 *
 * No actor threads are started. The MsgBuf is constructed but never run, so
 * nothing drains its mailbox and the backlog is deterministic: the Nth send
 * sees exactly N-1 messages ahead of it. Handlers are then invoked by hand on
 * those same messages, which is the real sequence (enqueue stamps, handler
 * copies) minus the scheduling. The queued messages leak, deliberately --
 * draining needs the run loop, and determinism is worth more here. This is the
 * same trade test_msg_qlen.cpp already makes.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "unit_test/TestHelper.hpp"
#include "actors/Actor.hpp"
#include "mcast_recv/act/MsgBuf.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"

using namespace unit_test;

namespace {

using PQ = mcast_recv::msg::ProcessQ<uint32_t>;

// MsgBuf hands the packet on with fast_send, which is not virtual, so a
// send()-overriding MockActor cannot see it. Record from a real handler.
struct RecordingProcessor : public actors::Actor
{
  struct seen { uint32_t seqnum; uint32_t buf_qlen; uint32_t msg_qlen; };
  std::vector<seen> packets;

  RecordingProcessor() { MESSAGE_HANDLER(PQ, on_pq); }
  const char *get_name() const override { return "RecordingProcessor"; }

  void on_pq(const PQ *m) noexcept
  {
    packets.push_back({m->buf.seqnum, m->buf.qlen, m->qlen});
  }

  std::vector<uint32_t> buf_qlens() const
  {
    std::vector<uint32_t> v;
    for (const auto &p : packets) v.push_back(p.buf_qlen);
    return v;
  }
};

class MsgBufQlenTest : public ::testing::Test
{
protected:
  RecordingProcessor mp;
  mcast_recv::MsgBuf<uint32_t> buf{"test", /*spin=*/0, /*chan=*/'a', &mp};

  // Enqueue for real (this is what stamps Message::qlen), keep the pointer,
  // then run the handler on it. Returns the depth the framework stamped.
  uint32_t enqueue(uint32_t seqnum, uint32_t preset_buf_qlen = 0)
  {
    auto *pq = new PQ();
    std::memset(&pq->buf, 0, sizeof(pq->buf));
    pq->buf.seqnum = seqnum;
    pq->buf.qlen   = preset_buf_qlen;
    buf.send(pq, nullptr); // add_message_to_queue stamps pq->qlen here
    queued_.push_back(pq);
    return pq->qlen;
  }

  void run_handler(size_t i)
  {
    TestHelper::invoke_handler(&buf, queued_[i], nullptr);
  }

  std::vector<PQ *> queued_;
};

// The contract, and the reason the depth is worth carrying at all: packet i
// queued behind exactly i others, and that number is what arrives downstream
// on the buffer.
TEST_F(MsgBufQlenTest, DepthMovesFromTheMessageOntoTheBuffer)
{
  for (uint32_t i = 0; i < 4; ++i)
    EXPECT_EQ(enqueue(100 + i), i) << "framework stamp for packet " << i;

  for (size_t i = 0; i < 4; ++i)
    run_handler(i);

  ASSERT_EQ(mp.packets.size(), 4u);
  EXPECT_EQ(mp.buf_qlens(), (std::vector<uint32_t>{0, 1, 2, 3}));
}

// The buffer field must be WRITTEN, not merely left alone. Pre-loading it with
// a value that cannot be a real depth here separates "copied correctly" from
// "never touched": with the copy line gone this reads back 12345.
//
// It is also the honest version of the zero test. A deleted copy normally
// leaves 0, and 0 is a legal depth, so the failure would be invisible.
TEST_F(MsgBufQlenTest, BufferDepthIsOverwrittenNotInherited)
{
  enqueue(1, /*preset_buf_qlen=*/12345);
  run_handler(0);
  ASSERT_EQ(mp.packets.size(), 1u);
  EXPECT_EQ(mp.packets[0].buf_qlen, 0u); // first message: mailbox was empty
}

// Zero is a legitimate reading (empty mailbox) and has to survive as zero.
// Paired with the test above, which is what makes a zero here meaningful.
TEST_F(MsgBufQlenTest, EmptyMailboxIsRecordedAsZero)
{
  EXPECT_EQ(enqueue(7), 0u);
  run_handler(0);
  ASSERT_EQ(mp.packets.size(), 1u);
  EXPECT_EQ(mp.packets[0].buf_qlen, 0u);
}

// MsgBuf forwards the same object, so the Message field is still there. The
// buffer copy must agree with it -- if these ever diverge, the copy is reading
// something other than the depth this packet queued behind.
TEST_F(MsgBufQlenTest, BufferDepthAgreesWithTheMessageStamp)
{
  for (uint32_t i = 0; i < 3; ++i) enqueue(200 + i);
  for (size_t i = 0; i < 3; ++i) run_handler(i);

  ASSERT_EQ(mp.packets.size(), 3u);
  for (const auto &p : mp.packets)
    EXPECT_EQ(p.buf_qlen, p.msg_qlen) << "seqnum " << p.seqnum;
}

// The packet itself is forwarded intact -- the depth is added to it, not
// swapped for it.
TEST_F(MsgBufQlenTest, PacketIdentitySurvivesTheHop)
{
  enqueue(4242);
  run_handler(0);
  ASSERT_EQ(mp.packets.size(), 1u);
  EXPECT_EQ(mp.packets[0].seqnum, 4242u);
}

} // namespace
