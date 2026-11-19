/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LmaxMdSync: snapshots diffed into Mbp2Mbo, checked by rebuilding the book
// from the records (BookCheck) after every W.

#include <gtest/gtest.h>

#include <random>

#include "lmax_md/BookCheck.hpp"
#include "lmax_md/LmaxMdSync.hpp"

using namespace lmax::md;

namespace
{
  struct Rig
  {
    Instrument inst{"4001", "EUR/USD", 7001, 0.00001, 0.1};
    std::vector<bfile::l3_t> recs;
    BookCheck check{7001, 0.00001, 0.1};
    sim::Mbp2Mbo out{{7001, 'L', 0.00001, 0.1}, [this](const bfile::l3_t &r) {
                       recs.push_back(r);
                       check.on_record(r);
                     }};
    LmaxMdSync sync{inst, out, 20, [this](const bfile::l3_t &r) {
                      recs.push_back(r);
                      check.on_record(r);
                    }, 'L'};
    uint64_t tx = 1'000'000'000;
    int seq = 1;

    MdSnapshot snap(std::vector<std::pair<double, double>> bids, std::vector<std::pair<double, double>> asks,
                    std::vector<MdEntry> trades = {})
    {
      MdSnapshot s;
      s.security_id = "4001";
      s.seq = seq++;
      for (auto &[p, q] : bids)
        s.entries.push_back(MdEntry{'0', p, q, true, true, {}, {}});
      for (auto &[p, q] : asks)
        s.entries.push_back(MdEntry{'1', p, q, true, true, {}, {}});
      for (auto &t : trades)
        s.entries.push_back(t);
      return s;
    }
    void apply(const MdSnapshot &s)
    {
      tx += 1000;
      sync.on_snapshot(s, tx);
    }
    size_t mismatches(size_t depth = 20)
    {
      std::string why;
      size_t n = check.compare(sync.bids(), sync.asks(), depth, why);
      EXPECT_EQ(n, 0u) << why;
      return n;
    }
    template <class T>
    size_t count()
    {
      size_t n = 0;
      for (auto &r : recs)
        n += std::holds_alternative<T>(r);
      return n;
    }
  };

  MdEntry trade(double px, double sz, const char *time)
  {
    return MdEntry{'2', px, sz, true, true, "20261118", time};
  }
}

TEST(LmaxMdSync, FirstBookIsARecoverySnapshotWithStatus17)
{
  Rig r;
  r.apply(r.snap({{1.15231, 1.0}, {1.15230, 2.5}}, {{1.15233, 0.3}}));
  ASSERT_FALSE(r.recs.empty());
  auto *sst = std::get_if<bfile::l3_sst_t>(&r.recs[0]);
  ASSERT_NE(sst, nullptr);
  EXPECT_EQ(sst->tradingStatus, 17);
  size_t adds = 0;
  for (auto &x : r.recs)
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&x))
    {
      EXPECT_TRUE(m->recovery);
      EXPECT_EQ(m->securityID, 7001);
      ++adds;
    }
  EXPECT_EQ(adds, 10u + 25u + 3u); // 1.0/0.1, 2.5/0.1, 0.3/0.1 chunks
  EXPECT_TRUE(r.sync.synced());
  EXPECT_EQ(r.sync.stats().resyncs, 1u);
  r.mismatches();
  EXPECT_EQ(r.check.best(true), 115231);
  EXPECT_EQ(r.check.best(false), 115233);
}

TEST(LmaxMdSync, LaterBooksSendOnlyTheChange)
{
  Rig r;
  r.apply(r.snap({{1.15231, 1.0}, {1.15230, 2.0}}, {{1.15233, 1.0}}));
  r.recs.clear();
  r.apply(r.snap({{1.15231, 1.2}, {1.15230, 2.0}}, {{1.15233, 1.0}}));
  size_t adds = 0, dels = 0;
  for (auto &x : r.recs)
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&x))
    {
      EXPECT_FALSE(m->recovery);
      (m->orderUpdateAction == 0 ? adds : dels)++;
    }
  EXPECT_EQ(adds, 2u);
  EXPECT_EQ(dels, 0u);
  r.mismatches();
  r.apply(r.snap({{1.15230, 2.0}}, {{1.15233, 1.0}, {1.15234, 0.5}}));
  r.mismatches();
  EXPECT_EQ(r.check.best(true), 115230);
}

TEST(LmaxMdSync, RepeatedTradeIsAppliedOnce)
{
  Rig r;
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15233, 1.0}}, {trade(1.15232, 0.5, "09:59:00.000")}));
  EXPECT_EQ(r.sync.stats().trades_first_book, 1u);
  EXPECT_EQ(r.sync.stats().trades_applied, 0u);
  // A buy of 0.3 lifts the offer; the next W shows the offer at 0.7.
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15233, 0.7}}, {trade(1.15233, 0.3, "10:00:00.001")}));
  EXPECT_EQ(r.sync.stats().trades_applied, 1u);
  EXPECT_EQ(r.check.executions(), 3u);
  r.mismatches();
  // The same trade in the next W is not applied again.
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15233, 0.7}}, {trade(1.15233, 0.3, "10:00:00.001")}));
  EXPECT_EQ(r.sync.stats().trades_applied, 1u);
  EXPECT_EQ(r.sync.stats().trades_repeated, 1u);
  EXPECT_EQ(r.check.executions(), 3u);
  r.mismatches();
}

TEST(LmaxMdSync, TradeSideFromThePreviousBook)
{
  Rig r;
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15234, 1.0}}));
  // At the bid: a sell; executions come off the bid's front.
  r.apply(r.snap({{1.15231, 0.8}}, {{1.15234, 1.0}}, {trade(1.15231, 0.2, "10:00:01.000")}));
  EXPECT_EQ(r.sync.stats().trades_applied, 1u);
  EXPECT_EQ(r.check.executions(), 2u);
  r.mismatches();
  // Inside the spread: unclassified, not applied.
  r.apply(r.snap({{1.15231, 0.8}}, {{1.15234, 1.0}}, {trade(1.15232, 0.2, "10:00:02.000")}));
  EXPECT_EQ(r.sync.stats().trades_unclassified, 1u);
  EXPECT_EQ(r.check.executions(), 2u);
  r.mismatches();
}

TEST(LmaxMdSync, TradeThenRefilledLevelKeepsLmaxSize)
{
  Rig r;
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15234, 1.0}}));
  // Traded 0.5 at the offer and refilled to 1.0 in the same W.
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15234, 1.0}}, {trade(1.15234, 0.5, "10:00:01.000")}));
  EXPECT_EQ(r.check.executions(), 5u);
  r.mismatches();
}

TEST(LmaxMdSync, ResetResendsTheBookAsRecovery)
{
  Rig r;
  r.apply(r.snap({{1.15231, 1.0}}, {{1.15234, 1.0}}));
  r.sync.reset();
  r.recs.clear();
  r.apply(r.snap({{1.15229, 0.4}}, {{1.15235, 0.2}}, {trade(1.15234, 0.1, "10:00:05.000")}));
  EXPECT_EQ(r.sync.stats().resyncs, 2u);
  EXPECT_EQ(r.sync.stats().trades_first_book, 1u);
  ASSERT_FALSE(r.recs.empty());
  EXPECT_TRUE(std::holds_alternative<bfile::l3_chr_v2_t>(r.recs[0])); // book cleared first
  for (auto &x : r.recs)
    if (auto *m = std::get_if<bfile::l3_mbo_v2_t>(&x))
      EXPECT_TRUE(m->recovery || m->orderUpdateAction == 2);
  r.mismatches();
  EXPECT_EQ(r.check.best(true), 115229);
  EXPECT_EQ(r.count<bfile::l3_sst_t>(), 0u); // status is sent once
}

TEST(LmaxMdSync, CountsCrossedOneSidedOffGridAndForeign)
{
  Rig r;
  r.apply(r.snap({{1.15235, 1.0}}, {{1.15234, 1.0}}));
  EXPECT_EQ(r.sync.stats().crossed, 1u);
  r.apply(r.snap({}, {{1.15234, 1.0}}));
  EXPECT_EQ(r.sync.stats().one_sided, 1u);
  r.apply(r.snap({{1.152315, 1.0}, {1.15231, 1.0}}, {{1.15234, 1.0}}));
  EXPECT_EQ(r.sync.stats().bad_entries, 1u);
  r.mismatches();
  MdSnapshot other = r.snap({{1.0, 1.0}}, {{1.1, 1.0}});
  other.security_id = "4004";
  r.apply(other);
  EXPECT_EQ(r.sync.stats().wrong_instrument, 1u);
  r.mismatches();
}

TEST(LmaxMdSync, DepthWindowKeepsTheBest)
{
  Rig r;
  Instrument inst = r.inst;
  std::vector<bfile::l3_t> recs;
  BookCheck check{7001, 0.00001, 0.1};
  sim::Mbp2Mbo out{{7001, 'L', 0.00001, 0.1}, [&](const bfile::l3_t &x) { check.on_record(x); }};
  LmaxMdSync sync{inst, out, 5};
  MdSnapshot s = r.snap({}, {});
  for (int i = 0; i < 20; ++i)
  {
    s.entries.push_back(MdEntry{'0', 1.15200 - i * 0.00001, 0.1 * (i + 1), true, true, {}, {}});
    s.entries.push_back(MdEntry{'1', 1.15210 + i * 0.00001, 0.1 * (i + 1), true, true, {}, {}});
  }
  sync.on_snapshot(s, 1000);
  std::string why;
  EXPECT_EQ(check.compare(sync.bids(), sync.asks(), 5, why), 0u) << why;
  EXPECT_EQ(check.orders(), 2u * (1 + 2 + 3 + 4 + 5));
}

// Random books and trades: after every W the rebuilt book equals LMAX's
// best 20 levels, and is never crossed when LMAX's is not.
TEST(LmaxMdSync, RandomReplayStaysConsistent)
{
  Rig r;
  std::mt19937 rng(12345);
  int64_t mid = 115230;
  int trades = 0;
  for (int i = 0; i < 3000; ++i)
  {
    mid += int(rng() % 5) - 2;
    std::vector<std::pair<double, double>> b, a;
    const int spread = 1 + int(rng() % 3);
    for (int k = 0; k < 20; ++k)
    {
      if (rng() % 6 == 0)
        continue;
      b.emplace_back((mid - k) * 0.00001, 0.1 * (1 + rng() % 50));
      a.emplace_back((mid + spread + k) * 0.00001, 0.1 * (1 + rng() % 50));
    }
    std::vector<MdEntry> t;
    if (i > 0 && rng() % 3 == 0 && !r.sync.asks().empty())
    {
      char tm[32];
      snprintf(tm, sizeof tm, "10:%02d:%02d.%03d", (i / 60000) % 60, (i / 1000) % 60, i % 1000);
      t.push_back(trade(r.sync.asks().front().first, 0.1 * (1 + rng() % 5), tm));
      ++trades;
    }
    r.apply(r.snap(b, a, t));
    ASSERT_EQ(r.mismatches(), 0u) << "after W " << i;
    ASSERT_FALSE(r.check.crossed()) << "after W " << i;
  }
  EXPECT_EQ(r.sync.stats().trades_applied, uint64_t(trades));
  EXPECT_GT(r.check.executions(), 0u);
  EXPECT_EQ(r.check.errors(), 0u);
}
