/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * MessageProcessor::processq() -- packet sequencing, and the SOURCE of the
 * ingress-qlen stamp.
 *
 * test_decode_qlen.cpp pins the stamp from DecodePacket onward. This file pins
 * the link upstream of that, which nothing covered:
 *
 *     message_buffer.qlen  ->  DecodePacket.qlen
 *
 * The depth is measured once, by Actor::add_message_to_queue, at the instant
 * the packet was pushed onto the MsgBuf mailbox. It is then copied onto the
 * message_buffer because MessageProcessor's reorder map stores buffers BY
 * VALUE, so anything living on the Message is dropped at that copy. From the
 * reorder map it has to come back out attached to the right packet.
 *
 * That last step is the whole risk. processq() drains the reorder map, and the
 * packet being drained is usually NOT the packet that arrived. Reading the
 * depth off the arriving packet would still produce a number -- a small,
 * plausible queue depth, on the majority of records -- and nothing downstream
 * could tell it from a real one. It would also bias one way: a burst packet's
 * depth would be attributed to whatever quiet packet happened to close the gap.
 *
 * The `ts` argument two lines from the qlen in processq() makes exactly that
 * mistake today. It is a separate, pre-existing defect, and it is pinned below
 * as-is under a KnownDefect_ name rather than quietly left untested.
 *
 * Packets here are header-only: the 12-byte MDP3 header (MsgSeqNum +
 * SendingTime) and zero SBE messages. scan() returns count==0, so the parallel
 * branch is skipped and mbo_data decodes an empty packet and calls
 * EndOfPacket(MsgSeqNum). Setting the packet's MsgSeqNum equal to the
 * message_buffer seqnum makes the recorded decode order read back directly as
 * the seqnums fed in.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"
#include "mdp3/act/MessageProcessor.hpp"
#include "mdp3/DataDecoder.hpp"
#include "mdp3/DecodeSink.hpp"
#include "mdp3/msg/DecodePacket.hpp"
#include "mdp3/msg/DecodeResult.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "enum/e_names.hpp"

using namespace unit_test;

namespace {

// Build a ProcessQ carrying a header-only MDP3 packet whose internal MsgSeqNum
// == seqnum, so EndOfPacket records `seqnum`. buf.seqnum is the key
// MessageProcessor sequences on; buf.qlen is the measurement under test.
mcast_recv::msg::ProcessQ<uint32_t> *make_pkt(uint32_t seqnum, uint32_t qlen,
                                              uint64_t recv_ts)
{
  auto *pq = new mcast_recv::msg::ProcessQ<uint32_t>();
  auto &b = pq->buf;
  std::memset(&b, 0, sizeof(b));
  b.seqnum  = seqnum;
  b.qlen    = qlen;
  b.recv_ts = recv_ts;
  b.len     = 12; // MsgSeqNum(4) + SendingTime(8), no SBE messages
  std::memcpy(&b.message[0], &seqnum, sizeof(seqnum));
  return pq;
}

// ---------------------------------------------------------------------------
// A: the DecodePacket itself. Stands in for the DataDecoder actor and records
//    what processq() handed it, per packet.
// ---------------------------------------------------------------------------

struct RecordingDecoder : public actors::Actor
{
  struct seen
  {
    uint32_t    msg_seq; // read back out of the packet bytes
    uint32_t    qlen;
    uint64_t    ts;
    std::size_t len;
  };
  std::vector<seen> packets;

  RecordingDecoder() { MESSAGE_HANDLER(mdp3::msg::DecodePacket, on_packet); }
  const char *get_name() const override { return "RecordingDecoder"; }

  void on_packet(const mdp3::msg::DecodePacket *m) noexcept
  {
    uint32_t sn = 0;
    std::memcpy(&sn, m->data, sizeof(sn));
    packets.push_back({sn, m->qlen, m->ts, m->len});
    // processq() dereferences this reply immediately -- it is not optional.
    reply(new mdp3::msg::DecodeResult(true, false));
  }

  std::vector<uint32_t> seqs() const
  {
    std::vector<uint32_t> v;
    for (const auto &p : packets) v.push_back(p.msg_seq);
    return v;
  }
  std::vector<uint32_t> qlens() const
  {
    std::vector<uint32_t> v;
    for (const auto &p : packets) v.push_back(p.qlen);
    return v;
  }
};

class ProcessqDecodePacketTest : public ::testing::Test
{
protected:
  MockActor recovery{"MockRecovery"};
  RecordingDecoder dec;
  // Start is never delivered, so in_instr_recovery stays false and processq()
  // drains from the first packet.
  mdp3::MessageProcessor mp{"test", &recovery, &dec, /*dorecovery=*/true,
                            /*recoveryonstart=*/false};

  void feed(uint32_t seqnum, uint32_t qlen, uint64_t recv_ts = 1)
  {
    auto *pq = make_pkt(seqnum, qlen, recv_ts);
    TestHelper::invoke_handler(&mp, pq, nullptr);
    delete pq; // the handler copied buf into the reorder map
  }
};

// The in-order case: each packet reaches the decoder carrying its OWN depth.
// The values differ per packet on purpose -- a constant would pass against a
// stuck or shared gauge. 0 is included because it is a legitimate depth (empty
// mailbox) and the one most easily confused with "never set".
TEST_F(ProcessqDecodePacketTest, InOrderPacketsCarryTheirOwnDepth)
{
  feed(1, 7);
  feed(2, 0);
  feed(3, 42);
  EXPECT_EQ(dec.seqs(), (std::vector<uint32_t>{1, 2, 3}));
  EXPECT_EQ(dec.qlens(), (std::vector<uint32_t>{7, 0, 42}));
}

// The case that actually matters. 4 arrives early carrying depth 99 and is
// buffered. It is not decoded until 3 closes the gap, and 3 carries depth 5.
// Packet 4 must still be decoded at 99 -- the depth it personally queued
// behind -- not at the depth of the packet that happened to trigger the drain.
TEST_F(ProcessqDecodePacketTest, BufferedPacketKeepsItsOwnDepthNotTheTriggersS)
{
  feed(1, 1);
  feed(2, 2);
  feed(4, 99); // gap: buffered, not decoded
  EXPECT_EQ(dec.seqs(), (std::vector<uint32_t>{1, 2}));

  feed(3, 5); // closes the gap -> drains 3 then 4
  EXPECT_EQ(dec.seqs(), (std::vector<uint32_t>{1, 2, 3, 4}));
  EXPECT_EQ(dec.qlens(), (std::vector<uint32_t>{1, 2, 5, 99}));
}

// KNOWN DEFECT, pinned deliberately.
//
// processq(ts, last) takes the ARRIVING packet's recv_ts and passes that same
// ts to every packet it drains. So a packet released out of a gap is decoded
// with the timestamp of whatever packet closed the gap, not its own. Here 4 was
// captured at 1000 but is decoded at 4000, packet 3's capture time.
//
// This is the exact error the qlen deliberately avoids, sitting two lines away
// from it. Asserting it keeps it measured rather than forgotten: when it is
// fixed, this test fails, and the fix is to change the expectation to
// {1000, 2000, 4000, 1000} and rename it.
TEST_F(ProcessqDecodePacketTest, KnownDefect_DrainedPacketGetsTheTriggersRecvTs)
{
  feed(1, 0, /*recv_ts=*/1000);
  feed(2, 0, /*recv_ts=*/2000);
  feed(4, 0, /*recv_ts=*/3000); // buffered
  feed(3, 0, /*recv_ts=*/4000); // closes the gap

  ASSERT_EQ(dec.seqs(), (std::vector<uint32_t>{1, 2, 3, 4}));
  std::vector<uint64_t> ts;
  for (const auto &p : dec.packets) ts.push_back(p.ts);
  // Packet 4's own recv_ts was 3000. It is decoded at 4000.
  EXPECT_EQ(ts, (std::vector<uint64_t>{1000, 2000, 4000, 4000}));
}

// Retransmits and stale packets are dropped, not re-decoded. Worth pinning next
// to the qlen tests: a duplicate that slipped through would emit a second copy
// of every record, each with a DIFFERENT depth, and the pair would look like
// two genuine observations of the same message at two queue depths.
TEST_F(ProcessqDecodePacketTest, DuplicateAndOldSeqnumsAreDropped)
{
  feed(1, 10);
  feed(2, 20);
  feed(2, 30); // duplicate
  feed(1, 40); // stale
  EXPECT_EQ(dec.seqs(), (std::vector<uint32_t>{1, 2}));
  EXPECT_EQ(dec.qlens(), (std::vector<uint32_t>{10, 20}));
}

// Cold start: qseq_num == 0 accepts the first packet at whatever seqnum, and
// that packet's depth is still its own.
TEST_F(ProcessqDecodePacketTest, ColdStartAcceptsFirstSeqnumWithItsDepth)
{
  feed(100, 13);
  EXPECT_EQ(dec.seqs(), (std::vector<uint32_t>{100}));
  EXPECT_EQ(dec.qlens(), (std::vector<uint32_t>{13}));
}

// The decoder is handed the packet's real length, not the ~1.5KB buffer. #118
// removed the drain copy; this is what stops it coming back.
TEST_F(ProcessqDecodePacketTest, DecodePacketCarriesThePacketLengthNotTheBuffer)
{
  feed(1, 0);
  ASSERT_EQ(dec.packets.size(), 1u);
  EXPECT_EQ(dec.packets[0].len, 12u);
}

// ---------------------------------------------------------------------------
// B: end to end, through the real DataDecoder, to the handler member the l3
//    emitters actually read.
// ---------------------------------------------------------------------------

// feed_handler_if has ~20 pure virtuals. DecodeSink is a concrete one already,
// so deriving from it costs two overrides instead of a 70-line stub that rots
// every time a callback signature moves.
struct RecordingHandler : public mdp3::DecodeSink
{
  RecordingHandler() : mdp3::DecodeSink(nullptr, nullptr, en::x::CMEMDFUT) {}

  std::vector<uint32_t> decoded;       // MsgSeqNums, in decode order
  std::vector<uint32_t> qlen_at_decode; // the member as each packet finished

  void set_ingress_qlen(uint32_t q) noexcept override { ingress_qlen_ = q; }

  void EndOfPacket(uint32_t msgSeqNum, uint64_t) noexcept override
  {
    decoded.push_back(msgSeqNum);
    qlen_at_decode.push_back(ingress_qlen_);
  }
};

class ProcessqHandlerQlenTest : public ::testing::Test
{
protected:
  MockActor recovery{"MockRecovery"};
  RecordingHandler cb;
  // parallel decode left off (no set_workers), so every packet takes the inline
  // branch -- which is also what a header-only packet would get anyway.
  mdp3::DataDecoder dec{&cb, /*disable_mbo=*/false, /*max_mbp_level=*/10,
                        /*debug=*/false};
  mdp3::MessageProcessor mp{"test", &recovery, &dec, true, false};

  void feed(uint32_t seqnum, uint32_t qlen)
  {
    auto *pq = make_pkt(seqnum, qlen, /*recv_ts=*/1);
    TestHelper::invoke_handler(&mp, pq, nullptr);
    delete pq;
  }
};

// The whole upstream chain with no mock in it: buffer -> DecodePacket ->
// DataDecoder -> handler member -> read at EndOfPacket.
TEST_F(ProcessqHandlerQlenTest, DepthReachesTheHandlerPerPacket)
{
  feed(1, 7);
  feed(2, 0);
  feed(3, 42);
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2, 3}));
  EXPECT_EQ(cb.qlen_at_decode, (std::vector<uint32_t>{7, 0, 42}));
}

// Same gap case as above, but measured where the l3 emitters read it. A stale
// member here is the failure that produces plausible-but-wrong depths on real
// records, so it is worth asserting at both ends of the chain.
TEST_F(ProcessqHandlerQlenTest, BufferedPacketReachesTheHandlerWithItsOwnDepth)
{
  feed(1, 1);
  feed(2, 2);
  feed(4, 99);
  EXPECT_EQ(cb.qlen_at_decode, (std::vector<uint32_t>{1, 2}));

  feed(3, 5);
  EXPECT_EQ(cb.decoded, (std::vector<uint32_t>{1, 2, 3, 4}));
  EXPECT_EQ(cb.qlen_at_decode, (std::vector<uint32_t>{1, 2, 5, 99}));
}

} // namespace
