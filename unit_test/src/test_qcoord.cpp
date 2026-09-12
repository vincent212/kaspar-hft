/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for QCoord (Order Quantity Coordinator)
 *
 * These tests verify that QCoord:
 * - Adds orders at price levels
 * - Removes orders (partial and full)
 * - Tracks total size across all prices
 * - Tracks size at individual price levels
 * - Tracks cumulative size up to a price
 * - Maintains lowest/highest price bookkeeping
 * - Handles multiple market makers at same price
 * - Manages cancellation counter
 *
 * QCoord manages order placement and tracking for light22 actors.
 * It uses a nested map: price → market_maker_id → order_info
 */

#include <gtest/gtest.h>
#include "light/qcoord.hpp"
#include "enum/e_names.hpp"
#include <limits>

using namespace light;

class QCoordTest : public ::testing::Test {
protected:
  QCoord qcoord;

  void SetUp() override {
    // QCoord starts empty
  }
};

/**
 * Test 1: InitialStateIsEmpty
 *
 * SETUP:  Fresh QCoord instance
 * VERIFY: total_sz() returns 0
 *         lowest_price() returns INT_MIN
 *         highest_price() returns INT_MAX
 */
TEST_F(QCoordTest, InitialStateIsEmpty) {
  EXPECT_EQ(qcoord.total_sz(), 0)
      << "New QCoord should have zero total size";
  EXPECT_EQ(qcoord.lowest_price(), std::numeric_limits<int>::min())
      << "Empty QCoord should return INT_MIN for lowest_price";
  EXPECT_EQ(qcoord.highest_price(), std::numeric_limits<int>::max())
      << "Empty QCoord should return INT_MAX for highest_price";
}

/**
 * Test 2: AddSingleOrder
 *
 * INPUT:  add_order(px=100, sz=10, mmid=1, id=1001, mxsz=100)
 * VERIFY: total_sz() returns 10
 *         sz_at_px(100) returns 10
 *         lowest_price() returns 100
 *         highest_price() returns 100
 */
TEST_F(QCoordTest, AddSingleOrder) {
  qcoord.add_order(100, 10, 1, 1001, 100);

  EXPECT_EQ(qcoord.total_sz(), 10)
      << "Total size should be 10 after adding order";
  EXPECT_EQ(qcoord.sz_at_px(100), 10)
      << "Size at price 100 should be 10";
  EXPECT_EQ(qcoord.lowest_price(), 100);
  EXPECT_EQ(qcoord.highest_price(), 100);
}

/**
 * Test 3: AddMultipleOrdersDifferentPrices
 *
 * INPUT:  add_order(px=100, sz=10, mmid=1, id=1001, mxsz=100)
 *         add_order(px=101, sz=20, mmid=2, id=1002, mxsz=100)
 *         add_order(px=99, sz=15, mmid=3, id=1003, mxsz=100)
 * VERIFY: total_sz() returns 45
 *         sz_at_px(100) returns 10
 *         sz_at_px(101) returns 20
 *         sz_at_px(99) returns 15
 *         lowest_price() returns 99
 *         highest_price() returns 101
 */
TEST_F(QCoordTest, AddMultipleOrdersDifferentPrices) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  qcoord.add_order(101, 20, 2, 1002, 100);
  qcoord.add_order(99, 15, 3, 1003, 100);

  EXPECT_EQ(qcoord.total_sz(), 45);
  EXPECT_EQ(qcoord.sz_at_px(100), 10);
  EXPECT_EQ(qcoord.sz_at_px(101), 20);
  EXPECT_EQ(qcoord.sz_at_px(99), 15);
  EXPECT_EQ(qcoord.lowest_price(), 99);
  EXPECT_EQ(qcoord.highest_price(), 101);
}

/**
 * Test 4: AddMultipleOrdersSamePrice
 *
 * Different market makers can have orders at the same price
 *
 * INPUT:  add_order(px=100, sz=10, mmid=1, id=1001, mxsz=100)
 *         add_order(px=100, sz=20, mmid=2, id=1002, mxsz=100)
 * VERIFY: total_sz() returns 30
 *         sz_at_px(100) returns 30
 */
TEST_F(QCoordTest, AddMultipleOrdersSamePrice) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  qcoord.add_order(100, 20, 2, 1002, 100);

  EXPECT_EQ(qcoord.total_sz(), 30);
  EXPECT_EQ(qcoord.sz_at_px(100), 30);
}

/**
 * Test 5: RemoveOrderFully
 *
 * INPUT:  add_order(px=100, sz=10, mmid=1, id=1001, mxsz=100)
 *         remove_order(px=100, sz=10, mmid=1, id=1001)
 * VERIFY: total_sz() returns 0
 *         sz_at_px(100) returns 0
 */
TEST_F(QCoordTest, RemoveOrderFully) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  int remaining = qcoord.remove_order(100, 10, 1, 1001);

  EXPECT_EQ(remaining, 0) << "Fully removed order should return 0 remaining";
  EXPECT_EQ(qcoord.total_sz(), 0);
  EXPECT_EQ(qcoord.sz_at_px(100), 0);
}

/**
 * Test 6: RemoveOrderPartially
 *
 * INPUT:  add_order(px=100, sz=10, mmid=1, id=1001, mxsz=100)
 *         remove_order(px=100, sz=3, mmid=1, id=1001)
 * VERIFY: returns 7 (remaining size)
 *         total_sz() returns 7
 *         sz_at_px(100) returns 7
 */
TEST_F(QCoordTest, RemoveOrderPartially) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  int remaining = qcoord.remove_order(100, 3, 1, 1001);

  EXPECT_EQ(remaining, 7) << "Partial remove should return remaining size";
  EXPECT_EQ(qcoord.total_sz(), 7);
  EXPECT_EQ(qcoord.sz_at_px(100), 7);
}

/**
 * Test 7: RemoveOrderWithAllFlag
 *
 * INPUT:  add_order(px=100, sz=10, mmid=1, id=1001, mxsz=100)
 *         remove_order(px=100, sz=0, mmid=1, id=1001, all=true)
 * VERIFY: returns 0
 *         total_sz() returns 0
 *         Order fully removed regardless of sz parameter
 */
TEST_F(QCoordTest, RemoveOrderWithAllFlag) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  // Remove with all=true ignores the sz parameter
  int remaining = qcoord.remove_order(100, 0, 1, 1001, true);

  EXPECT_EQ(remaining, 0);
  EXPECT_EQ(qcoord.total_sz(), 0);
}

/**
 * Test 8: SzAtPxReturnsZeroForEmptyPrice
 *
 * VERIFY: sz_at_px(999) returns 0 for price with no orders
 */
TEST_F(QCoordTest, SzAtPxReturnsZeroForEmptyPrice) {
  EXPECT_EQ(qcoord.sz_at_px(999), 0)
      << "Empty price level should return 0";
}

/**
 * Test 9: LastInTracksMarketMaker
 *
 * INPUT:  add_order(px=100, sz=10, mmid=5, id=1001, mxsz=100)
 * VERIFY: last_in(100) returns 5
 *         last_in(101) returns -1 (no orders at that price)
 */
TEST_F(QCoordTest, LastInTracksMarketMaker) {
  qcoord.add_order(100, 10, 5, 1001, 100);

  EXPECT_EQ(qcoord.last_in(100), 5)
      << "last_in should return mmid of last order at price";
  EXPECT_EQ(qcoord.last_in(101), -1)
      << "last_in should return -1 for price with no orders";
}

/**
 * Test 10: CumulativeSizeBuySide
 *
 * For BUY side, cum_sz_upto_px returns sum of all orders >= px
 * Tests both cached (use_cache=true) and non-cached (use_cache=false) variants.
 */
TEST_F(QCoordTest, CumulativeSizeBuySide) {
  qcoord.add_order(98, 5, 1, 1001, 100);
  qcoord.add_order(99, 10, 2, 1002, 100);
  qcoord.add_order(100, 15, 3, 1003, 100);
  qcoord.add_order(101, 20, 4, 1004, 100);

  // Test with cache enabled (default)
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 100, true), 35)
      << "BUY cumulative (cached): orders at 100 and 101 (15+20=35)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 99, true), 45)
      << "BUY cumulative (cached): orders at 99, 100, 101 (10+15+20=45)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 102, true), 0)
      << "BUY cumulative (cached): no orders >= 102";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 98, true), 50)
      << "BUY cumulative (cached): all orders (5+10+15+20=50)";

  // Test with cache disabled - should produce same results
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 100, false), 35)
      << "BUY cumulative (non-cached): orders at 100 and 101 (15+20=35)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 99, false), 45)
      << "BUY cumulative (non-cached): orders at 99, 100, 101 (10+15+20=45)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 102, false), 0)
      << "BUY cumulative (non-cached): no orders >= 102";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::BUY, 98, false), 50)
      << "BUY cumulative (non-cached): all orders (5+10+15+20=50)";
}

/**
 * Test 11: CumulativeSizeSellSide
 *
 * For SEL side, cum_sz_upto_px returns sum of all orders <= px
 * Tests both cached (use_cache=true) and non-cached (use_cache=false) variants.
 */
TEST_F(QCoordTest, CumulativeSizeSellSide) {
  qcoord.add_order(98, 5, 1, 1001, 100);
  qcoord.add_order(99, 10, 2, 1002, 100);
  qcoord.add_order(100, 15, 3, 1003, 100);
  qcoord.add_order(101, 20, 4, 1004, 100);

  // Test with cache enabled (default)
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::SEL, 99, true), 15)
      << "SEL cumulative (cached): orders at 98 and 99 (5+10=15)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::SEL, 100, true), 30)
      << "SEL cumulative (cached): orders at 98, 99, 100 (5+10+15=30)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::SEL, 97, true), 0)
      << "SEL cumulative (cached): no orders <= 97";

  // Test with cache disabled - should produce same results
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::SEL, 99, false), 15)
      << "SEL cumulative (non-cached): orders at 98 and 99 (5+10=15)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::SEL, 100, false), 30)
      << "SEL cumulative (non-cached): orders at 98, 99, 100 (5+10+15=30)";
  EXPECT_EQ(qcoord.cum_sz_upto_px(en::bs::SEL, 97, false), 0)
      << "SEL cumulative (non-cached): no orders <= 97";
}

/**
 * Test 12: CancellationCounterIncrements
 *
 * INPUT:  incr_num_canc() called 3 times
 * VERIFY: num_canc == 3
 *         incr_num_canc() returns incremented value
 */
TEST_F(QCoordTest, CancellationCounterIncrements) {
  EXPECT_EQ(qcoord.incr_num_canc(), 1);
  EXPECT_EQ(qcoord.incr_num_canc(), 2);
  EXPECT_EQ(qcoord.incr_num_canc(), 3);
  EXPECT_EQ(qcoord.num_canc, 3);
}

/**
 * Test 13: CancellationCounterClears
 *
 * SETUP:  Increment counter to 5
 * INPUT:  clear_num_canc()
 * VERIFY: num_canc == 0
 */
TEST_F(QCoordTest, CancellationCounterClears) {
  qcoord.incr_num_canc();
  qcoord.incr_num_canc();
  qcoord.incr_num_canc();
  qcoord.incr_num_canc();
  qcoord.incr_num_canc();

  qcoord.clear_num_canc();
  EXPECT_EQ(qcoord.num_canc, 0);
}

/**
 * Test 14: GetNameReturnsCorrectIdentifier
 *
 * VERIFY: get_name() returns "qcoord"
 */
TEST_F(QCoordTest, GetNameReturnsCorrectIdentifier) {
  EXPECT_STREQ(qcoord.get_name(), "qcoord");
}

/**
 * Test 15: RemoveNonExistentMmidReturnsZero
 *
 * When QCOORDMULTIREMOVE is defined, removing a non-existent mmid
 * returns 0 instead of asserting.
 *
 * INPUT:  remove_order(px=100, sz=5, mmid=99, id=1, all=false)
 * VERIFY: returns 0 (no order to remove)
 */
TEST_F(QCoordTest, RemoveNonExistentMmidReturnsZero) {
  // mmid 99 has no orders
  int remaining = qcoord.remove_order(100, 5, 99, 1, false);
  EXPECT_EQ(remaining, 0);
}

/**
 * Test 16: LowestPriceUpdatesAfterRemoval
 *
 * INPUT:  add_order(px=100, ...), add_order(px=101, ...)
 *         remove_order(px=100, ..., all=true)
 * VERIFY: lowest_price() returns 101 after removing order at 100
 */
TEST_F(QCoordTest, LowestPriceUpdatesAfterRemoval) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  qcoord.add_order(101, 20, 2, 1002, 100);

  EXPECT_EQ(qcoord.lowest_price(), 100);

  qcoord.remove_order(100, 10, 1, 1001);

  EXPECT_EQ(qcoord.lowest_price(), 101)
      << "Lowest price should update after removal";
}

/**
 * Test 17: HighestPriceUpdatesAfterRemoval
 *
 * INPUT:  add_order(px=100, ...), add_order(px=101, ...)
 *         remove_order(px=101, ..., all=true)
 * VERIFY: highest_price() returns 100 after removing order at 101
 */
TEST_F(QCoordTest, HighestPriceUpdatesAfterRemoval) {
  qcoord.add_order(100, 10, 1, 1001, 100);
  qcoord.add_order(101, 20, 2, 1002, 100);

  EXPECT_EQ(qcoord.highest_price(), 101);

  qcoord.remove_order(101, 20, 2, 1002);

  EXPECT_EQ(qcoord.highest_price(), 100)
      << "Highest price should update after removal";
}

/**
 * Test 18: MultiplePartialFills
 *
 * Simulate multiple partial fills on the same order
 *
 * INPUT:  add_order(px=100, sz=100, mmid=1, id=1001, mxsz=100)
 *         remove_order(px=100, sz=30, mmid=1, id=1001)
 *         remove_order(px=100, sz=25, mmid=1, id=1001)
 *         remove_order(px=100, sz=45, mmid=1, id=1001)
 * VERIFY: After each removal, remaining size is correct
 *         Final removal returns 0, total_sz() = 0
 */
TEST_F(QCoordTest, MultiplePartialFills) {
  qcoord.add_order(100, 100, 1, 1001, 100);

  int remaining = qcoord.remove_order(100, 30, 1, 1001);
  EXPECT_EQ(remaining, 70);
  EXPECT_EQ(qcoord.total_sz(), 70);

  remaining = qcoord.remove_order(100, 25, 1, 1001);
  EXPECT_EQ(remaining, 45);
  EXPECT_EQ(qcoord.total_sz(), 45);

  remaining = qcoord.remove_order(100, 45, 1, 1001);
  EXPECT_EQ(remaining, 0);
  EXPECT_EQ(qcoord.total_sz(), 0);
}

/**
 * Test 19: TotalSizeAccuracyAfterMixedOperations
 *
 * Complex sequence of adds and removes
 *
 * INPUT:  add(100, 10), add(101, 20), add(102, 30)
 *         remove(100, 5), remove(101, 20, all=true)
 *         add(103, 15)
 * VERIFY: total_sz() = 10-5 + 0 + 30 + 15 = 50
 */
TEST_F(QCoordTest, TotalSizeAccuracyAfterMixedOperations) {
  qcoord.add_order(100, 10, 1, 1001, 100);  // total = 10
  qcoord.add_order(101, 20, 2, 1002, 100);  // total = 30
  qcoord.add_order(102, 30, 3, 1003, 100);  // total = 60

  qcoord.remove_order(100, 5, 1, 1001);     // total = 55
  qcoord.remove_order(101, 0, 2, 1002, true); // total = 35 (remove all 20)

  qcoord.add_order(103, 15, 4, 1004, 100);  // total = 50

  EXPECT_EQ(qcoord.total_sz(), 50);
  EXPECT_EQ(qcoord.sz_at_px(100), 5);
  EXPECT_EQ(qcoord.sz_at_px(101), 0);
  EXPECT_EQ(qcoord.sz_at_px(102), 30);
  EXPECT_EQ(qcoord.sz_at_px(103), 15);
}

/**
 * Test 20: CacheInvalidationOnModification
 *
 * Verifies that sz_at_px cache is invalidated after order modifications
 *
 * INPUT:  add_order, query sz_at_px, add another order at same price
 * VERIFY: sz_at_px returns updated value (cache was invalidated)
 */
TEST_F(QCoordTest, CacheInvalidationOnModification) {
  qcoord.add_order(100, 10, 1, 1001, 100);

  // First query should populate cache
  EXPECT_EQ(qcoord.sz_at_px(100), 10);

  // Add another order at same price (different mmid)
  qcoord.add_order(100, 15, 2, 1002, 100);

  // Cache should be invalidated, new query should return updated value
  EXPECT_EQ(qcoord.sz_at_px(100), 25)
      << "sz_at_px should return updated value after cache invalidation";
}
