/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The handler driving real OB instances (frame::ob::act::OB), registered as the
// instrument loader registers them: OB's levels ("bbbo", "qat") equal the
// handler's book, through executions, replaces, a snapshot recovery and End
// of Session; a crossed book outside market hours is not an error.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>

#include "BookCheck.hpp"
#include "TestUtil.hpp"
#include "frame/ref/RefData.hpp"
#include "jnx_md/act/JnxFeed.hpp"

using namespace jnx_md;
using namespace jnx_md::test;

namespace
{
  class ObTest : public ::testing::Test
  {
  protected:
    static void SetUpTestSuite()
    {
      const auto seed = std::filesystem::temp_directory_path() / "jnx_test_universe.csv";
      std::ofstream(seed) << "# jnx tests\n";
      frame::ref::RefData::set_universe(seed.string());
      frame::ref::RefData::inst();
    }

    check::ObSink obs{true};
    std::unique_ptr<Handler> h;
    std::unique_ptr<Feeder> feed;

    void SetUp() override
    {
      static std::atomic<int> n{0};
      auto c = config(true);
      c.instruments.name_prefix = "JNXT" + std::to_string(n++) + ":";
      h = std::make_unique<Handler>(c, obs);
      feed = std::make_unique<Feeder>(Feeder{*h});
    }

    void directory(uint32_t secs = 30000, bool q = true)
    {
      std::vector<std::string> d = {T(secs)};
      for (auto &l : table37())
        d.push_back(l);
      d.push_back(R("1301", "DAY", 37, 40000, 20000));
      d.push_back(R("9984", "DAY", 37, 450000, 350000));   // 35,000-45,000 JPY: tick 5 JPY
      d.push_back(H(1, "1301", "DAY", 'T'));
      d.push_back(H(2, "9984", "DAY", 'T'));
      if (q)
        d.push_back(S(3, "DAY", 'Q'));
      (*feed)(d);
    }

    const Instrument &inst(const char *sicc)
    {
      char s[4] = {' ', ' ', ' ', ' '}, g[4] = {'D', 'A', 'Y', ' '};
      std::memcpy(s, sicc, 4);
      return *h->instruments().find(book_key(s, g));
    }

    check::ObCompare compare()
    {
      check::ObCompare c;
      for (const auto &[a, ob] : obs.obs())
        if (const Instrument *i = h->instruments().by_asset(a))
          check::compare_ob(*i, *ob, 100, 2000, c);
      return c;
    }
  };
}

TEST_F(ObTest, LevelsFollowTheFeed)
{
  directory();
  (*feed)({A(10, 1, 'B', 100, "1301", "DAY", 25000), A(11, 2, 'B', 300, "1301", "DAY", 25000),
           A(12, 3, 'S', 200, "1301", "DAY", 25010), A(13, 4, 'S', 500, "1301", "DAY", 25020)});
  (*feed)({E(20, 1, 40), U(21, 2, 5, 200, 25000), U(22, 3, 6, 200, 25005), D(23, 4)});
  (*feed)({E(30, 1, 60), A(31, 7, 'B', 100, "1301", "DAY", 24995)});
  auto c = compare();
  EXPECT_EQ(c.instruments, 1u);
  EXPECT_EQ(c.mismatched_levels, 0u);
  EXPECT_EQ(c.bbo_mismatches, 0u);
  for (const auto &e : c.examples)
    ADD_FAILURE() << e;
  auto *ob = obs.ob(inst("1301").asset);
  ASSERT_NE(ob, nullptr);
  EXPECT_EQ(check::bbbo(*ob), std::make_pair(25000, 25005));
  const auto l = check::qat(*ob, true, 25000);
  EXPECT_EQ(l.qty, 200);
  EXPECT_EQ(l.count, 1);
}

// 9984's limits (35,000 to 45,000 JPY) lie in the 5 JPY band: one Kaspar tick is 5 JPY,
// so 40,010 JPY is tick 8,002.
TEST_F(ObTest, CoarseTick)
{
  directory();
  const Instrument &i = inst("9984");
  EXPECT_EQ(i.unit, 50u);
  EXPECT_EQ(i.maxpx, int(450000 / 50 + 2));
  EXPECT_DOUBLE_EQ(frame::ref::RefData::get_asset(size_t(i.asset))->get_units(), 5.0);
  (*feed)({A(10, 1, 'B', 100, "9984", "DAY", 400100), A(11, 2, 'S', 100, "9984", "DAY", 400200),
           A(12, 3, 'S', 100, "9984", "DAY", 499950)});   // above the limit: off OB's ladder
  auto *ob = obs.ob(i.asset);
  ASSERT_NE(ob, nullptr);
  EXPECT_EQ(check::bbbo(*ob), std::make_pair(8002, 8004));
  EXPECT_EQ(h->stats().px_out_of_limits, 1u);
}

// Recovery: gap, channel reset, snapshot, live again; OB equals the handler.
TEST_F(ObTest, SnapshotRecovery)
{
  directory();
  (*feed)({A(10, 1, 'B', 100, "1301", "DAY", 25000), A(11, 2, 'S', 200, "1301", "DAY", 25010)});
  // A gap with no re-request server: snapshot.
  feed->seq += 2;
  (*feed)({A(20, 5, 'B', 100, "1301", "DAY", 24990)});
  EXPECT_EQ(h->mode(), Handler::Mode::Snapshot);
  // The snapshot (written by hand from what the venue would hold): orders 1
  // and 3 (an order the gap hid), then End of Snapshot at the buffered packet.
  RecSink ref_sink;
  Handler ref(config(), ref_sink);
  Feeder rf{ref};
  {
    std::vector<std::string> d = {T(30000)};
    for (auto &l : table37())
      d.push_back(l);
    d.push_back(R("1301", "DAY", 37, 40000, 20000));
    d.push_back(R("9984", "DAY", 37, 450000, 350000));
    d.push_back(H(1, "1301", "DAY", 'T'));
    d.push_back(H(2, "9984", "DAY", 'T'));
    d.push_back(S(3, "DAY", 'Q'));
    rf(d);
  }
  rf({A(10, 1, 'B', 100, "1301", "DAY", 25000), A(11, 3, 'S', 300, "1301", "DAY", 25020)});
  const std::string snap = glimpse_snapshot(ref, feed->seq - 1);
  h->on_glimpse_bytes(snap.data(), snap.size());
  EXPECT_EQ(h->mode(), Handler::Mode::Live);
  (*feed)({E(30, 1, 100), A(31, 6, 'S', 100, "1301", "DAY", 25015)});
  auto c = compare();
  EXPECT_EQ(c.mismatched_levels, 0u);
  EXPECT_EQ(c.bbo_mismatches, 0u);
  for (const auto &e : c.examples)
    ADD_FAILURE() << e;
  auto *ob = obs.ob(inst("1301").asset);
  EXPECT_EQ(check::bbbo(*ob), std::make_pair(24990, 25015));
  EXPECT_EQ(check::qat(*ob, false, 25010).qty, 0);   // order 2 is gone
  EXPECT_EQ(check::qat(*ob, false, 25020).qty, 300);
}

TEST_F(ObTest, EndOfSessionEmptiesTheBooks)
{
  directory();
  (*feed)({A(10, 1, 'B', 100, "1301", "DAY", 25000), A(11, 2, 'S', 200, "1301", "DAY", 25010)});
  auto eos = mold_packet(kSession, feed->seq, {}, 0xFFFF);
  feed->send(eos);
  auto *ob = obs.ob(inst("1301").asset);
  EXPECT_EQ(check::qat(*ob, true, 25000).qty, 0);
  EXPECT_EQ(check::qat(*ob, false, 25010).qty, 0);
}

// Before Q (status 21) a crossed book is the venue's state, not an error: OB,
// built to abort on a cross, does not.
TEST_F(ObTest, CrossedBeforeMarketHoursIsNotAnError)
{
  directory(27000, false);
  EXPECT_EQ(inst("1301").status_sent, 21);
  (*feed)({A(10, 1, 'B', 100, "1301", "DAY", 25010)});
  (*feed)({A(20, 2, 'S', 100, "1301", "DAY", 25000)});
  (*feed)({A(30, 3, 'S', 100, "1301", "DAY", 25020)});
  EXPECT_EQ(h->stats().crossed_other, 1u);
  EXPECT_EQ(h->stats().crossed_at_17, 0u);
  EXPECT_EQ(check::bbbo(*obs.ob(inst("1301").asset)), std::make_pair(25010, 25000));
}

// The actor: MoldUDP64 datagrams as the multicast reader delivers them
// (ProcessQ<uint64_t>), records to the OB its BookFor callback returns. Driven
// with fast_send, so no threads run.
TEST(FeedActor, RoutesToBooks)
{
  std::map<std::string, std::unique_ptr<frame::ob::act::OB>> books;
  act::FeedConfig c;
  c.name = "JNXA";
  c.handler = config(true, 0, Handler::Join::FromOne);
  c.handler.instruments.name_prefix = "JNXA:";
  act::JnxFeed feed(c, [&](const Instrument &i) -> actors::Actor * {
    if (i.sicc != "1301")
      return nullptr;   // books may be left out
    boost::property_tree::ptree pt;
    auto &ob = books[i.sicc];
    ob = std::make_unique<frame::ob::act::OB>(nullptr, true, nullptr, uint(i.asset), pt);
    return ob.get();
  }, nullptr);
  uint64_t seq = 1;
  auto deliver = [&](const std::vector<std::string> &msgs) {
    mcast_recv::msg::ProcessQ<uint64_t> q;
    const std::string p = mold_packet(kSession, seq, msgs);
    seq += msgs.size();
    std::memcpy(q.buf.message.data(), p.data(), p.size());
    q.buf.len = p.size();
    q.buf.recv_ts = kMidnight + seq;
    feed.fast_send(q, nullptr);
  };
  std::vector<std::string> d = {T(30000)};
  for (auto &l : table37())
    d.push_back(l);
  d.push_back(R("1301", "DAY", 37, 40000, 20000));
  d.push_back(R("7203", "DAY", 37, 40000, 20000));
  d.push_back(H(1, "1301", "DAY", 'T'));
  d.push_back(S(2, "DAY", 'Q'));
  deliver(d);
  deliver({A(10, 1, 'B', 100, "1301", "DAY", 25000), A(11, 2, 'S', 200, "1301", "DAY", 25010),
           A(12, 3, 'S', 200, "7203", "DAY", 25010)});
  deliver({E(20, 1, 30), U(21, 2, 4, 100, 25005)});
  ASSERT_EQ(books.size(), 1u);
  auto &ob = *books["1301"];
  EXPECT_EQ(check::bbbo(ob), std::make_pair(25000, 25005));
  EXPECT_EQ(check::qat(ob, true, 25000).qty, 70);
  EXPECT_EQ(check::qat(ob, false, 25005).qty, 100);
  EXPECT_EQ(feed.handler().stats().messages, 17u);
}
