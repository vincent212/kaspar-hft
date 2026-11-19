// MoldSequencer: ordering, duplicates, gaps and re-requests, sessions,
// heartbeats, giving up, snapshots (feed.md section 5).
#include "ItchBuilder.hpp"
#include "nasdaq_md/MoldSequencer.hpp"
#include "nasdaq_md/Transport.hpp"

#include <gtest/gtest.h>

using namespace nasdaq_md;
using namespace nasdaq_md::test;

namespace
{
struct Rec : MoldSequencer::Owner
{
  std::vector<uint64_t> seqs;
  std::vector<uint64_t> recv;   // recv_ns of each packet applied
  std::vector<std::pair<uint64_t, uint64_t>> gaps;
  std::vector<std::tuple<std::string, uint64_t, uint16_t>> reqs;
  int sessions = 0, packets = 0;
  bool snapshot_ok = false;
  int snapshot_calls = 0;
  void on_packet_begin(uint64_t, uint64_t r) override
  {
    ++packets;
    recv.push_back(r);
  }
  void on_message(const char *, size_t, uint64_t s) override { seqs.push_back(s); }
  void on_packet_end() override {}
  void on_gap(uint64_t a, uint64_t b) override { gaps.emplace_back(a, b); }
  void on_session(const std::string &, const std::string &) override { ++sessions; }
  void request(const std::string &s, uint64_t q, uint16_t n) override { reqs.emplace_back(s, q, n); }
  bool need_snapshot() override
  {
    ++snapshot_calls;
    return snapshot_ok;
  }
};

// A packet of `count` Delete messages numbered from seq.
std::string pkt(uint64_t seq, uint16_t count, const std::string &session = "SESSION001")
{
  std::string p = MoldPacketizer::header_only(session, seq, count);
  for (uint16_t i = 0; i < count; ++i)
    p += Delete(1, seq + i, 1);
  return p;
}

std::vector<uint64_t> range(uint64_t a, uint64_t b)
{
  std::vector<uint64_t> v;
  for (uint64_t i = a; i <= b; ++i)
    v.push_back(i);
  return v;
}

struct SeqTest : ::testing::Test
{
  Rec own;
  MoldSequencer::Config cfg;
  std::unique_ptr<MoldSequencer> s;
  uint64_t now = 1000;
  void SetUp() override
  {
    cfg.retry_ns = 100;
    cfg.max_tries = 3;
    cfg.max_request = 4;
    s = std::make_unique<MoldSequencer>(own, cfg);
  }
  void in(const std::string &p, uint64_t recv_ns = 0)
  {
    s->on_packet(p.data(), p.size(), recv_ns, now += 10);
  }
};

TEST_F(SeqTest, InOrderPacketsAreAppliedAndNumbered)
{
  in(pkt(1, 3));
  in(pkt(4, 2));
  EXPECT_EQ(own.seqs, range(1, 5));
  EXPECT_EQ(s->next_expected(), 6u);
  EXPECT_TRUE(own.gaps.empty());
}

TEST_F(SeqTest, DuplicatesAndOverlapsAreDropped)
{
  in(pkt(1, 3));
  in(pkt(1, 3));   // feed B's copy
  in(pkt(2, 4));   // overlaps 2-3
  EXPECT_EQ(own.seqs, range(1, 5));
  EXPECT_EQ(s->stats().dup_packets, 1u);
  EXPECT_EQ(s->stats().partial_overlap, 1u);
}

TEST_F(SeqTest, AGapIsHeldReRequestedAndFilled)
{
  in(pkt(1, 2));
  in(pkt(6, 2));   // 3-5 missing
  ASSERT_EQ(own.gaps.size(), 1u);
  EXPECT_EQ(own.gaps[0], std::make_pair(uint64_t(3), uint64_t(5)));
  ASSERT_EQ(own.reqs.size(), 1u);
  EXPECT_EQ(own.reqs[0], std::make_tuple(std::string("SESSION001"), uint64_t(3), uint16_t(3)));
  EXPECT_EQ(own.seqs, range(1, 2));
  in(pkt(8, 1));   // more live data, held
  in(pkt(3, 3));   // the retransmission
  EXPECT_EQ(own.seqs, range(1, 8));
  EXPECT_FALSE(s->in_hole());
}

TEST_F(SeqTest, RequestsAreLimitedAndContinueAfterEachReply)
{
  in(pkt(1, 1));
  in(pkt(12, 1));  // 2-11 missing: 10 messages, 4 per request
  ASSERT_EQ(own.reqs.size(), 1u);
  EXPECT_EQ(std::get<2>(own.reqs[0]), 4);
  in(pkt(2, 4));
  ASSERT_EQ(own.reqs.size(), 2u);
  EXPECT_EQ(std::get<1>(own.reqs[1]), 6u);
  in(pkt(6, 4));
  in(pkt(10, 2));
  EXPECT_EQ(own.seqs, range(1, 12));
}

TEST_F(SeqTest, RetriesThenSkipsWithoutASnapshot)
{
  in(pkt(1, 1));
  in(pkt(5, 1));
  for (int i = 0; i < 5; ++i)
    s->on_timer(now += 200);
  EXPECT_EQ(own.reqs.size(), 3u);   // max_tries
  EXPECT_EQ(own.snapshot_calls, 1);
  EXPECT_EQ(s->stats().lost_messages, 3u);
  EXPECT_EQ(own.seqs, (std::vector<uint64_t>{1, 5}));
}

TEST_F(SeqTest, RetriesThenSnapshotAndResume)
{
  own.snapshot_ok = true;
  in(pkt(1, 1));
  in(pkt(5, 2));    // 5-6 held
  for (int i = 0; i < 5; ++i)
    s->on_timer(now += 200);
  EXPECT_TRUE(s->awaiting_snapshot());
  in(pkt(7, 2));    // held while the spin runs
  in(pkt(9, 1));
  s->resume_from(6, now);   // the snapshot holds 1-5 (decision 1)
  EXPECT_EQ(own.seqs, (std::vector<uint64_t>{1, 6, 7, 8, 9}));
  EXPECT_EQ(s->stats().lost_messages, 0u);
}

TEST_F(SeqTest, ASnapshotTooOldIsFilledByReRequest)
{
  own.snapshot_ok = true;
  s->begin_snapshot();
  in(pkt(10, 2));
  s->resume_from(7, now);   // 7-9 are neither in the snapshot nor held
  ASSERT_EQ(own.reqs.size(), 1u);
  EXPECT_EQ(std::get<1>(own.reqs[0]), 7u);
  in(pkt(7, 3));
  EXPECT_EQ(own.seqs, range(7, 11));
}

TEST_F(SeqTest, HeartbeatsAndEndOfSessionRevealGaps)
{
  in(pkt(1, 2));
  in(MoldPacketizer::header_only("SESSION001", 3, 0));   // nothing missing
  EXPECT_TRUE(own.gaps.empty());
  in(MoldPacketizer::header_only("SESSION001", 6, 0));   // 3-5 missing
  ASSERT_EQ(own.gaps.size(), 1u);
  EXPECT_EQ(own.gaps[0], std::make_pair(uint64_t(3), uint64_t(5)));
  in(pkt(3, 3));
  EXPECT_FALSE(s->in_hole());
  in(MoldPacketizer::header_only("SESSION001", 6, 0xFFFF));
  EXPECT_EQ(s->stats().end_of_session, 1u);
  EXPECT_EQ(s->stats().heartbeats, 2u);
}

TEST_F(SeqTest, ANewSessionIsAResetAndStartsAtItsFirstPacket)
{
  in(pkt(1, 3));
  in(pkt(1, 2, "SESSION002"));
  EXPECT_EQ(own.sessions, 1);
  EXPECT_EQ(own.seqs, (std::vector<uint64_t>{1, 2, 3, 1, 2}));
}

TEST_F(SeqTest, ALateJoinTakesASnapshotWhenConfigured)
{
  cfg.snapshot_on_late_join = true;
  s = std::make_unique<MoldSequencer>(own, cfg);
  own.snapshot_ok = true;
  in(pkt(100, 2));
  EXPECT_TRUE(s->awaiting_snapshot());
  EXPECT_TRUE(own.seqs.empty());
  s->resume_from(101, now);
  EXPECT_EQ(own.seqs, (std::vector<uint64_t>{101}));
}

TEST_F(SeqTest, TooManyHeldPacketsGiveUp)
{
  cfg.max_buffered = 3;
  s = std::make_unique<MoldSequencer>(own, cfg);
  in(pkt(1, 1));
  for (uint64_t q = 10; q < 16; ++q)
    in(pkt(q, 1));
  EXPECT_GE(own.snapshot_calls, 1);
  EXPECT_GT(s->stats().lost_messages, 0u);
}

// A held packet is applied with the receive time it arrived with, not the
// time of the packet that closed the hole.
TEST_F(SeqTest, HeldPacketsKeepTheirReceiveTime)
{
  in(pkt(1, 2), 100);
  in(pkt(6, 2), 600);   // held
  in(pkt(8, 1), 800);   // held
  in(pkt(3, 3), 300);   // closes the hole: 6-7 and 8 drain after it
  EXPECT_EQ(own.seqs, range(1, 8));
  EXPECT_EQ(own.recv, (std::vector<uint64_t>{100, 300, 600, 800}));
}

// While a snapshot is awaited, no more than max_buffered packets are held:
// the oldest go, counted as lost, and the snapshot's N then re-requests the
// stretch between N and the first packet still held.
TEST_F(SeqTest, TheBufferIsBoundedWhileASnapshotIsAwaited)
{
  cfg.max_buffered = 3;
  s = std::make_unique<MoldSequencer>(own, cfg);
  own.snapshot_ok = true;
  s->begin_snapshot();
  for (uint64_t q = 10; q < 16; ++q)
    in(pkt(q, 1));
  EXPECT_EQ(s->buffered(), 3u);
  EXPECT_EQ(s->stats().held_dropped, 3u);
  EXPECT_EQ(s->stats().lost_messages, 3u);
  EXPECT_TRUE(s->awaiting_snapshot());
  s->resume_from(10, now);   // the snapshot covers up to 9; 10-12 were dropped
  ASSERT_EQ(own.reqs.size(), 1u);
  EXPECT_EQ(own.reqs[0], std::make_tuple(std::string("SESSION001"), uint64_t(10), uint16_t(3)));
  in(pkt(10, 3));
  EXPECT_EQ(own.seqs, range(10, 15));
  EXPECT_FALSE(s->in_hole());
}

// A snapshot that fails is abandoned: the stream goes on from the first held
// packet, the hole before it lost.
TEST_F(SeqTest, AnAbandonedSnapshotGoesOnFromTheFirstHeldPacket)
{
  own.snapshot_ok = true;
  in(pkt(1, 1));
  in(pkt(5, 2));
  for (int i = 0; i < 5; ++i)
    s->on_timer(now += 200);
  ASSERT_TRUE(s->awaiting_snapshot());
  in(pkt(7, 2));
  s->abandon_snapshot(now);
  EXPECT_FALSE(s->awaiting_snapshot());
  EXPECT_FALSE(s->in_hole());
  EXPECT_EQ(own.seqs, (std::vector<uint64_t>{1, 5, 6, 7, 8}));
  EXPECT_EQ(s->stats().lost_messages, 3u);
  EXPECT_EQ(s->stats().snapshots_abandoned, 1u);
  in(pkt(9, 1));
  EXPECT_EQ(s->next_expected(), 10u);
}

TEST_F(SeqTest, ShortPacketsAreMalformed)
{
  in(std::string(10, 'x'));
  std::string p = pkt(1, 2);
  p.resize(p.size() - 3);   // the second block is cut
  in(p);
  EXPECT_EQ(s->stats().malformed, 2u);
  EXPECT_EQ(own.seqs, range(1, 1));
}
} // namespace
