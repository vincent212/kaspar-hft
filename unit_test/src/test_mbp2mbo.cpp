/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Mbp2Mbo -- price levels in, per-order records out.
 *
 * What OB sees is what these tests pin: the records Mbp2Mbo emits, in order.
 * Chunk 0.01 and tick 0.01 throughout, so a level of 0.03 is three orders and
 * a price of 100.05 is tick 10005.
 */

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace {

constexpr uint64_t kT0 = 1791811800000000000ULL;

class Mbp2MboTest : public ::testing::Test {
protected:
  std::vector<bfile::l3_t> out;
  sim::Mbp2Mbo m{{/*sec_id*/ 7, /*venue*/ char(en::x::BINANCE), /*tick*/ 0.01, /*chunk*/ 0.01},
                 [this](const bfile::l3_t &r) { out.push_back(r); }};

  // Records of the given type, in emission order.
  template <class T> std::vector<T> all() const {
    std::vector<T> v;
    for (const auto &r : out)
      if (auto *p = std::get_if<T>(&r)) v.push_back(*p);
    return v;
  }
  std::vector<bfile::l3_mbo_v2_t> mbo() const { return all<bfile::l3_mbo_v2_t>(); }

  std::vector<uint64_t> q(bool bid, double px) const {
    auto *d = m.queue(bid, m.ticks(px));
    return d ? std::vector<uint64_t>(d->begin(), d->end()) : std::vector<uint64_t>{};
  }

  void level(bool bid, double px, double qty, uint64_t tx = kT0) {
    m.level(bid, px, qty, tx);
    m.end_event();
  }
};

TEST_F(Mbp2MboTest, ALevelIsCeilOfQuantityOverChunk) {
  level(false, 100.05, 0.025);
  EXPECT_EQ(m.chunks(false, 10005), 3u);
  level(false, 100.06, 0.03);          // 0.03 / 0.01 is 2.9999999999999996 in binary
  EXPECT_EQ(m.chunks(false, 10006), 3u);
  level(false, 100.07, 0.0001);
  EXPECT_EQ(m.chunks(false, 10007), 1u) << "any quantity at all is one chunk";
}

TEST_F(Mbp2MboTest, AddsCarryTicksOneLotTheSecurityAndTheSide) {
  level(true, 100.05, 0.01);
  auto r = mbo();
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0].orderUpdateAction, 0);
  EXPECT_DOUBLE_EQ(r[0].pxd, 10005.0) << "prices go out in ticks";
  EXPECT_EQ(r[0].displayQty, 1u);
  EXPECT_EQ(r[0].securityID, 7);
  EXPECT_EQ(r[0].side, '0');
  EXPECT_EQ(r[0].venue, char(en::x::BINANCE));
}

TEST_F(Mbp2MboTest, AGrowingLevelAddsAtTheBack) {
  level(true, 100, 0.02);
  const auto before = q(true, 100);
  out.clear();
  level(true, 100, 0.04);

  auto after = q(true, 100);
  ASSERT_EQ(after.size(), 4u);
  EXPECT_EQ(after[0], before[0]);
  EXPECT_EQ(after[1], before[1]) << "the resting chunks keep their place";
  auto r = mbo();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].orderID, after[2]);
  EXPECT_EQ(r[1].orderID, after[3]);
}

TEST_F(Mbp2MboTest, AShrinkingLevelCancelsFromTheBack) {
  level(true, 100, 0.04);
  const auto full = q(true, 100);
  out.clear();
  level(true, 100, 0.02);

  EXPECT_EQ(q(true, 100), std::vector<uint64_t>(full.begin(), full.begin() + 2));
  auto r = mbo();
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].orderUpdateAction, 2);
  EXPECT_EQ(r[0].orderID, full[3]) << "newest first";
  EXPECT_EQ(r[1].orderID, full[2]);
}

TEST_F(Mbp2MboTest, QuantityZeroRemovesTheLevel) {
  level(false, 101, 0.03);
  out.clear();
  level(false, 101, 0);
  EXPECT_EQ(m.queue(false, 10100), nullptr);
  EXPECT_EQ(mbo().size(), 3u);
  EXPECT_EQ(m.best(false), 0);
}

TEST_F(Mbp2MboTest, ATradeExecutesFromTheFrontAsExecThenDelete) {
  level(false, 101, 0.03);
  const auto full = q(false, 101);
  out.clear();

  m.trade(101, 0.02, /*buyer_is_maker*/ false, kT0 + 5);   // a buyer lifted the offer
  m.end_event();

  ASSERT_GE(out.size(), 4u);
  auto x0 = std::get_if<bfile::l3_mbo_trd_v2_t>(&out[0]);
  auto d0 = std::get_if<bfile::l3_mbo_v2_t>(&out[1]);
  auto x1 = std::get_if<bfile::l3_mbo_trd_v2_t>(&out[2]);
  auto d1 = std::get_if<bfile::l3_mbo_v2_t>(&out[3]);
  ASSERT_TRUE(x0 && d0 && x1 && d1);
  EXPECT_EQ(x0->orderID, full[0]) << "the oldest chunk trades first";
  EXPECT_EQ(x0->lastQty, 1);
  EXPECT_EQ(d0->orderID, full[0]);
  EXPECT_EQ(d0->orderUpdateAction, 2);
  EXPECT_EQ(d0->transactTime, x0->transactTime) << "exec and delete are one transaction";
  EXPECT_EQ(x1->orderID, full[1]);
  EXPECT_EQ(q(false, 101), std::vector<uint64_t>{full[2]});
}

TEST_F(Mbp2MboTest, BuyerIsMakerMeansTheBidWasHit) {
  level(true, 100, 0.01);
  level(false, 101, 0.01);
  out.clear();
  m.trade(100, 0.01, /*buyer_is_maker*/ true, kT0 + 5);
  m.end_event();
  EXPECT_EQ(m.chunks(true, 10000), 0u);
  EXPECT_EQ(m.chunks(false, 10100), 1u);
}

TEST_F(Mbp2MboTest, ADepthUpdateAfterATradeDoesNotCountItTwice) {
  level(false, 101, 0.03);
  m.trade(101, 0.02, false, kT0 + 5);
  m.end_event();
  out.clear();

  level(false, 101, 0.01, kT0 + 10);   // Binance's update reflecting that trade

  EXPECT_TRUE(mbo().empty()) << "the level already has the one chunk it should";
  EXPECT_EQ(m.chunks(false, 10100), 1u);
}

// The depth update that reflects a trade can arrive before the trade (separate
// streams). The update has already shrunk the level, from the back; the late
// trade must still execute from the front without shrinking it again.
TEST_F(Mbp2MboTest, ATradeAlreadyInTheDepthUpdateDoesNotShrinkTheLevelAgain) {
  level(false, 101, 0.10, kT0);
  const auto full = q(false, 101);
  level(false, 101, 0.07, kT0 + 100);    // reflects a 3-chunk trade
  out.clear();

  m.trade(101, 0.03, false, kT0 + 90);    // that trade, arriving late
  m.end_event();

  EXPECT_EQ(m.chunks(false, 10100), 7u) << "Binance's size, not 4";
  auto x = all<bfile::l3_mbo_trd_v2_t>();
  ASSERT_EQ(x.size(), 3u);
  EXPECT_EQ(x[0].orderID, full[0]) << "executions still come from the front";
  EXPECT_EQ(x[2].orderID, full[2]);
}

TEST_F(Mbp2MboTest, FractionalTradesAccumulateToAChunk) {
  level(false, 101, 0.05);
  out.clear();
  for (int i = 0; i < 2; ++i) { m.trade(101, 0.004, false, kT0 + 5 + i); m.end_event(); }
  EXPECT_TRUE(all<bfile::l3_mbo_trd_v2_t>().empty()) << "0.008 is less than a chunk";

  m.trade(101, 0.004, false, kT0 + 10);   // 0.012
  m.end_event();
  EXPECT_EQ(all<bfile::l3_mbo_trd_v2_t>().size(), 1u);

  out.clear();
  m.trade(101, 0.008, false, kT0 + 11);   // 0.002 left over + 0.008 = 0.010
  m.end_event();
  EXPECT_EQ(all<bfile::l3_mbo_trd_v2_t>().size(), 1u) << "the remainder carries";
}

TEST_F(Mbp2MboTest, ATradeWithNoLevelEmitsNothing) {
  level(false, 101, 0.01);
  out.clear();
  m.trade(102, 0.05, false, kT0 + 5);
  m.end_event();
  EXPECT_TRUE(out.empty());
}

TEST_F(Mbp2MboTest, EveryEventGetsItsOwnIncreasingTransactTime) {
  level(false, 101, 0.05);
  m.trade(101, 0.01, false, kT0 + 5);
  m.end_event();
  m.trade(101, 0.01, false, kT0 + 5);   // same millisecond on the wire
  m.end_event();
  m.trade(101, 0.01, false, kT0 + 1);   // stamped in the past
  m.end_event();

  auto x = all<bfile::l3_mbo_trd_v2_t>();
  ASSERT_EQ(x.size(), 3u);
  EXPECT_LT(x[0].transactTime, x[1].transactTime);
  EXPECT_LT(x[1].transactTime, x[2].transactTime);
}

TEST_F(Mbp2MboTest, AnEventEndsWithEndOfEventAndAnEob) {
  m.level(true, 100, 0.01, kT0);
  m.level(false, 101, 0.02, kT0);
  m.end_event();

  ASSERT_EQ(out.size(), 4u);
  auto r = mbo();
  EXPECT_FALSE(r[0].endOfEvent);
  EXPECT_FALSE(r[1].endOfEvent);
  EXPECT_TRUE(r[2].endOfEvent) << "the last record of the event";
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eob_t>(out.back()));
  EXPECT_EQ(r[0].transactTime, r[2].transactTime) << "one event, one transaction";
}

TEST_F(Mbp2MboTest, StartSaysTheMarketIsMatching) {
  m.start(kT0);
  ASSERT_EQ(out.size(), 2u);
  auto s = std::get_if<bfile::l3_sst_t>(&out[0]);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->tradingStatus, 17);
  EXPECT_EQ(s->securityID, 7);
}

TEST_F(Mbp2MboTest, ASnapshotReplacesTheBookWithRecoveryAdds) {
  level(true, 99, 0.05);
  out.clear();
  m.snapshot({{100, 0.02}}, {{101, 0.01}}, kT0 + 100);
  m.end_event();

  EXPECT_EQ(m.queue(true, 9900), nullptr) << "the old book is gone";
  auto r = mbo();
  ASSERT_EQ(r.size(), 3u);
  for (const auto &x : r) {
    EXPECT_EQ(x.orderUpdateAction, 0);
    EXPECT_TRUE(x.recovery) << "OB clears its book on the first recovery record";
  }
  EXPECT_EQ(m.best(true), 10000);
  EXPECT_EQ(m.best(false), 10100);
}

}  // namespace
