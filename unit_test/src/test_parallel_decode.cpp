/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Parallel decode: DataDecoderActor (record) -> HandlerIfActor (replay in order),
// and MessageProcessor's dispatch / recovery gating. All actors are driven
// synchronously through TestHelper::invoke_handler; no threads.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"
#include "unit_test/NullFeedHandler.hpp"
#include "mdp3/DataDecoder.hpp"
#include "mdp3/act/DataDecoderActor.hpp"
#include "mdp3/act/HandlerIfActor.hpp"
#include "mdp3/act/MessageProcessor.hpp"
#include "mdp3/msg/DecodeDone.hpp"
#include "mdp3/msg/DecodedPacket.hpp"
#include "mdp3/msg/DoDataRecovery.hpp"
#include "mdp3/msg/ParDecodePacket.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "mdp3_sbe/ChannelReset4.h"
#include "mdp3_sbe/MDIncrementalRefreshBook46.h"

using namespace unit_test;

namespace {

struct TraceHandler : public NullFeedHandler
{
  std::vector<std::string> trace;
  void add(std::string s) { trace.push_back(std::move(s)); }

  void MDIncrementalRefreshBook(uint64_t rt, uint32_t sq, uint64_t tt, uint64_t st,
      int32_t sec, int64_t pm, int8_t pe, char side, int32_t sz, int32_t no,
      uint8_t lvl, bool eoe, bool rec) noexcept override
  {
    add("mbp " + std::to_string(rt) + " " + std::to_string(sq) + " " + std::to_string(tt) +
        " " + std::to_string(st) + " " + std::to_string(sec) + " " + std::to_string(pm) +
        " " + std::to_string(pe) + " " + side + " " + std::to_string(sz) + " " +
        std::to_string(no) + " " + std::to_string(lvl) + " " + std::to_string(eoe) +
        std::to_string(rec));
  }
  void MDIncrementalRefreshBook(uint64_t rt, uint32_t sq, uint64_t tt, uint64_t st,
      int32_t sec, int64_t pm, int8_t pe, char side, int32_t q, uint64_t oid,
      uint8_t act, uint64_t pri, bool lq, bool eoe, bool rec) noexcept override
  {
    add("mbo " + std::to_string(rt) + " " + std::to_string(sq) + " " + std::to_string(tt) +
        " " + std::to_string(st) + " " + std::to_string(sec) + " " + std::to_string(pm) +
        " " + std::to_string(pe) + " " + side + " " + std::to_string(q) + " " +
        std::to_string(oid) + " " + std::to_string(act) + " " + std::to_string(pri) + " " +
        std::to_string(lq) + std::to_string(eoe) + std::to_string(rec));
  }
  void ChannelReset(uint32_t sq, uint64_t tt, uint64_t st, const char *t) noexcept override
  {
    add("reset " + std::to_string(sq) + " " + std::to_string(tt) + " " +
        std::to_string(st) + " " + (t ? std::string(t, 1) : std::string("null")));
  }
  void EndOfPacket(uint32_t sq, uint64_t st) noexcept override
  {
    add("eop " + std::to_string(sq) + " " + std::to_string(st));
  }
  void Gap() noexcept override { add("gap"); }
  void set_ingress_qlen(uint32_t q) noexcept override { add("qlen " + std::to_string(q)); }
};

// One MDP3 packet: MsgSeqNum(4) SendingTime(8), then per message
// MsgSize(2) + SBE header(8) + body.
struct PacketBuilder
{
  std::vector<char> buf;

  PacketBuilder(uint32_t seq, uint64_t sending_time)
  {
    buf.resize(12);
    std::memcpy(&buf[0], &seq, 4);
    std::memcpy(&buf[4], &sending_time, 8);
  }

  template <typename F>
  void add(F &&encode)
  {
    const std::size_t at = buf.size();
    buf.resize(at + 1500);
    const std::uint64_t body = encode(&buf[at + 2], 1500 - 2);
    const uint16_t msg_size = uint16_t(2 + 8 + body);
    std::memcpy(&buf[at], &msg_size, 2);
    buf.resize(at + msg_size);
  }
};

std::vector<char> sample_packet()
{
  PacketBuilder pb(777, 1111);
  pb.add([](char *p, std::uint64_t cap) {
    sbe::MDIncrementalRefreshBook46 m;
    m.wrapAndApplyHeader(p, 0, cap).transactTime(2222);
    auto &e = m.noMDEntriesCount(2);
    e.next().mDEntrySize(5).securityID(42).rptSeq(1).numberOfOrders(1).mDPriceLevel(1)
        .mDUpdateAction(sbe::MDUpdateAction::New).mDEntryType(sbe::MDEntryTypeBook::Bid);
    e.mDEntryPx().mantissa(5000250000000LL);
    e.next().mDEntrySize(7).securityID(42).rptSeq(2).numberOfOrders(2).mDPriceLevel(1)
        .mDUpdateAction(sbe::MDUpdateAction::New).mDEntryType(sbe::MDEntryTypeBook::Offer);
    e.mDEntryPx().mantissa(5000500000000LL);
    auto &o = m.noOrderIDEntriesCount(2);
    o.next().orderID(9001).mDOrderPriority(11).mDDisplayQty(5).referenceID(1)
        .orderUpdateAction(sbe::OrderUpdateAction::New);
    o.next().orderID(9002).mDOrderPriority(12).mDDisplayQty(7).referenceID(2)
        .orderUpdateAction(sbe::OrderUpdateAction::New);
    return m.encodedLength();
  });
  pb.add([](char *p, std::uint64_t cap) {
    sbe::ChannelReset4 m;
    m.wrapAndApplyHeader(p, 0, cap).transactTime(3333);
    m.noMDEntriesCount(1).next().applID(310);
    return m.encodedLength();
  });
  return pb.buf;
}

// A DecodedPacket whose only recorded call is EndOfPacket(tag, 0).
mdp3::msg::DecodedPacket *decoded(uint64_t id, uint32_t epoch, uint32_t tag,
                                  bool rc = true, bool with_call = true)
{
  auto *d = new mdp3::msg::DecodedPacket();
  d->dispatch_id = id;
  d->epoch = epoch;
  d->sn = 100 + uint32_t(id);
  d->qlen = 0;
  d->rc = rc;
  if (with_call)
    d->calls.emplace_back([tag](mdp3::feed_handler_if &h) { h.EndOfPacket(tag, 0); });
  return d;
}

std::vector<std::string> without_qlen(const std::vector<std::string> &t)
{
  std::vector<std::string> out;
  for (const auto &s : t)
    if (s.rfind("qlen ", 0) != 0)
      out.push_back(s);
  return out;
}

class HandlerIfActorTest : public ::testing::Test
{
protected:
  TraceHandler cb;
  MockActor mp{"MockMP"};
  mdp3::HandlerIfActor h{310, &cb};

  void SetUp() override { h.set_message_processor(&mp); }
  void TearDown() override { mp.clear(); }

  void deliver(mdp3::msg::DecodedPacket *d)
  {
    TestHelper::invoke_handler(&h, d, nullptr);
    delete d;
  }

  const mdp3::msg::DecodeDone *done(size_t i) const
  {
    return mp.get_message<mdp3::msg::DecodeDone>(i);
  }
};

} // namespace

TEST(ParallelDecodeReplay, ReplayOfRealSbePacketMatchesSerialDecode)
{
  const auto pkt = sample_packet();

  TraceHandler serial;
  mdp3::DataDecoder dec(&serial, /*disable_mbo=*/false, /*max_mbp_level=*/10, false, 310);
  bool serial_reset = false;
  std::vector<char> copy(pkt);
  ASSERT_TRUE(dec.mbo_data(copy.data(), copy.size(), /*ts=*/4444, serial_reset));

  TraceHandler parallel;
  MockActor fwd{"MockHandlerActor"};
  mdp3::DataDecoderActor worker(310, 0, &fwd, false, 10);
  auto *pd = new mdp3::msg::ParDecodePacket(0, 0, 777, pkt.data(), pkt.size(), 4444, 3);
  TestHelper::invoke_handler(&worker, pd, nullptr);
  delete pd;
  ASSERT_EQ(fwd.message_count(), 1u);
  const auto *d = fwd.get_message<mdp3::msg::DecodedPacket>(0);
  ASSERT_NE(d, nullptr);

  MockActor mp{"MockMP"};
  mdp3::HandlerIfActor h(310, &parallel);
  h.set_message_processor(&mp);
  TestHelper::invoke_handler(&h, d, nullptr);
  fwd.clear();

  int n_book = 0, n_reset = 0;
  for (const auto &s : serial.trace)
  {
    n_book += s.rfind("mbo ", 0) == 0 || s.rfind("mbp ", 0) == 0;
    n_reset += s.rfind("reset ", 0) == 0;
  }
  EXPECT_GE(n_book, 2);
  EXPECT_EQ(n_reset, 1);
  EXPECT_EQ(serial.trace.back(), "eop 777 1111");
  EXPECT_EQ(parallel.trace.front(), "qlen 3");
  EXPECT_EQ(without_qlen(parallel.trace), serial.trace);

  ASSERT_EQ(mp.message_count(), 1u);
  const auto *dd = mp.get_message<mdp3::msg::DecodeDone>(0);
  ASSERT_NE(dd, nullptr);
  EXPECT_TRUE(dd->rc);
  EXPECT_TRUE(dd->applied);
  EXPECT_EQ(dd->is_channel_reset, serial_reset);
  EXPECT_EQ(dd->sn, 777u);
  mp.clear();
}

TEST_F(HandlerIfActorTest, OutOfOrderPacketsAreAppliedInDispatchOrder)
{
  deliver(decoded(2, 0, 2));
  deliver(decoded(0, 0, 0));
  EXPECT_EQ(h.num_buffered(), 1u);
  deliver(decoded(1, 0, 1));

  EXPECT_EQ(without_qlen(cb.trace),
            (std::vector<std::string>{"eop 0 0", "eop 1 0", "eop 2 0"}));
  EXPECT_EQ(h.num_buffered(), 0u);
  EXPECT_EQ(h.next_dispatch_id(), 3u);
  ASSERT_EQ(mp.message_count(), 3u);
  for (size_t i = 0; i < 3; ++i)
  {
    ASSERT_NE(done(i), nullptr);
    EXPECT_EQ(done(i)->dispatch_id, i);
    EXPECT_TRUE(done(i)->applied);
  }
}

TEST_F(HandlerIfActorTest, EmptyPacketDoesNotStallLaterPackets)
{
  deliver(decoded(1, 0, 1));
  deliver(decoded(0, 0, 0, true, /*with_call=*/false));

  EXPECT_EQ(without_qlen(cb.trace), (std::vector<std::string>{"eop 1 0"}));
  EXPECT_EQ(h.next_dispatch_id(), 2u);
  EXPECT_EQ(mp.message_count(), 2u);
}

TEST_F(HandlerIfActorTest, FailedPacketGapsAndDropsTheRestOfItsEpoch)
{
  deliver(decoded(0, 0, 0, /*rc=*/false));
  deliver(decoded(1, 0, 1));
  deliver(decoded(2, 1, 2));

  EXPECT_EQ(without_qlen(cb.trace),
            (std::vector<std::string>{"eop 0 0", "gap", "eop 2 0"}));
  ASSERT_EQ(mp.message_count(), 3u);
  EXPECT_FALSE(done(0)->rc);
  EXPECT_TRUE(done(0)->applied);
  EXPECT_FALSE(done(1)->applied);
  EXPECT_TRUE(done(2)->applied);
  EXPECT_TRUE(done(2)->rc);
}

TEST_F(HandlerIfActorTest, DuplicateDispatchIdIsIgnored)
{
  deliver(decoded(0, 0, 0));
  deliver(decoded(0, 0, 0));
  EXPECT_EQ(without_qlen(cb.trace), (std::vector<std::string>{"eop 0 0"}));
  EXPECT_EQ(mp.message_count(), 1u);
}

namespace {

mcast_recv::msg::ProcessQ<uint32_t> *make_pkt(uint32_t seqnum)
{
  auto *pq = new mcast_recv::msg::ProcessQ<uint32_t>();
  auto &b = pq->buf;
  std::memset(&b, 0, sizeof(b));
  b.seqnum = seqnum;
  b.recv_ts = 1;
  b.len = 12;
  std::memcpy(&b.message[0], &seqnum, sizeof(seqnum));
  return pq;
}

class ParallelMessageProcessorTest : public ::testing::Test
{
protected:
  MockActor recovery{"MockRecovery"};
  MockActor handler_actor{"MockHandlerActor"};
  MockActor w0{"W0"}, w1{"W1"};
  mdp3::MessageProcessor mp{"test", &recovery, &handler_actor, /*dorecovery=*/true,
                            /*recoveryonstart=*/false};

  void SetUp() override { mp.set_workers({&w0, &w1}); }
  void TearDown() override
  {
    recovery.clear();
    handler_actor.clear();
    w0.clear();
    w1.clear();
  }

  void feed(uint32_t sn)
  {
    auto *pq = make_pkt(sn);
    TestHelper::invoke_handler(&mp, pq, nullptr);
    delete pq;
  }

  void ack(uint64_t id, uint32_t sn, bool rc = true)
  {
    auto *m = new mdp3::msg::DecodeDone(id, sn, rc, false, true);
    TestHelper::invoke_handler(&mp, m, nullptr);
    delete m;
  }
};

} // namespace

TEST_F(ParallelMessageProcessorTest, InOrderPacketsAreDispatchedRoundRobin)
{
  feed(1);
  feed(2);
  feed(3);

  ASSERT_EQ(w0.message_count(), 2u);
  ASSERT_EQ(w1.message_count(), 1u);
  EXPECT_EQ(w0.get_message<mdp3::msg::ParDecodePacket>(0)->dispatch_id, 0u);
  EXPECT_EQ(w0.get_message<mdp3::msg::ParDecodePacket>(0)->sn, 1u);
  EXPECT_EQ(w1.get_message<mdp3::msg::ParDecodePacket>(0)->dispatch_id, 1u);
  EXPECT_EQ(w1.get_message<mdp3::msg::ParDecodePacket>(0)->sn, 2u);
  EXPECT_EQ(w0.get_message<mdp3::msg::ParDecodePacket>(1)->dispatch_id, 2u);
  EXPECT_EQ(w0.get_message<mdp3::msg::ParDecodePacket>(1)->sn, 3u);
  EXPECT_EQ(mp.num_inflight(), 3u);
  EXPECT_EQ(handler_actor.message_count(), 0u);
}

TEST_F(ParallelMessageProcessorTest, GapRecoveryWaitsForInflightPackets)
{
  feed(1);
  feed(3); // gap: waitcnt 3 -> 2
  feed(3); // 2 -> 1
  feed(3); // 1 -> 0: recovery owed, but packet 1 is still in flight

  EXPECT_TRUE(mp.waiting_to_recover());
  EXPECT_FALSE(recovery.has_message_of_type<mdp3::msg::DoDataRecovery>());

  ack(0, 1);
  EXPECT_EQ(mp.num_inflight(), 0u);
  EXPECT_FALSE(mp.waiting_to_recover());
  EXPECT_TRUE(recovery.has_message_of_type<mdp3::msg::DoDataRecovery>());
}

TEST_F(ParallelMessageProcessorTest, DecodeFailureBumpsEpochAndRecovers)
{
  feed(1);
  feed(2);
  ack(0, 1, /*rc=*/false); // packet 2 still in flight
  EXPECT_TRUE(mp.waiting_to_recover());
  EXPECT_FALSE(recovery.has_message_of_type<mdp3::msg::DoDataRecovery>());

  auto *dropped = new mdp3::msg::DecodeDone(1, 2, true, false, /*applied=*/false);
  TestHelper::invoke_handler(&mp, dropped, nullptr);
  delete dropped;

  EXPECT_TRUE(recovery.has_message_of_type<mdp3::msg::DoDataRecovery>());
}
