/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for PCoord (Position Coordinator)
 *
 * These tests verify that PCoord:
 * - Tracks position correctly for BUY and SELL
 * - Handles thread-safe position access
 * - Tracks various breach/event counters
 * - Stores and retrieves last trade prices
 *
 * PCoord is a lightweight position tracker used by light22 actors
 * to coordinate position across multiple order management components.
 */

#include <gtest/gtest.h>
#include "light/qcoord.hpp"
#include "enum/e_names.hpp"

using namespace light;

class PCoordTest : public ::testing::Test {
protected:
  PCoord pcoord;

  void SetUp() override {
    // PCoord starts with position = 0
  }
};

/**
 * Test 1: InitialPositionIsZero
 *
 * SETUP:  Fresh PCoord instance
 * VERIFY: get_position() returns 0
 */
TEST_F(PCoordTest, InitialPositionIsZero) {
  EXPECT_EQ(pcoord.get_position(), 0)
      << "New PCoord should have zero position";
}

/**
 * Test 2: BuyIncreasesPosition
 *
 * INPUT:  add_position(BUY, 10)
 * STATE:  position changes: 0 → 10
 * VERIFY: get_position() returns 10
 */
TEST_F(PCoordTest, BuyIncreasesPosition) {
  pcoord.add_position(en::bs::BUY, 10);
  EXPECT_EQ(pcoord.get_position(), 10)
      << "BUY should increase position";
}

/**
 * Test 3: SellDecreasesPosition
 *
 * INPUT:  add_position(SEL, 5)
 * STATE:  position changes: 0 → -5
 * VERIFY: get_position() returns -5
 */
TEST_F(PCoordTest, SellDecreasesPosition) {
  pcoord.add_position(en::bs::SEL, 5);
  EXPECT_EQ(pcoord.get_position(), -5)
      << "SELL should decrease position (go short)";
}

/**
 * Test 4: MultipleBuysAccumulate
 *
 * INPUT:  add_position(BUY, 10), add_position(BUY, 5), add_position(BUY, 3)
 * STATE:  position: 0 → 10 → 15 → 18
 * VERIFY: get_position() returns 18
 */
TEST_F(PCoordTest, MultipleBuysAccumulate) {
  pcoord.add_position(en::bs::BUY, 10);
  pcoord.add_position(en::bs::BUY, 5);
  pcoord.add_position(en::bs::BUY, 3);
  EXPECT_EQ(pcoord.get_position(), 18)
      << "Multiple BUYs should accumulate";
}

/**
 * Test 5: BuyThenSellReducesPosition
 *
 * INPUT:  add_position(BUY, 10), add_position(SEL, 3)
 * STATE:  position: 0 → 10 → 7
 * VERIFY: get_position() returns 7
 */
TEST_F(PCoordTest, BuyThenSellReducesPosition) {
  pcoord.add_position(en::bs::BUY, 10);
  pcoord.add_position(en::bs::SEL, 3);
  EXPECT_EQ(pcoord.get_position(), 7)
      << "SELL after BUY should reduce position";
}

/**
 * Test 6: PositionCanCrossZero
 *
 * INPUT:  add_position(BUY, 5), add_position(SEL, 10)
 * STATE:  position: 0 → 5 → -5
 * VERIFY: get_position() returns -5 (flipped from long to short)
 */
TEST_F(PCoordTest, PositionCanCrossZero) {
  pcoord.add_position(en::bs::BUY, 5);
  pcoord.add_position(en::bs::SEL, 10);
  EXPECT_EQ(pcoord.get_position(), -5)
      << "Position should be able to flip from long to short";
}

/**
 * Test 7: ZeroSizeIsIgnored
 *
 * INPUT:  add_position(BUY, 0)
 * STATE:  position unchanged (still 0)
 * VERIFY: get_position() returns 0
 *
 * Note: PCoord logs error and returns early for sz < 1
 */
TEST_F(PCoordTest, ZeroSizeIsIgnored) {
  pcoord.add_position(en::bs::BUY, 0);
  EXPECT_EQ(pcoord.get_position(), 0)
      << "Zero size should be ignored";
}

/**
 * Test 8: NegativeSizeIsIgnored
 *
 * INPUT:  add_position(BUY, -5)
 * STATE:  position unchanged (still 0)
 * VERIFY: get_position() returns 0
 *
 * Note: PCoord logs error and returns early for sz < 1
 */
TEST_F(PCoordTest, NegativeSizeIsIgnored) {
  pcoord.add_position(en::bs::BUY, -5);
  EXPECT_EQ(pcoord.get_position(), 0)
      << "Negative size should be ignored";
}

/**
 * Test 9: LastTradePriceBuy
 *
 * INPUT:  set_last_trade_px(100, BUY)
 * VERIFY: get_last_trade_px(BUY) returns 100
 *         get_last_trade_px(SEL) returns 0 (unchanged)
 */
TEST_F(PCoordTest, LastTradePriceBuy) {
  pcoord.set_last_trade_px(100, en::bs::BUY);
  EXPECT_EQ(pcoord.get_last_trade_px(en::bs::BUY), 100)
      << "Last BUY trade price should be stored";
  EXPECT_EQ(pcoord.get_last_trade_px(en::bs::SEL), 0)
      << "SELL trade price should be unchanged";
}

/**
 * Test 10: LastTradePriceSell
 *
 * INPUT:  set_last_trade_px(95, SEL)
 * VERIFY: get_last_trade_px(SEL) returns 95
 *         get_last_trade_px(BUY) returns 0 (unchanged)
 */
TEST_F(PCoordTest, LastTradePriceSell) {
  pcoord.set_last_trade_px(95, en::bs::SEL);
  EXPECT_EQ(pcoord.get_last_trade_px(en::bs::SEL), 95)
      << "Last SELL trade price should be stored";
  EXPECT_EQ(pcoord.get_last_trade_px(en::bs::BUY), 0)
      << "BUY trade price should be unchanged";
}

/**
 * Test 11: LastTradePriceUpdates
 *
 * INPUT:  set_last_trade_px(100, BUY), set_last_trade_px(105, BUY)
 * VERIFY: get_last_trade_px(BUY) returns 105 (updated)
 */
TEST_F(PCoordTest, LastTradePriceUpdates) {
  pcoord.set_last_trade_px(100, en::bs::BUY);
  pcoord.set_last_trade_px(105, en::bs::BUY);
  EXPECT_EQ(pcoord.get_last_trade_px(en::bs::BUY), 105)
      << "Last trade price should be updated";
}

/**
 * Test 12: BreachCountersInitiallyZero
 *
 * VERIFY: All breach counters start at 0
 */
TEST_F(PCoordTest, BreachCountersInitiallyZero) {
  EXPECT_EQ(pcoord.hedge_pos_size_breach, 0);
  EXPECT_EQ(pcoord.directional_pos_breach, 0);
  EXPECT_EQ(pcoord.out_px_breach, 0);
  EXPECT_EQ(pcoord.exceeded_reset_dist, 0);
  EXPECT_EQ(pcoord.expectation_breach, 0);
  EXPECT_EQ(pcoord.zero_pos, 0);
  EXPECT_EQ(pcoord.canc_to_aggr, 0);
  EXPECT_EQ(pcoord.delay_skip, 0);
  EXPECT_EQ(pcoord.attached_order_id_match, 0);
}

/**
 * Test 13: IncrementHedgePosBreachCounter
 *
 * INPUT:  incr_hedge_pos_size_breach() called 3 times
 * VERIFY: hedge_pos_size_breach == 3
 */
TEST_F(PCoordTest, IncrementHedgePosBreachCounter) {
  pcoord.incr_hedge_pos_size_breach();
  pcoord.incr_hedge_pos_size_breach();
  pcoord.incr_hedge_pos_size_breach();
  EXPECT_EQ(pcoord.hedge_pos_size_breach, 3);
}

/**
 * Test 14: IncrementDirectionalPosBreachCounter
 *
 * INPUT:  incr_directional_pos_breach() called 2 times
 * VERIFY: directional_pos_breach == 2
 */
TEST_F(PCoordTest, IncrementDirectionalPosBreachCounter) {
  pcoord.incr_directional_pos_breach();
  pcoord.incr_directional_pos_breach();
  EXPECT_EQ(pcoord.directional_pos_breach, 2);
}

/**
 * Test 15: IncrementOutPxBreachCounter
 *
 * INPUT:  incr_out_px_breach()
 * VERIFY: out_px_breach == 1
 */
TEST_F(PCoordTest, IncrementOutPxBreachCounter) {
  pcoord.incr_out_px_breach();
  EXPECT_EQ(pcoord.out_px_breach, 1);
}

/**
 * Test 16: IncrementExceededResetDistCounter
 *
 * INPUT:  incr_exceeded_reset_dist()
 * VERIFY: exceeded_reset_dist == 1
 */
TEST_F(PCoordTest, IncrementExceededResetDistCounter) {
  pcoord.incr_exceeded_reset_dist();
  EXPECT_EQ(pcoord.exceeded_reset_dist, 1);
}

/**
 * Test 17: IncrementExpectationBreachCounter
 *
 * INPUT:  incr_expectation_breach()
 * VERIFY: expectation_breach == 1
 */
TEST_F(PCoordTest, IncrementExpectationBreachCounter) {
  pcoord.incr_expectation_breach();
  EXPECT_EQ(pcoord.expectation_breach, 1);
}

/**
 * Test 18: IncrementZeroPosCounter
 *
 * INPUT:  incr_zero_pos()
 * VERIFY: zero_pos == 1
 */
TEST_F(PCoordTest, IncrementZeroPosCounter) {
  pcoord.incr_zero_pos();
  EXPECT_EQ(pcoord.zero_pos, 1);
}

/**
 * Test 19: IncrementCancToAggrCounter
 *
 * INPUT:  incr_canc_to_aggr()
 * VERIFY: canc_to_aggr == 1
 */
TEST_F(PCoordTest, IncrementCancToAggrCounter) {
  pcoord.incr_canc_to_aggr();
  EXPECT_EQ(pcoord.canc_to_aggr, 1);
}

/**
 * Test 20: IncrementDelaySkipCounter
 *
 * INPUT:  incr_delay_skip()
 * VERIFY: delay_skip == 1
 */
TEST_F(PCoordTest, IncrementDelaySkipCounter) {
  pcoord.incr_delay_skip();
  EXPECT_EQ(pcoord.delay_skip, 1);
}

/**
 * Test 21: IncrementAttachedOrderIdMatchCounter
 *
 * INPUT:  incr_attached_order_id_match()
 * VERIFY: attached_order_id_match == 1
 */
TEST_F(PCoordTest, IncrementAttachedOrderIdMatchCounter) {
  pcoord.incr_attached_order_id_match();
  EXPECT_EQ(pcoord.attached_order_id_match, 1);
}

/**
 * Test 22: GetIncrsReturnsTuple
 *
 * SETUP:  Set various counters
 * VERIFY: get_incrs() returns correct tuple
 */
TEST_F(PCoordTest, GetIncrsReturnsTuple) {
  pcoord.incr_hedge_pos_size_breach();
  pcoord.incr_hedge_pos_size_breach();
  pcoord.incr_directional_pos_breach();
  pcoord.incr_out_px_breach();
  pcoord.incr_out_px_breach();
  pcoord.incr_out_px_breach();
  pcoord.incr_exceeded_reset_dist();
  pcoord.incr_expectation_breach();
  pcoord.incr_zero_pos();
  pcoord.incr_canc_to_aggr();
  pcoord.incr_canc_to_aggr();

  auto incrs = pcoord.get_incrs();
  EXPECT_EQ(std::get<0>(incrs), 2) << "hedge_pos_size_breach";
  EXPECT_EQ(std::get<1>(incrs), 1) << "directional_pos_breach";
  EXPECT_EQ(std::get<2>(incrs), 3) << "out_px_breach";
  EXPECT_EQ(std::get<3>(incrs), 1) << "exceeded_reset_dist";
  EXPECT_EQ(std::get<4>(incrs), 1) << "expectation_breach";
  EXPECT_EQ(std::get<5>(incrs), 1) << "zero_pos";
  EXPECT_EQ(std::get<6>(incrs), 2) << "canc_to_aggr";
}

/**
 * Test 23: GetNameReturnsCorrectIdentifier
 *
 * VERIFY: get_name() returns "pcoord"
 */
TEST_F(PCoordTest, GetNameReturnsCorrectIdentifier) {
  EXPECT_STREQ(pcoord.get_name(), "pcoord");
}

/**
 * Test 24: IncrementNoOutPxCounter
 *
 * INPUT:  incr_no_outpx()
 * VERIFY: no_out_px_ == 1
 */
TEST_F(PCoordTest, IncrementNoOutPxCounter) {
  pcoord.incr_no_outpx();
  EXPECT_EQ(pcoord.no_out_px_, 1);
}

/**
 * Test 25: IncrementFollowingCounter
 *
 * INPUT:  incr_following()
 * VERIFY: following == 1
 */
TEST_F(PCoordTest, IncrementFollowingCounter) {
  pcoord.incr_following();
  EXPECT_EQ(pcoord.following, 1);
}
