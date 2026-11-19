// NasdaqFeed end to end: MoldUDP64 packets through the sequencer and the
// dispatcher; re-requests over a real UDP socket to a fake re-request server
// on the loopback; the pcap path; GLIMPSE alignment against an uninterrupted
// book; the records into a real OB.
#include "ItchBuilder.hpp"
#include "nasdaq_md/NasdaqFeed.hpp"
#include "nasdaq_md/Transport.hpp"

#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <random>
#include <thread>

using namespace nasdaq_md;
using namespace nasdaq_md::test;

namespace
{
// A day in miniature: a directory, three securities, orders, executions,
// replaces, cancels, deletes, prints.
std::vector<std::string> day(int norders = 400, unsigned seed = 7)
{
  std::vector<std::string> m;
  m.push_back(SysEvent('O', 1));
  m.push_back(Dir(1, "AAA", 2));
  m.push_back(Dir(2, "BBB", 3));
  m.push_back(Dir(3, "SUBC", 4));
  for (uint16_t l = 1; l <= 3; ++l)
    m.push_back(Action(l, l == 1 ? "AAA" : l == 2 ? "BBB" : "SUBC", 'T', 5));
  m.push_back(SysEvent('S', 6));
  std::mt19937 rng(seed);
  struct Live { uint64_t ref; uint16_t loc; bool buy; uint32_t shares; };
  std::vector<Live> live;
  uint64_t ref = 100, ts = 1000;
  // Bids below the base price, offers above: the book never crosses.
  auto price = [&](uint16_t l, bool buy, uint32_t k) {
    const uint32_t base = l == 3 ? 5000 : 1000000, step = l == 3 ? 1 : 100;
    return buy ? base - step * k : base + step * k;
  };
  const char *names[] = {"", "AAA", "BBB", "SUBC"};
  for (int i = 0; i < norders * 3; ++i)
  {
    ts += rng() % 3;   // equal timestamps happen
    const int what = live.empty() ? 0 : int(rng() % 6);
    if (what <= 1)
    {
      const uint16_t l = uint16_t(1 + rng() % 3);
      const bool buy = rng() % 2;
      const uint32_t sh = 100 * (1 + rng() % 5);
      m.push_back(Add(l, ++ref, buy ? 'B' : 'S', sh, price(l, buy, 1 + rng() % 5), ts, names[l]));
      live.push_back({ref, l, buy, sh});
      continue;
    }
    const size_t k = rng() % live.size();
    auto &o = live[k];
    const uint16_t l = o.loc;
    switch (what)
    {
    case 2:
      m.push_back(Exec(l, o.ref, 100, ts));
      o.shares -= 100;
      break;
    case 3:
      m.push_back(Cancel(l, o.ref, 100, ts));
      o.shares -= 100;
      break;
    case 4:
      m.push_back(Delete(l, o.ref, ts));
      o.shares = 0;
      break;
    case 5:
      o.shares = 100 * (1 + rng() % 3);
      m.push_back(Replace(l, o.ref, ++ref, o.shares, price(l, o.buy, 1 + rng() % 5), ts));
      o.ref = ref;
      break;
    }
    if (o.shares == 0)
      live.erase(live.begin() + long(k));
    if (rng() % 10 == 0)
      m.push_back(TradeP(l, 50, price(l, true, 0), ts));
  }
  return m;
}

std::vector<std::string> packets(const std::vector<std::string> &msgs, unsigned per,
                                 const std::string &session = "SESSION001")
{
  std::vector<std::string> p;
  MoldPacketizer pz(session, 1, per);
  for (auto &m : msgs)
    if (pz.add(m.data(), m.size()))
      p.push_back(pz.packet());
  if (pz.flush())
    p.push_back(pz.packet());
  return p;
}

// The reference: every message straight into a dispatcher.
struct Reference
{
  Instruments ins{test_registrar()};
  RecSink sink;
  Dispatcher d{test_config(), ins, sink};
  explicit Reference(const std::vector<std::string> &msgs, size_t upto = SIZE_MAX)
  {
    for (size_t i = 0; i < msgs.size() && i < upto; ++i)
      d.on_itch(msgs[i].data(), msgs[i].size(), i + 1);
  }
};

// Levels and queues of two own books.
void expect_same_book(const OwnBook &a, const OwnBook &b)
{
  EXPECT_EQ(a.size(), b.size());
  const auto qa = a.queues(), qb = b.queues();
  ASSERT_EQ(qa.size(), qb.size());
  for (const auto &[k, v] : qa)
  {
    auto it = qb.find(k);
    ASSERT_NE(it, qb.end());
    ASSERT_EQ(v.size(), it->second.size());
    for (size_t i = 0; i < v.size(); ++i)
    {
      EXPECT_EQ(v[i].first, it->second[i].first) << "queue order at " << k.px;
      EXPECT_EQ(v[i].second.shares, it->second[i].second.shares);
    }
  }
}

// Unwraps no-op invariants of the generated stream.
TEST(Day, TheSyntheticDayIsConsistent)
{
  auto msgs = day();
  Reference r(msgs);
  EXPECT_GT(r.d.book().size(), 50u);
  for (char t : {'E', 'X', 'D', 'U'})
    EXPECT_EQ(r.d.stats().unknown_ref[uint8_t(t)], 0u);
  EXPECT_EQ(r.d.stats().crossed_17, 0u);
}

struct NoReq : Requester
{
  void request(const std::string &, uint64_t, uint16_t) override {}
};

TEST(Feed, PacketsGiveTheSameBookAsTheMessages)
{
  auto msgs = day();
  Reference ref(msgs);
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  NasdaqFeed feed(test_config(), sc, ins, sink);
  uint64_t now = 0;
  for (auto &p : packets(msgs, 5))
  {
    feed.on_packet(p.data(), p.size(), 0, ++now);
    feed.on_packet(p.data(), p.size(), 0, ++now);   // feed B
  }
  expect_same_book(feed.dispatcher().book(), ref.d.book());
  EXPECT_EQ(feed.sequencer().stats().dup_packets, packets(msgs, 5).size());
  // the same records reach the books (B's copies add nothing)
  EXPECT_EQ(sink.data().size(), ref.sink.data().size());
}

// ---- re-request over UDP ----------------------------------------------------
// A fake MoldUDP64 re-request server on 127.0.0.1: answers each Request Packet
// with one downstream packet holding as many of the requested messages as fit.
class FakeRerequestServer
{
public:
  explicit FakeRerequestServer(std::vector<std::string> msgs) : msgs_(std::move(msgs))
  {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof a);
    socklen_t n = sizeof a;
    getsockname(fd_, reinterpret_cast<sockaddr *>(&a), &n);
    port_ = ntohs(a.sin_port);
    th_ = std::thread([this] { loop(); });
  }
  ~FakeRerequestServer()
  {
    stop_ = true;
    th_.join();
    close(fd_);
  }
  uint16_t port() const { return port_; }
  int requests() const { return requests_; }

private:
  std::vector<std::string> msgs_;
  int fd_;
  uint16_t port_;
  std::thread th_;
  std::atomic<bool> stop_{false};
  std::atomic<int> requests_{0};
  void loop()
  {
    char b[1500];
    while (!stop_)
    {
      pollfd p{fd_, POLLIN, 0};
      if (poll(&p, 1, 50) <= 0)
        continue;
      sockaddr_in from{};
      socklen_t fl = sizeof from;
      const ssize_t n = recvfrom(fd_, b, sizeof b, 0, reinterpret_cast<sockaddr *>(&from), &fl);
      if (n != 20)
        continue;
      ++requests_;
      nasdaq::mold::to_nasdaq::RequestPacket r;
      std::memcpy(&r, b, 20);
      const uint64_t seq = r.SequenceNumber;
      std::string body;
      uint16_t k = 0;
      for (uint64_t s = seq; k < uint16_t(r.RequestedMessageCount) && s <= msgs_.size(); ++s, ++k)
      {
        if (body.size() + msgs_[s - 1].size() > 1400)
          break;
        body += msgs_[s - 1];
      }
      const std::string out =
          MoldPacketizer::header_only(std::string(r.Session, 10), seq, k) + body;
      sendto(fd_, out.data(), out.size(), 0, reinterpret_cast<sockaddr *>(&from), fl);
    }
  }
};

TEST(Feed, DroppedPacketsAreRecoveredFromAReRequestServerOverUdp)
{
  auto msgs = day(600);
  Reference ref(msgs);
  FakeRerequestServer server(msgs);
  UdpRequester req("127.0.0.1", server.port());
  ASSERT_TRUE(req.ok());
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  sc.retry_ns = 20'000'000;
  sc.max_request = 50;
  NasdaqFeed feed(test_config(), sc, ins, sink, &req);
  auto pk = packets(msgs, 4);
  uint64_t now = 0, dropped = 0;
  std::string reply;
  for (size_t i = 0; i < pk.size(); ++i)
  {
    if (i % 7 == 3 || (i >= 100 && i < 130))   // single drops and a 30-packet hole
    {
      ++dropped;
      continue;
    }
    feed.on_packet(pk[i].data(), pk[i].size(), 0, now += 1000);
    while (req.receive(reply, feed.sequencer().in_hole() ? 100 : 0))
      feed.on_packet(reply.data(), reply.size(), 0, now += 1000);
  }
  // the tail: drain outstanding requests
  for (int k = 0; k < 50 && feed.sequencer().in_hole(); ++k)
  {
    feed.on_timer(now += 30'000'000);
    while (req.receive(reply, 100))
      feed.on_packet(reply.data(), reply.size(), 0, now += 1000);
  }
  EXPECT_FALSE(feed.sequencer().in_hole());
  EXPECT_EQ(feed.sequencer().stats().lost_messages, 0u);
  EXPECT_GT(server.requests(), 0);
  EXPECT_GT(dropped, 30u);
  expect_same_book(feed.dispatcher().book(), ref.d.book());
  for (char t : {'E', 'X', 'D', 'U'})
    EXPECT_EQ(feed.dispatcher().stats().unknown_ref[uint8_t(t)], 0u);
  // a gap record went to the books for each hole
  EXPECT_EQ(feed.dispatcher().stats().gaps, feed.sequencer().stats().gaps);
}

// ---- pcap -------------------------------------------------------------------
TEST(Feed, PcapReplayGivesTheSameBook)
{
  auto msgs = day();
  Reference ref(msgs);
  char path[] = "/tmp/nasdaq_md_test_XXXXXX";
  const int fd = mkstemp(path);
  ASSERT_GE(fd, 0);
  close(fd);
  {
    PcapWriter w(path);
    uint64_t ts = 1781236800000000000ull;
    for (auto &p : packets(msgs, 6))
      w.write(p, 26477, ts += 1000);
  }
  PcapReader r(path);
  ASSERT_TRUE(r.ok());
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  NasdaqFeed feed(test_config(), sc, ins, sink);
  std::string pl;
  uint16_t port = 0;
  uint64_t ts = 0;
  size_t n = 0;
  while (r.next(pl, port, ts))
  {
    EXPECT_EQ(port, 26477);
    feed.on_packet(pl.data(), pl.size(), ts, ts);
    ++n;
  }
  unlink(path);
  EXPECT_EQ(n, packets(msgs, 6).size());
  expect_same_book(feed.dispatcher().book(), ref.d.book());
}

// ---- GLIMPSE ------------------------------------------------------------------
struct SpinOnRequest : SnapshotSource
{
  bool asked = false;
  int starts = 0, cancels = 0;
  std::string session;   // of the last spin started
  bool start(const std::string &s) override
  {
    asked = true;
    ++starts;
    session = s;
    return true;
  }
  void cancel() override { ++cancels; }
};

// Decision 1: the spin holds 1..N-1 and End of Snapshot says N; live from N.
// A gap opens at packet 40, re-requests go unanswered, the handler asks for a
// spin; the spin is built from the reference at N and fed in uneven TCP
// chunks after more live packets have arrived; the result equals the
// uninterrupted book, queue order included (decision 2: spin order).
TEST(Glimpse, RecoveryAlignsWithTheLiveStream)
{
  auto msgs = day(800);
  Reference full(msgs);
  auto pk = packets(msgs, 4);
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  sc.retry_ns = 1;
  sc.max_tries = 2;
  SpinOnRequest snap;
  NoReq noreq;
  NasdaqFeed feed(test_config(), sc, ins, sink, &noreq, &snap);
  uint64_t now = 0;
  size_t i = 0;
  for (; i < 40; ++i)
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  // packets 40-44 lost
  for (i = 45; i < 50; ++i)
  {
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
    feed.on_timer(now += 10);
  }
  ASSERT_TRUE(snap.asked);
  ASSERT_TRUE(feed.sequencer().awaiting_snapshot());
  // the spin is taken now: it holds every message before packet 60's first
  const uint64_t n = 60 * 4 + 1;
  Reference at_n(msgs, n - 1);
  for (; i < 70; ++i)   // live packets keep coming during the spin
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  SpinWriter w;
  w.login_accepted("GLIMPSE001");
  w.spin(at_n.d.book(), at_n.ins, n, 1500, 'Q');
  const auto &b = w.bytes();
  for (size_t o = 0; o < b.size(); o += 37)
    feed.on_glimpse_bytes(b.data() + o, std::min<size_t>(37, b.size() - o));
  EXPECT_FALSE(feed.sequencer().awaiting_snapshot());
  EXPECT_EQ(feed.snapshot_n(), n);
  for (; i < pk.size(); ++i)
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  expect_same_book(feed.dispatcher().book(), full.d.book());
  EXPECT_EQ(feed.dispatcher().stats().boundary_suspect, 0u);
  EXPECT_EQ(feed.dispatcher().stats().recoveries, 1u);
  for (char t : {'E', 'X', 'D', 'U'})
    EXPECT_EQ(feed.dispatcher().stats().unknown_ref[uint8_t(t)], 0u);
  // the spin's adds went out as recovery records, after a CHR
  size_t chr = 0, rec = 0;
  for (auto &[a, r] : sink.book)
  {
    chr += std::holds_alternative<bfile::l3_chr_v2_t>(r);
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&r))
      rec += m->recovery;
  }
  EXPECT_EQ(chr, 3u);   // one per routed book
  EXPECT_EQ(rec, at_n.d.book().size());
}

// A spin that fails (the session ends before End of Snapshot) is given up:
// the feed goes on live from the first packet it held, the stretch before it
// lost, and a second spin later rebuilds the books. Bytes of another
// session's spin are dropped.
TEST(Glimpse, AFailedSpinIsGivenUpAndASecondSpinRecovers)
{
  auto msgs = day(800);
  Reference full(msgs);
  auto pk = packets(msgs, 4);
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  sc.retry_ns = 1;
  sc.max_tries = 2;
  SpinOnRequest snap;
  NoReq noreq;
  NasdaqFeed feed(test_config(), sc, ins, sink, &noreq, &snap);
  uint64_t now = 0;
  size_t i = 0;
  for (; i < 40; ++i)
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  for (i = 45; i < 50; ++i)   // packets 40-44 lost
  {
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
    feed.on_timer(now += 10);
  }
  ASSERT_TRUE(feed.spinning());
  EXPECT_EQ(snap.session, "SESSION001");
  feed.on_glimpse_bytes("xx", 2, "SESSION000");   // not this spin's
  EXPECT_EQ(feed.spin_bytes_dropped(), 1u);
  feed.on_glimpse_failed("SESSION000");            // nor is this failure
  EXPECT_TRUE(feed.spinning());
  feed.on_glimpse_failed("SESSION001");
  EXPECT_FALSE(feed.spinning());
  EXPECT_FALSE(feed.sequencer().awaiting_snapshot());
  EXPECT_EQ(feed.spins_failed(), 1u);
  EXPECT_EQ(feed.sequencer().stats().lost_messages, 20u);   // packets 40-44
  EXPECT_EQ(feed.sequencer().next_expected(), 50u * 4 + 1); // 45-49 were applied
  for (; i < 70; ++i)   // live, on a book missing the lost messages
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  EXPECT_EQ(feed.sequencer().buffered(), 0u);
  // the retry
  ASSERT_TRUE(feed.recover());
  EXPECT_EQ(snap.starts, 2);
  const uint64_t n = 80 * 4 + 1;
  Reference at_n(msgs, n - 1);
  for (; i < 80; ++i)
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  SpinWriter w;
  w.login_accepted("GLIMPSE001");
  w.spin(at_n.d.book(), at_n.ins, n, 1500, 'Q');
  feed.on_glimpse_bytes(w.bytes().data(), w.bytes().size(), "SESSION001");
  EXPECT_FALSE(feed.spinning());
  EXPECT_EQ(feed.spins_done(), 1u);
  for (; i < pk.size(); ++i)
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  expect_same_book(feed.dispatcher().book(), full.d.book());
  EXPECT_EQ(feed.dispatcher().stats().recoveries, 2u);
  EXPECT_EQ(feed.dispatcher().stats().boundary_suspect, 0u);
}

// A new MoldUDP64 session while a spin is in flight: the spin is the old
// session's, so it is cancelled and what it still delivers is dropped; the
// new session, joined late, gets a spin of its own.
TEST(Glimpse, ASessionChangeCancelsTheSpinInFlight)
{
  auto msgs = day(200);
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  sc.snapshot_on_late_join = true;
  SpinOnRequest snap;
  NasdaqFeed feed(test_config(), sc, ins, sink, nullptr, &snap);
  auto pk = packets(msgs, 4);
  uint64_t now = 0;
  for (size_t i = 0; i < 10; ++i)
    feed.on_packet(pk[i].data(), pk[i].size(), 0, ++now);
  ASSERT_TRUE(feed.recover());
  EXPECT_EQ(snap.session, "SESSION001");
  auto pk2 = packets(msgs, 4, "SESSION002");
  feed.on_packet(pk2[0].data(), pk2[0].size(), 0, ++now);   // seq 1: no late join
  EXPECT_EQ(snap.cancels, 1);
  EXPECT_EQ(feed.spins_cancelled(), 1u);
  EXPECT_FALSE(feed.spinning());
  EXPECT_FALSE(feed.sequencer().awaiting_snapshot());
  EXPECT_EQ(feed.sequencer().stats().sessions, 1u);
  // the old spin's bytes, arriving late, change nothing
  SpinWriter w;
  w.login_accepted("GLIMPSE001");
  w.spin(feed.dispatcher().book(), ins, 5, 1500, 'Q');
  feed.on_glimpse_bytes(w.bytes().data(), w.bytes().size(), "SESSION001");
  EXPECT_EQ(feed.spins_done(), 0u);
  EXPECT_GT(feed.spin_bytes_dropped(), 0u);
  EXPECT_EQ(feed.sequencer().next_expected(), 5u);
  // a third session joined late while the second's spin... none runs: a spin
  // for the third is started and tagged with it
  auto pk3 = packets(msgs, 4, "SESSION003");
  feed.on_packet(pk3[10].data(), pk3[10].size(), 0, ++now);
  EXPECT_TRUE(feed.spinning());
  EXPECT_EQ(snap.session, "SESSION003");
  EXPECT_EQ(feed.spin_session(), "SESSION003");
  EXPECT_EQ(snap.starts, 2);
}

// ---- the GLIMPSE TCP client against a fake server on the loopback ------------
// Accepts one connection, reads the Login Request, sends `reply`, closes.
class FakeGlimpseServer
{
public:
  explicit FakeGlimpseServer(std::string reply) : reply_(std::move(reply))
  {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof a);
    socklen_t n = sizeof a;
    getsockname(fd_, reinterpret_cast<sockaddr *>(&a), &n);
    port_ = ntohs(a.sin_port);
    listen(fd_, 1);
    th_ = std::thread([this] {
      const int c = accept(fd_, nullptr, nullptr);
      if (c < 0)
        return;
      char b[256];
      size_t got = 0;
      while (got < 49)   // the Login Request
      {
        const ssize_t r = recv(c, b, sizeof b, 0);
        if (r <= 0)
          break;
        got += size_t(r);
      }
      login_seen_ = got >= 49;
      (void)::send(c, reply_.data(), reply_.size(), 0);
      close(c);
    });
  }
  ~FakeGlimpseServer()
  {
    th_.join();
    close(fd_);
  }
  uint16_t port() const { return port_; }
  bool login_seen() const { return login_seen_; }

private:
  std::string reply_;
  int fd_;
  uint16_t port_;
  std::thread th_;
  std::atomic<bool> login_seen_{false};
};

std::string end_of_snapshot_packet(uint64_t n)
{
  SpinWriter w;
  Msg g;
  g.c('G').s(std::to_string(n), 20);
  w.put(*reinterpret_cast<const nasdaq::glimpse::from_nasdaq::EndOfSnapshot *>(g.b.data()));
  return w.bytes();
}

TEST(Glimpse, TheClientFailsWhenTheSessionEndsBeforeEndOfSnapshot)
{
  const std::string eos(std::string("\0\1Z", 3));
  SpinWriter w;
  w.login_accepted("GLIMPSE001");
  FakeGlimpseServer server(w.bytes() + eos);
  GlimpseClient c("127.0.0.1", server.port(), "user", "pw");
  std::string got;
  EXPECT_FALSE(c.run([&](const char *p, size_t n) { got.append(p, n); }, 5));
  EXPECT_TRUE(server.login_seen());
  EXPECT_EQ(got.size(), w.bytes().size() + 3);
}

TEST(Glimpse, TheClientSucceedsOnEndOfSnapshotThenEndOfSession)
{
  const std::string eos(std::string("\0\1Z", 3));
  SpinWriter w;
  w.login_accepted("GLIMPSE001");
  FakeGlimpseServer server(w.bytes() + end_of_snapshot_packet(7) + eos);
  GlimpseClient c("127.0.0.1", server.port(), "user", "pw");
  std::string got;
  EXPECT_TRUE(c.run([&](const char *p, size_t n) { got.append(p, n); }, 5));
  EXPECT_GE(got.size(), w.bytes().size() + 24);
}

TEST(Glimpse, TheClientFailsOnALoginRejected)
{
  FakeGlimpseServer server(std::string("\0\2JA", 4));
  GlimpseClient c("127.0.0.1", server.port(), "user", "pw");
  EXPECT_FALSE(c.run([](const char *, size_t) {}, 5));
}

// The other reading of decision 1 (N already in the snapshot) is visible at N.
TEST(Glimpse, ASpinThatAlreadyHoldsNIsFlagged)
{
  auto msgs = day(300, 11);
  // find a Delete at least 100 messages in
  size_t k = 100;
  while (msgs[k][2] != 'D')
    ++k;
  const uint64_t n = k + 1;                     // its sequence number
  Reference at(msgs, n);                        // holds 1..N: the wrong reading
  Instruments ins{test_registrar()};
  RecSink sink;
  MoldSequencer::Config sc;
  SpinOnRequest snap;
  NasdaqFeed feed(test_config(), sc, ins, sink, nullptr, &snap);
  auto first = packets(msgs, 1);
  feed.on_packet(first[0].data(), first[0].size(), 0, 1);
  ASSERT_TRUE(feed.recover());
  SpinWriter w;
  w.spin(at.d.book(), at.ins, n, 1500, 'Q');
  feed.on_glimpse_bytes(w.bytes().data(), w.bytes().size());
  for (size_t i = 1; i < first.size(); ++i)
    feed.on_packet(first[i].data(), first[i].size(), 0, 2 + i);
  EXPECT_EQ(feed.dispatcher().stats().boundary_suspect, 1u);
}

TEST(Glimpse, SoupStreamReassemblesSplitPackets)
{
  SpinWriter w;
  w.login_accepted("ABC");
  Msg g;
  g.c('G').s(std::string(19, ' ') + "7", 20);
  w.put(*reinterpret_cast<const nasdaq::glimpse::from_nasdaq::EndOfSnapshot *>(g.b.data()));
  std::string session;
  uint64_t login_seq = 0;
  std::vector<std::string> payloads;
  SoupStream s(SoupStream::Events{
      [&](const char *p, size_t n) { payloads.emplace_back(p, n); },
      [&](std::string_view sess, uint64_t q) {
        session = sess;
        login_seq = q;
      },
      {}, {}});
  for (char c : w.bytes())
    s.feed(&c, 1);
  EXPECT_EQ(session, "ABC");
  EXPECT_EQ(login_seq, 1u);
  ASSERT_EQ(payloads.size(), 1u);
  EXPECT_EQ(payloads[0].size(), 21u);
}

TEST(Glimpse, LoginRequestsSequenceOne)
{
  const std::string l = glimpse_login("user", "pw");
  ASSERT_EQ(l.size(), 49u);
  EXPECT_EQ(l[2], 'L');
  EXPECT_EQ(l.substr(3, 6), "user  ");
  EXPECT_EQ(l.substr(29 + 19, 1), "1");
}

// ---- into a real OB ------------------------------------------------------------
struct TestableOB : public frame::ob::act::OB
{
  using frame::ob::act::OB::OB;
  using frame::ob::act::OB::reply_to;
};

struct OBSink : Sink
{
  std::map<int32_t, TestableOB *> books;
  unit_test::MockActor probe{"probe"};
  void to_book(int32_t a, const bfile::l3_t &r) override
  {
    auto it = books.find(a);
    if (it == books.end())
      return;
    frame::mda::msg::Data d(true);
    d.l3 = r;
    it->second->reply_to = &probe;
    unit_test::TestHelper::invoke_handler(it->second, &d, &probe);
    it->second->reply_to = nullptr;
  }
  std::string get(TestableOB *ob, const frame::cons::msg::Get &g)
  {
    probe.clear();
    ob->reply_to = &probe;
    unit_test::TestHelper::invoke_handler(ob, &g, &probe);
    ob->reply_to = nullptr;
    auto p = probe.get_message<frame::cons::msg::Page>(0);
    return p ? p->val : "";
  }
};

class OBTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    const std::string u = "/tmp/nasdaq_md_test_universe.csv";
    std::ofstream(u) << "# empty\n";
    frame::ref::RefData::set_universe(u, "");
    frame::ref::RefData::inst();
  }
};

TEST_F(OBTest, TheSyntheticDayInRealOBsMatchesTheOwnBook)
{
  auto msgs = day(1500, 3);
  OBSink obs;
  std::vector<std::unique_ptr<TestableOB>> keep;
  auto base = refdata_registrar(char(en::x::NASDAQ), {}, false, "NQTEST.", 1900000000, 0.01,
                                {{"SUBC", 0.0001}});
  Instruments ins([&](Instrument &i) -> int32_t {
    const int32_t a = base(i);
    if (a >= 0)
    {
      boost::property_tree::ptree pt;
      pt.put("ladder_ticks", 1 << 12);
      keep.push_back(std::make_unique<TestableOB>(nullptr, true, nullptr, uint(a), pt));
      obs.books[a] = keep.back().get();
    }
    return a;
  });
  MoldSequencer::Config sc;
  NasdaqFeed feed(test_config(), sc, ins, obs);
  uint64_t now = 0;
  for (auto &p : packets(msgs, 3))
    feed.on_packet(p.data(), p.size(), 0, ++now);
  const auto &own = feed.dispatcher().book();
  for (uint16_t loc = 1; loc <= 3; ++loc)
  {
    auto *ob = obs.books.at(ins.asset(loc));
    const long tick = long(ins.at(loc).tick);
    long b = -1, a = -1;
    std::sscanf(obs.get(ob, frame::cons::msg::Get("bbbo")).c_str(), "BBBO: %ld %ld", &b, &a);
    EXPECT_EQ(b * tick, long(own.best_bid(loc))) << ins.at(loc).symbol;
    if (own.best_ask(loc))
      EXPECT_EQ(a * tick, long(own.best_ask(loc))) << ins.at(loc).symbol;
    for (bool bid : {true, false})
      for (const auto &[px, lv] : own.top(loc, bid, 10))
      {
        long sz = -1, cnt = -1, sim = -1;
        frame::cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(px / tick)}});
        std::sscanf(obs.get(ob, g).c_str(), "QAT: %ld %ld %ld", &sz, &cnt, &sim);
        EXPECT_EQ(sz, long(lv.shares)) << ins.at(loc).symbol << " " << px;
        EXPECT_EQ(cnt, long(lv.count)) << ins.at(loc).symbol << " " << px;
      }
  }
}
} // namespace

// ---- the actor -----------------------------------------------------------------
#include "nasdaq_md/act/NasdaqMD.hpp"
#include "nasdaq_md/if/NasdaqMD.hpp"

namespace
{
TEST_F(OBTest, TheActorSendsDataMessagesToTheInstrumentsBook)
{
  nasdaq_md::FeedConfig cfg;
  cfg.dispatch = test_config();
  cfg.symbols = {"ACTX"};
  cfg.asset_prefix = "NQACT.";
  cfg.fast_send = false;   // MockActor captures send()
  unit_test::MockActor book("book"), rec("binrec");
  std::vector<actors::Actor *> books(100000, &book);
  std::unique_ptr<nasdaq_md::act::NasdaqMD> md(create_NasdaqMD(cfg, books, &rec));
  EXPECT_EQ(create_NasdaqMD_reader(cfg, md.get(), 'A'), nullptr);   // no group configured
  std::vector<std::string> msgs = {Dir(5, "ACTX", 1), Dir(6, "OTHER", 1), SysEvent('S', 2),
                                   Action(5, "ACTX", 'T', 3), Add(5, 1, 'B', 100, 1000000, 4),
                                   Add(6, 2, 'B', 100, 1000000, 4)};
  for (auto &p : packets(msgs, 10))
  {
    mcast_recv::msg::ProcessQ<uint64_t> q;
    std::memcpy(q.buf.message.data(), p.data(), p.size());
    q.buf.len = p.size();
    q.buf.recv_ts = 5;
    unit_test::TestHelper::invoke_handler(md.get(), &q, nullptr);
  }
  const int32_t a = md->instruments().asset(5);
  ASSERT_GE(a, 0);
  EXPECT_LT(md->instruments().asset(6), 0);
  size_t mbo = 0, sst = 0, eob = 0;
  for (size_t i = 0; i < book.message_count(); ++i)
  {
    auto *d = book.get_message<frame::mda::msg::Data>(i);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->israw);
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&d->l3))
    {
      ++mbo;
      EXPECT_EQ(m->securityID, a);
      EXPECT_EQ(m->handlerendtim, 5u);
    }
    sst += std::holds_alternative<bfile::l3_sst_t>(d->l3);
    eob += std::holds_alternative<bfile::l3_eob_t>(d->l3);
  }
  EXPECT_EQ(mbo, 1u);
  EXPECT_EQ(sst, 3u);   // 21, then 2 at S, then 17 at T
  EXPECT_EQ(eob, 1u);
  // the recorder: every record sent to a book except EOB, and one EOP per packet
  size_t eop = 0;
  for (size_t i = 0; i < rec.message_count(); ++i)
    eop += std::holds_alternative<bfile::l3_eop_t>(rec.get_message<frame::mda::msg::Data>(i)->l3);
  EXPECT_EQ(eop, 1u);
  EXPECT_EQ(rec.message_count(), mbo + sst + eop);
  book.clear();
  rec.clear();
}

// A failed spin schedules another after glimpse_retry_ms, doubling on each
// failure up to glimpse_retry_max_ms; a spin that completes resets the wait.
// Reports of a spin for another session are ignored.
TEST_F(OBTest, AFailedSpinIsRetriedWithABackoff)
{
  // a port nobody listens on: a bound, then closed, loopback socket's
  const int probe = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(probe, reinterpret_cast<sockaddr *>(&a), sizeof a);
  socklen_t alen = sizeof a;
  getsockname(probe, reinterpret_cast<sockaddr *>(&a), &alen);
  close(probe);

  nasdaq_md::FeedConfig cfg;
  cfg.dispatch = test_config();
  cfg.symbols = {"ACTX"};
  cfg.asset_prefix = "NQRETRY.";
  cfg.fast_send = false;
  cfg.glimpse_host = "127.0.0.1";
  cfg.glimpse_port = ntohs(a.sin_port);
  cfg.glimpse_retry_ms = 10;
  cfg.glimpse_retry_max_ms = 25;
  unit_test::MockActor book("book"), rec("binrec");
  std::vector<actors::Actor *> books(100000, &book);
  std::unique_ptr<nasdaq_md::act::NasdaqMD> md(create_NasdaqMD(cfg, books, &rec));
  EXPECT_EQ(md->glimpse_backoff_ns(), 10'000'000u);
  EXPECT_EQ(md->glimpse_retry_at(), 0u);
  ASSERT_TRUE(md->feed().recover());   // the client thread fails to connect
  ASSERT_TRUE(md->feed().spinning());
  const std::string session = md->feed().spin_session();

  nasdaq_md::msg::GlimpseBytes stale;
  stale.session = "OTHERSESSN";
  stale.failed = true;
  unit_test::TestHelper::invoke_handler(md.get(), &stale, nullptr);
  EXPECT_TRUE(md->feed().spinning());
  EXPECT_EQ(md->glimpse_retry_at(), 0u);

  const uint64_t t0 = 1'000'000'000'000ull;
  nasdaq_md::msg::GlimpseBytes failed;
  failed.session = session;
  failed.failed = true;
  unit_test::TestHelper::invoke_handler(md.get(), &failed, nullptr);
  EXPECT_FALSE(md->feed().spinning());
  EXPECT_EQ(md->feed().spins_failed(), 1u);
  EXPECT_GT(md->glimpse_retry_at(), 0u);
  EXPECT_EQ(md->glimpse_backoff_ns(), 20'000'000u);
  // not before it is due
  md->on_timer(t0);
  const uint64_t due = md->glimpse_retry_at();
  EXPECT_FALSE(md->feed().spinning());
  md->on_timer(due);
  EXPECT_EQ(md->glimpse_retry_at(), 0u);
  EXPECT_TRUE(md->feed().spinning());
  // the second failure: 20 ms, then capped at 25
  unit_test::TestHelper::invoke_handler(md.get(), &failed, nullptr);
  EXPECT_EQ(md->glimpse_backoff_ns(), 25'000'000u);
  md->on_timer(md->glimpse_retry_at());
  unit_test::TestHelper::invoke_handler(md.get(), &failed, nullptr);
  EXPECT_EQ(md->glimpse_backoff_ns(), 25'000'000u);
  md->on_timer(md->glimpse_retry_at());
  ASSERT_TRUE(md->feed().spinning());
  // a spin that completes (End of Snapshot) resets the wait
  nasdaq_md::msg::GlimpseBytes done;
  done.session = session;
  done.bytes = end_of_snapshot_packet(1);
  unit_test::TestHelper::invoke_handler(md.get(), &done, nullptr);
  EXPECT_FALSE(md->feed().spinning());
  EXPECT_EQ(md->feed().spins_done(), 1u);
  EXPECT_EQ(md->glimpse_backoff_ns(), 10'000'000u);
  book.clear();
  rec.clear();
}
} // namespace
