/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Sequencing and recovery (feed.md section 5): duplicates, gaps closed by
// re-request, gaps escalated to a GLIMPSE snapshot, snapshot alignment, End of
// Session, a new session, late join, GLIMPSE failures.

#include <gtest/gtest.h>

#include <deque>

#include "TestUtil.hpp"

using namespace jnx_md;
using namespace jnx_md::test;

namespace
{
  std::vector<std::string> day()
  {
    std::vector<std::string> d = {T(30000)};   // 08:20:00
    for (auto &l : table37())
      d.push_back(l);
    d.push_back(R("1301", "DAY", 37, 40000, 20000));
    d.push_back(R("7203", "DAY", 37, 40000, 20000));
    d.push_back(H(1, "1301", "DAY", 'T'));
    d.push_back(H(2, "7203", "DAY", 'T'));
    d.push_back(S(3, "DAY", 'Q'));
    return d;
  }

  // Packets of a small session: the directory, then orders on two books.
  std::vector<std::string> session_packets()
  {
    std::vector<std::vector<std::string>> msgs = {
        day(),
        {A(10, 1, 'B', 100, "1301", "DAY", 25000), A(11, 2, 'S', 200, "1301", "DAY", 25010)},
        {A(12, 3, 'B', 300, "1301", "DAY", 25000), A(13, 4, 'B', 100, "7203", "DAY", 30000)},
        {E(14, 1, 50), U(15, 2, 5, 100, 25010)},
        {A(16, 6, 'S', 100, "7203", "DAY", 30005), D(17, 3)},
        {E(18, 1, 50), A(19, 7, 'B', 400, "1301", "DAY", 24990)},
        {U(20, 7, 8, 400, 25000), A(21, 9, 'S', 100, "1301", "DAY", 25020)},
        {D(22, 9), E(23, 4, 100), A(24, 10, 'B', 100, "7203", "DAY", 29995)},
    };
    std::vector<std::string> out;
    uint64_t seq = 1;
    for (auto &m : msgs)
    {
      out.push_back(mold_packet(kSession, seq, m));
      seq += m.size();
    }
    return out;
  }

  uint64_t first_seq(const std::string &p)
  {
    jnx::moldudp64::from_jnx::DownstreamPacketHeader h;
    std::memcpy(&h, p.data(), sizeof(h));
    return h.SequenceNumber;
  }

  // A handler fed every packet, in order: what the others must end up equal to.
  struct Reference
  {
    RecSink sink;
    Handler h{config(), sink};
    explicit Reference(size_t upto = SIZE_MAX, size_t partial = 0)
    {
      auto p = session_packets();
      for (size_t k = 0; k < p.size() && k < upto; ++k)
        h.on_packet(p[k].data(), p[k].size(), kMidnight + k);
      if (upto < p.size() && partial)
      {
        auto m = mold_messages(p[upto]);
        m.resize(partial);
        auto cut = mold_packet(kSession, first_seq(p[upto]), m);
        h.on_packet(cut.data(), cut.size(), kMidnight + upto);
      }
    }
  };

  // Every book of a and b: the same orders, sizes and queue order.
  bool same_books(const Handler &a, const Handler &b)
  {
    for (const auto &ia : a.instruments().all())
    {
      const Instrument *ib = const_cast<Handler &>(b).instruments().find(ia->key);
      if (!ib)
        return ia->book.empty();
      auto cmp = [](const auto &x, const auto &y) {
        if (x.size() != y.size())
          return false;
        for (auto i = x.begin(), j = y.begin(); i != x.end(); ++i, ++j)
        {
          if (i->first != j->first || i->second.q.size() != j->second.q.size())
            return false;
          for (auto o = i->second.q.begin(), q = j->second.q.begin(); o != i->second.q.end();
               ++o, ++q)
            if ((*o)->number != (*q)->number || (*o)->qty != (*q)->qty)
              return false;
        }
        return true;
      };
      if (!cmp(ia->book.bids, ib->book.bids) || !cmp(ia->book.asks, ib->book.asks))
        return false;
    }
    return true;
  }

  size_t count_all(const RecSink &s, bool gap)
  {
    size_t n = 0;
    for (const auto &r : s.recs)
      if (r.where == Rec::All &&
          (gap ? std::holds_alternative<bfile::l3_gap_v2_t>(r.r)
               : std::holds_alternative<bfile::l3_chr_v2_t>(r.r)))
        ++n;
    return n;
  }
}

TEST(Handler, InOrder)
{
  Reference ref;
  EXPECT_EQ(ref.h.stats().gaps, 0u);
  EXPECT_EQ(ref.h.stats().messages, 28u);
  EXPECT_EQ(ref.h.next_seq(), 29u);
  EXPECT_EQ(ref.h.dispatcher().orders().size(), 4u);   // 5, 6, 8 and 10 rest
}

TEST(Handler, DuplicatesAndStraddlingPackets)
{
  RecSink sink;
  Handler h(config(), sink);
  auto p = session_packets();
  for (size_t k = 0; k < p.size(); ++k)
  {
    h.on_packet(p[k].data(), p[k].size(), kMidnight + k);
    h.on_packet(p[k].data(), p[k].size(), kMidnight + k);   // every packet twice
  }
  // A packet that starts below the expected number: only its new messages apply.
  auto m = mold_messages(p[3]);
  const auto m4 = mold_messages(p[4]);
  m.insert(m.end(), m4.begin(), m4.end());
  EXPECT_EQ(h.stats().dup_packets, p.size());
  EXPECT_EQ(h.stats().gaps, 0u);
  Reference ref;
  EXPECT_TRUE(same_books(ref.h, h));

  RecSink sink2;
  Handler h2(config(), sink2);
  for (size_t k = 0; k < 4; ++k)
    h2.on_packet(p[k].data(), p[k].size(), kMidnight + k);
  // packets 3 and 4 merged into one that starts at packet 3's first number
  auto merged = mold_packet(kSession, first_seq(p[3]), m);
  h2.on_packet(merged.data(), merged.size(), kMidnight + 4);
  for (size_t k = 5; k < p.size(); ++k)
    h2.on_packet(p[k].data(), p[k].size(), kMidnight + k);
  EXPECT_EQ(h2.stats().old_messages, 2u);
  EXPECT_TRUE(same_books(ref.h, h2));
}

TEST(Handler, GapClosedByRerequest)
{
  RecSink sink;
  std::vector<std::pair<uint64_t, uint32_t>> asked;
  Handler::Hooks hooks;
  hooks.rerequest = [&](std::string_view s, uint64_t seq, uint32_t n) {
    EXPECT_EQ(s, kSession);
    asked.emplace_back(seq, n);
    return true;
  };
  Handler h(config(), sink, hooks);
  auto p = session_packets();
  for (size_t k : {0, 1, 4, 5})   // packets 2 and 3 lost
    h.on_packet(p[k].data(), p[k].size(), kMidnight + k);
  ASSERT_EQ(asked.size(), 1u);
  EXPECT_EQ(asked[0].first, first_seq(p[2]));
  EXPECT_EQ(asked[0].second, first_seq(p[4]) - first_seq(p[2]));
  EXPECT_EQ(h.mode(), Handler::Mode::Buffering);
  EXPECT_EQ(h.buffered(), 2u);
  // The reply holds only packet 2 (one UDP packet): the rest is asked again.
  h.on_packet(p[2].data(), p[2].size(), kMidnight + 6);
  ASSERT_EQ(asked.size(), 2u);
  EXPECT_EQ(asked[1].first, first_seq(p[3]));
  h.on_packet(p[3].data(), p[3].size(), kMidnight + 7);
  EXPECT_EQ(h.mode(), Handler::Mode::Live);
  for (size_t k = 6; k < p.size(); ++k)
    h.on_packet(p[k].data(), p[k].size(), kMidnight + 8 + k);
  Reference ref;
  EXPECT_TRUE(same_books(ref.h, h));
  EXPECT_EQ(count_all(sink, true), 0u);   // no gap record: recovery closed it
}

// A heartbeat carries the next number, so it reveals a gap with nothing buffered.
TEST(Handler, HeartbeatRevealsAGap)
{
  RecSink sink;
  std::vector<std::pair<uint64_t, uint32_t>> asked;
  Handler::Hooks hooks;
  hooks.rerequest = [&](std::string_view, uint64_t seq, uint32_t n) {
    asked.emplace_back(seq, n);
    return true;
  };
  Handler h(config(), sink, hooks);
  auto p = session_packets();
  h.on_packet(p[0].data(), p[0].size(), kMidnight);
  auto hb = mold_packet(kSession, first_seq(p[2]), {});
  h.on_packet(hb.data(), hb.size(), kMidnight + 1);
  ASSERT_EQ(asked.size(), 1u);
  EXPECT_EQ(asked[0].first, first_seq(p[1]));
  EXPECT_EQ(asked[0].second, 2u);
  h.on_packet(p[1].data(), p[1].size(), kMidnight + 2);
  EXPECT_EQ(h.mode(), Handler::Mode::Live);
}

// No re-request server: a gap record, a channel reset, a snapshot; End of
// Snapshot falls inside a buffered packet, whose earlier messages are dropped.
TEST(Handler, GapEscalatedToSnapshot)
{
  RecSink sink;
  bool asked = false;
  Handler::Hooks hooks;
  hooks.request_snapshot = [&] { asked = true; };
  Handler h(config(), sink, hooks);
  auto p = session_packets();
  for (size_t k : {0, 1, 2})
    h.on_packet(p[k].data(), p[k].size(), kMidnight + k);
  h.on_packet(p[4].data(), p[4].size(), kMidnight + 4);   // packet 3 lost
  EXPECT_TRUE(asked);
  EXPECT_EQ(h.mode(), Handler::Mode::Snapshot);
  EXPECT_EQ(count_all(sink, true), 1u);
  EXPECT_EQ(count_all(sink, false), 1u);
  EXPECT_TRUE(h.dispatcher().orders().empty());
  h.on_packet(p[5].data(), p[5].size(), kMidnight + 5);   // buffered meanwhile

  // The snapshot is taken after the first message of packet 4.
  Reference at(4, 1);
  const uint64_t N = first_seq(p[4]) + 1;
  const std::string snap = glimpse_snapshot(at.h, N);
  sink.clear();
  for (char c : snap)   // one byte at a time: framing across reads
    h.on_glimpse_bytes(&c, 1);
  EXPECT_EQ(h.mode(), Handler::Mode::Live);
  EXPECT_EQ(h.next_seq(), first_seq(p[6]));
  EXPECT_EQ(h.stats().old_messages, 1u);
  // Snapshot orders are recovery adds stamped 1 ns before the snapshot time.
  size_t rec = 0;
  for (const auto &m : sink.of<bfile::l3_mbo_v2_t>())
    if (m.recovery)
    {
      ++rec;
      EXPECT_EQ(m.orderUpdateAction, 0);
      EXPECT_EQ(m.transactTime, at.h.dispatcher().now() - 1);
    }
  EXPECT_EQ(rec, at.h.dispatcher().orders().size());
  for (size_t k = 6; k < p.size(); ++k)
    h.on_packet(p[k].data(), p[k].size(), kMidnight + k);
  Reference ref;
  EXPECT_TRUE(same_books(ref.h, h));
  // Market hours from the clock (08:20:00 is open): status 17 again.
  for (const auto &i : h.instruments().all())
    EXPECT_EQ(i->status_sent, 17);
}

TEST(Handler, RerequestTimeoutEscalates)
{
  RecSink sink;
  int snapshots = 0;
  Handler::Hooks hooks;
  hooks.rerequest = [](std::string_view, uint64_t, uint32_t) { return true; };
  hooks.request_snapshot = [&] { ++snapshots; };
  auto c = config();
  c.rerequest_timeout_ns = 1000;
  Handler h(c, sink, hooks);
  auto p = session_packets();
  h.on_packet(p[0].data(), p[0].size(), kMidnight);
  h.on_packet(p[2].data(), p[2].size(), kMidnight + 10);
  EXPECT_EQ(h.mode(), Handler::Mode::Buffering);
  h.tick(kMidnight + 500);
  EXPECT_EQ(snapshots, 0);
  h.tick(kMidnight + 2000);
  EXPECT_EQ(snapshots, 1);
  EXPECT_EQ(h.mode(), Handler::Mode::Snapshot);
}

// A snapshot older than the buffer leaves a hole, which is re-requested.
TEST(Handler, SnapshotBelowTheBuffer)
{
  RecSink sink;
  std::vector<uint64_t> asked;
  Handler::Hooks hooks;
  hooks.rerequest = [&](std::string_view, uint64_t seq, uint32_t) {
    asked.push_back(seq);
    return true;
  };
  Handler h(config(false, 0, Handler::Join::Snapshot), sink, hooks);
  auto p = session_packets();
  h.on_packet(p[5].data(), p[5].size(), kMidnight);   // late join
  EXPECT_EQ(h.mode(), Handler::Mode::Snapshot);
  Reference at(3);
  h.on_glimpse_bytes(glimpse_snapshot(at.h, first_seq(p[3])).data(),
                     glimpse_snapshot(at.h, first_seq(p[3])).size());
  ASSERT_EQ(asked.size(), 1u);
  EXPECT_EQ(asked[0], first_seq(p[3]));
  h.on_packet(p[3].data(), p[3].size(), kMidnight + 1);
  h.on_packet(p[4].data(), p[4].size(), kMidnight + 2);
  EXPECT_EQ(h.mode(), Handler::Mode::Live);
  for (size_t k = 6; k < p.size(); ++k)
    h.on_packet(p[k].data(), p[k].size(), kMidnight + k);
  Reference ref;
  EXPECT_TRUE(same_books(ref.h, h));
}

TEST(Handler, EndOfSessionClearsEverything)
{
  Reference ref;
  ref.sink.clear();
  auto eos = mold_packet(kSession, ref.h.next_seq(), {}, 0xFFFF);
  ref.h.on_packet(eos.data(), eos.size(), kMidnight + 100);
  EXPECT_EQ(count_all(ref.sink, false), 1u);
  EXPECT_EQ(ref.h.mode(), Handler::Mode::Ended);
  EXPECT_TRUE(ref.h.dispatcher().orders().empty());
  for (const auto &i : ref.h.instruments().all())
    EXPECT_TRUE(i->book.empty());
  ref.h.on_packet(eos.data(), eos.size(), kMidnight + 101);   // repeated once a second
  EXPECT_EQ(count_all(ref.sink, false), 1u);
  EXPECT_EQ(ref.h.stats().packets_ignored_after_eos, 1u);
}

TEST(Handler, NewSessionResets)
{
  Reference ref;
  ref.sink.clear();
  auto p = mold_packet("1697745684", 1, day());
  ref.h.on_packet(p.data(), p.size(), kMidnight + 100);
  EXPECT_EQ(count_all(ref.sink, false), 1u);
  EXPECT_EQ(ref.h.session(), "1697745684");
  EXPECT_EQ(ref.h.next_seq(), 1u + day().size());
  EXPECT_TRUE(ref.h.dispatcher().orders().empty());
}

// A failed GLIMPSE session is asked for again after glimpse_retry_ns, the wait
// doubling on each failure up to glimpse_retry_max_ns; a snapshot that
// completes starts the backoff over. A failure reported twice (the packet in
// the bytes, then the client's report) gives one retry.
TEST(Handler, GlimpseFailureIsRetriedWithABackoff)
{
  RecSink sink;
  int snapshots = 0;
  Handler::Hooks hooks;
  hooks.request_snapshot = [&] { ++snapshots; };
  auto c = config(false, 0, Handler::Join::Snapshot);
  c.glimpse_retry_ns = 1000;
  c.glimpse_retry_max_ns = 2500;
  Handler h(c, sink, hooks);
  auto p = session_packets();
  h.on_packet(p[2].data(), p[2].size(), kMidnight);
  EXPECT_EQ(snapshots, 1);
  const std::string rej = soup_packet('J', "A");
  h.on_glimpse_bytes(rej.data(), rej.size());
  h.glimpse_failed("GLIMPSE closed the connection");   // the client's report of the same
  EXPECT_EQ(snapshots, 1);
  EXPECT_EQ(h.stats().glimpse_failures, 2u);
  EXPECT_EQ(h.glimpse_retry_at(), kMidnight + 1000);
  h.tick(kMidnight + 999);
  EXPECT_EQ(snapshots, 1);
  h.tick(kMidnight + 1000);
  EXPECT_EQ(snapshots, 2);
  EXPECT_EQ(h.stats().glimpse_retries, 1u);
  EXPECT_EQ(h.glimpse_retry_at(), 0u);
  // bytes of the failed session, arriving late, are ignored
  const std::string z = soup_packet('Z', "");
  h.on_glimpse_bytes(z.data(), z.size());   // the second session fails too
  EXPECT_EQ(h.glimpse_retry_at(), kMidnight + 1000 + 2000);
  h.on_glimpse_bytes(z.data(), z.size());
  EXPECT_EQ(h.stats().glimpse_bytes_ignored, 1u);
  h.tick(kMidnight + 3000);
  EXPECT_EQ(snapshots, 3);
  h.on_glimpse_bytes(z.data(), z.size());
  EXPECT_EQ(h.glimpse_retry_at(), kMidnight + 3000 + 2500);   // capped
  h.tick(kMidnight + 5500);
  EXPECT_EQ(snapshots, 4);
  EXPECT_EQ(h.mode(), Handler::Mode::Snapshot);
  // a snapshot completes: the next failure waits glimpse_retry_ns again
  Reference at(3);
  const std::string snap = glimpse_snapshot(at.h, first_seq(p[3]));
  h.on_glimpse_bytes(snap.data(), snap.size());
  EXPECT_EQ(h.mode(), Handler::Mode::Live);
  h.on_packet(p[3].data(), p[3].size(), kMidnight + 6000);
  h.force_recovery("test");
  EXPECT_EQ(snapshots, 5);
  h.on_glimpse_bytes(rej.data(), rej.size());
  EXPECT_EQ(h.glimpse_retry_at(), kMidnight + 6000 + 1000);
}

// The stream's own number, not the packet count, is the sequence: a packet
// whose header says more messages than it holds still moves the sequence on.
TEST(Handler, ShortPacketIsCounted)
{
  RecSink sink;
  Handler h(config(), sink);
  auto p = session_packets();
  h.on_packet(p[0].data(), p[0].size(), kMidnight);
  std::string bad = p[1].substr(0, p[1].size() - 5);   // last message cut
  h.on_packet(bad.data(), bad.size(), kMidnight + 1);
  EXPECT_EQ(h.stats().malformed, 1u);
  EXPECT_EQ(h.next_seq(), first_seq(p[2]));
  std::string tiny = "short";
  h.on_packet(tiny.data(), tiny.size(), kMidnight + 2);
  EXPECT_EQ(h.stats().short_packets, 1u);
}
