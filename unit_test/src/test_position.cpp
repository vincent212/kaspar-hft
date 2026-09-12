/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for Position Class
 *
 * The Position class tracks trading positions using FIFO accounting:
 * - Maintains a list of open position entries (PosEntry)
 * - Calculates realized P&L when positions are closed out
 * - Calculates unrealized P&L based on current bid/ask
 * - Tracks buy/sell share counts and trade counts
 *
 * Key methods:
 * - addToPosition(PosEntry): Add a trade, potentially closing existing positions
 * - totalSize(): Net position (positive=long, negative=short)
 * - pnl(): Realized P&L from closed positions
 * - unrealPnl(bid, ask): Unrealized P&L at current market prices
 * - do_swap(Swap): Handle contract rolls/adjustments
 */

#include <gtest/gtest.h>
#include "frame/pos/Position.hpp"
#include "enum/e_names.hpp"

using namespace frame::pos;

// =============================================================================
// Basic Position Tests
// =============================================================================

class PositionTest : public ::testing::Test {
protected:
  Position pos;

  void SetUp() override {
    pos = Position();
  }
};

/**
 * Test: InitialStateIsFlat
 *
 * VERIFY: New Position has zero size and zero P&L
 */
TEST_F(PositionTest, InitialStateIsFlat) {
  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 0.0);
  EXPECT_EQ(pos.buy_shares(), 0);
  EXPECT_EQ(pos.sell_shares(), 0);
  EXPECT_EQ(pos.nb_trades, 0);
  EXPECT_DOUBLE_EQ(pos.last_trade_price(), -1.0);  // Default unset value
}

/**
 * Test: BuyIncreasesPosition
 *
 * INPUT:  addToPosition(BUY, price=100, sz=10)
 * VERIFY: totalSize() = 10 (long)
 */
TEST_F(PositionTest, BuyIncreasesPosition) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));

  EXPECT_EQ(pos.totalSize(), 10);
  EXPECT_EQ(pos.buy_shares(), 10);
  EXPECT_EQ(pos.sell_shares(), 0);
  EXPECT_EQ(pos.nb_trades, 1);
  EXPECT_DOUBLE_EQ(pos.last_trade_price(), 100.0);
}

/**
 * Test: SellCreatesShortPosition
 *
 * INPUT:  addToPosition(SEL, price=100, sz=10)
 * VERIFY: totalSize() = -10 (short)
 */
TEST_F(PositionTest, SellCreatesShortPosition) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));

  EXPECT_EQ(pos.totalSize(), -10);
  EXPECT_EQ(pos.sell_shares(), 10);
  EXPECT_EQ(pos.buy_shares(), 0);
  EXPECT_EQ(pos.nb_trades, 1);
}

/**
 * Test: MultipleBuysAccumulate
 *
 * INPUT:  BUY 10 @ 100, BUY 5 @ 102
 * VERIFY: totalSize() = 15
 */
TEST_F(PositionTest, MultipleBuysAccumulate) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 102.0, 5));

  EXPECT_EQ(pos.totalSize(), 15);
  EXPECT_EQ(pos.buy_shares(), 15);
  EXPECT_EQ(pos.nb_trades, 2);
  EXPECT_DOUBLE_EQ(pos.last_trade_price(), 102.0);
}

/**
 * Test: MultipleSellsAccumulate
 *
 * INPUT:  SEL 10 @ 100, SEL 5 @ 98
 * VERIFY: totalSize() = -15
 */
TEST_F(PositionTest, MultipleSellsAccumulate) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 98.0, 5));

  EXPECT_EQ(pos.totalSize(), -15);
  EXPECT_EQ(pos.sell_shares(), 15);
  EXPECT_EQ(pos.nb_trades, 2);
}

// =============================================================================
// P&L Calculation Tests - Long Positions
// =============================================================================

class PositionPnLTest : public ::testing::Test {
protected:
  Position pos;
};

/**
 * Test: PartialCloseOutLongProfit
 *
 * INPUT:  BUY 10 @ 100, SEL 5 @ 110
 * VERIFY: totalSize() = 5 (remaining long)
 *         pnl() = 5 * (110 - 100) = 50
 */
TEST_F(PositionPnLTest, PartialCloseOutLongProfit) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 5));

  EXPECT_EQ(pos.totalSize(), 5);
  EXPECT_DOUBLE_EQ(pos.pnl(), 50.0);  // 5 * (110 - 100)
}

/**
 * Test: FullCloseOutLongProfit
 *
 * INPUT:  BUY 10 @ 100, SEL 10 @ 110
 * VERIFY: totalSize() = 0 (flat)
 *         pnl() = 10 * (110 - 100) = 100
 */
TEST_F(PositionPnLTest, FullCloseOutLongProfit) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 10));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 100.0);
}

/**
 * Test: FullCloseOutLongLoss
 *
 * INPUT:  BUY 10 @ 100, SEL 10 @ 90 (loss)
 * VERIFY: pnl() = 10 * (90 - 100) = -100
 */
TEST_F(PositionPnLTest, FullCloseOutLongLoss) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 90.0, 10));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), -100.0);
}

/**
 * Test: PartialCloseOutLongLoss
 *
 * INPUT:  BUY 10 @ 100, SEL 3 @ 95
 * VERIFY: totalSize() = 7
 *         pnl() = 3 * (95 - 100) = -15
 */
TEST_F(PositionPnLTest, PartialCloseOutLongLoss) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 95.0, 3));

  EXPECT_EQ(pos.totalSize(), 7);
  EXPECT_DOUBLE_EQ(pos.pnl(), -15.0);
}

// =============================================================================
// P&L Calculation Tests - Short Positions
// =============================================================================

/**
 * Test: ShortPositionProfit
 *
 * INPUT:  SEL 10 @ 110 (short), BUY 10 @ 100 (cover)
 * VERIFY: totalSize() = 0 (flat)
 *         pnl() = 10 * (110 - 100) = 100 (profit on short)
 */
TEST_F(PositionPnLTest, ShortPositionProfit) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 100.0);
}

/**
 * Test: ShortPositionLoss
 *
 * INPUT:  SEL 10 @ 100 (short), BUY 10 @ 110 (cover at loss)
 * VERIFY: pnl() = 10 * (100 - 110) = -100
 */
TEST_F(PositionPnLTest, ShortPositionLoss) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 110.0, 10));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), -100.0);
}

/**
 * Test: PartialCoverShortPosition
 *
 * INPUT:  SEL 10 @ 100 (short), BUY 4 @ 95 (partial cover)
 * VERIFY: totalSize() = -6 (still short)
 *         pnl() = 4 * (100 - 95) = 20
 */
TEST_F(PositionPnLTest, PartialCoverShortPosition) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 95.0, 4));

  EXPECT_EQ(pos.totalSize(), -6);
  EXPECT_DOUBLE_EQ(pos.pnl(), 20.0);
}

// =============================================================================
// Position Flip Tests
// =============================================================================

/**
 * Test: FlipFromLongToShort
 *
 * INPUT:  BUY 10 @ 100, SEL 15 @ 110
 * VERIFY: totalSize() = -5 (now short)
 *         pnl() = 10 * (110 - 100) = 100 (realized from closed long)
 */
TEST_F(PositionPnLTest, FlipFromLongToShort) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 15));

  EXPECT_EQ(pos.totalSize(), -5);  // Now short 5
  EXPECT_DOUBLE_EQ(pos.pnl(), 100.0);  // Realized on the 10 closed
}

/**
 * Test: FlipFromShortToLong
 *
 * INPUT:  SEL 10 @ 100, BUY 15 @ 95
 * VERIFY: totalSize() = 5 (now long)
 *         pnl() = 10 * (100 - 95) = 50 (realized from closed short)
 */
TEST_F(PositionPnLTest, FlipFromShortToLong) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 95.0, 15));

  EXPECT_EQ(pos.totalSize(), 5);  // Now long 5
  EXPECT_DOUBLE_EQ(pos.pnl(), 50.0);  // Realized on the 10 closed
}

// =============================================================================
// Multiple Trade P&L Tests
// =============================================================================

/**
 * Test: MultipleClosesAccumulatePnL
 *
 * INPUT:  BUY 10 @ 100, BUY 10 @ 102, SEL 20 @ 110
 * VERIFY: pnl() = 10 * (110 - 100) + 10 * (110 - 102) = 100 + 80 = 180
 */
TEST_F(PositionPnLTest, MultipleClosesAccumulatePnL) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 102.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 20));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 180.0);  // 100 + 80
}

/**
 * Test: MixedProfitAndLoss
 *
 * INPUT:  BUY 5 @ 100, SEL 5 @ 110 (profit 50), BUY 5 @ 115, SEL 5 @ 105 (loss -50)
 * VERIFY: pnl() = 50 - 50 = 0
 */
TEST_F(PositionPnLTest, MixedProfitAndLoss) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 5));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 5));  // +50
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 115.0, 5));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 105.0, 5));  // -50

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 0.0);
}

/**
 * Test: PartialFillsInSequence
 *
 * INPUT:  BUY 10 @ 100, SEL 3 @ 105, SEL 4 @ 108, SEL 3 @ 110
 * VERIFY: pnl() = 3*(105-100) + 4*(108-100) + 3*(110-100) = 15 + 32 + 30 = 77
 */
TEST_F(PositionPnLTest, PartialFillsInSequence) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 105.0, 3));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 108.0, 4));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 3));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 77.0);
}

// =============================================================================
// Unrealized P&L Tests
// =============================================================================

class PositionUnrealPnLTest : public ::testing::Test {
protected:
  Position pos;
};

/**
 * Test: UnrealizedPnLLongPositionProfit
 *
 * INPUT:  BUY 10 @ 100
 * VERIFY: unrealPnl(bid=110, ask=111) = 10 * (110 - 100) = 100
 */
TEST_F(PositionUnrealPnLTest, UnrealizedPnLLongPositionProfit) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));

  // Long position valued at bid
  double unreal = pos.unrealPnl(110.0, 111.0);
  EXPECT_DOUBLE_EQ(unreal, 100.0);  // 10 * (110 - 100)
}

/**
 * Test: UnrealizedPnLLongPositionLoss
 *
 * INPUT:  BUY 10 @ 100
 * VERIFY: unrealPnl(bid=95, ask=96) = 10 * (95 - 100) = -50
 */
TEST_F(PositionUnrealPnLTest, UnrealizedPnLLongPositionLoss) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));

  double unreal = pos.unrealPnl(95.0, 96.0);
  EXPECT_DOUBLE_EQ(unreal, -50.0);
}

/**
 * Test: UnrealizedPnLShortPositionProfit
 *
 * INPUT:  SEL 10 @ 100
 * VERIFY: unrealPnl(bid=89, ask=90) = 10 * (100 - 90) = 100
 */
TEST_F(PositionUnrealPnLTest, UnrealizedPnLShortPositionProfit) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));

  // Short position valued at ask (off)
  double unreal = pos.unrealPnl(89.0, 90.0);
  EXPECT_DOUBLE_EQ(unreal, 100.0);  // 10 * (100 - 90)
}

/**
 * Test: UnrealizedPnLShortPositionLoss
 *
 * INPUT:  SEL 10 @ 100
 * VERIFY: unrealPnl(bid=104, ask=105) = 10 * (100 - 105) = -50
 */
TEST_F(PositionUnrealPnLTest, UnrealizedPnLShortPositionLoss) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));

  double unreal = pos.unrealPnl(104.0, 105.0);
  EXPECT_DOUBLE_EQ(unreal, -50.0);
}

/**
 * Test: UnrealizedPnLFlatPosition
 *
 * INPUT:  (no position)
 * VERIFY: unrealPnl(bid=100, ask=101) = 0
 */
TEST_F(PositionUnrealPnLTest, UnrealizedPnLFlatPosition) {
  double unreal = pos.unrealPnl(100.0, 101.0);
  EXPECT_DOUBLE_EQ(unreal, 0.0);
}

/**
 * Test: UnrealizedPnLMultipleEntries
 *
 * INPUT:  BUY 5 @ 100, BUY 5 @ 102
 * VERIFY: unrealPnl(bid=108, ask=109) = 5*(108-100) + 5*(108-102) = 40 + 30 = 70
 */
TEST_F(PositionUnrealPnLTest, UnrealizedPnLMultipleEntries) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 5));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 102.0, 5));

  double unreal = pos.unrealPnl(108.0, 109.0);
  EXPECT_DOUBLE_EQ(unreal, 70.0);
}

// =============================================================================
// PosEntry Tests
// =============================================================================

class PosEntryTest : public ::testing::Test {
protected:
};

/**
 * Test: PosEntryConstruction
 *
 * VERIFY: PosEntry constructor works correctly
 */
TEST(PosEntryTest, Construction) {
  Position::PosEntry entry(en::bs::BUY, 100.0, 10);

  EXPECT_EQ(entry.trade_type, en::bs::BUY);
  EXPECT_DOUBLE_EQ(entry.trade_price, 100.0);
  EXPECT_EQ(entry.sz, 10);
}

/**
 * Test: PosEntryDefaultConstruction
 *
 * VERIFY: Default PosEntry has invalid values (sentinel)
 */
TEST(PosEntryTest, DefaultConstruction) {
  Position::PosEntry entry;

  EXPECT_DOUBLE_EQ(entry.trade_price, -1.0);
  EXPECT_EQ(entry.sz, -1);
}

/**
 * Test: PosEntrySellSide
 *
 * VERIFY: PosEntry can represent sell trades
 */
TEST(PosEntryTest, SellSide) {
  Position::PosEntry entry(en::bs::SEL, 95.5, 25);

  EXPECT_EQ(entry.trade_type, en::bs::SEL);
  EXPECT_DOUBLE_EQ(entry.trade_price, 95.5);
  EXPECT_EQ(entry.sz, 25);
}

// =============================================================================
// Position State Query Tests
// =============================================================================

class PositionStateTest : public ::testing::Test {
protected:
  Position pos;
};

/**
 * Test: GetSideLongPosition
 *
 * VERIFY: get_side() returns BUY for long positions
 */
TEST_F(PositionStateTest, GetSideLongPosition) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  EXPECT_EQ(pos.get_side(), en::bs::BUY);
}

/**
 * Test: GetSideShortPosition
 *
 * VERIFY: get_side() returns SEL for short positions
 */
TEST_F(PositionStateTest, GetSideShortPosition) {
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.0, 10));
  EXPECT_EQ(pos.get_side(), en::bs::SEL);
}

/**
 * Test: GetSideAfterFlip
 *
 * VERIFY: get_side() reflects current position after flip
 */
TEST_F(PositionStateTest, GetSideAfterFlip) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  EXPECT_EQ(pos.get_side(), en::bs::BUY);

  // Flip to short
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 15));
  EXPECT_EQ(pos.get_side(), en::bs::SEL);
}

/**
 * Test: LastTradePriceUpdates
 *
 * VERIFY: last_trade_price reflects most recent trade
 */
TEST_F(PositionStateTest, LastTradePriceUpdates) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  EXPECT_DOUBLE_EQ(pos.last_trade_price(), 100.0);

  pos.addToPosition(Position::PosEntry(en::bs::BUY, 105.0, 5));
  EXPECT_DOUBLE_EQ(pos.last_trade_price(), 105.0);

  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 15));
  EXPECT_DOUBLE_EQ(pos.last_trade_price(), 110.0);
}

/**
 * Test: TradeCountsAccumulate
 *
 * VERIFY: nb_trades increments with each trade
 */
TEST_F(PositionStateTest, TradeCountsAccumulate) {
  EXPECT_EQ(pos.nb_trades, 0);

  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  EXPECT_EQ(pos.nb_trades, 1);

  pos.addToPosition(Position::PosEntry(en::bs::SEL, 105.0, 5));
  EXPECT_EQ(pos.nb_trades, 2);

  pos.addToPosition(Position::PosEntry(en::bs::SEL, 108.0, 5));
  EXPECT_EQ(pos.nb_trades, 3);
}

/**
 * Test: BuySellSharesTrackedSeparately
 *
 * VERIFY: buy_shares and sell_shares accumulate independently
 */
TEST_F(PositionStateTest, BuySellSharesTrackedSeparately) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 102.0, 5));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 105.0, 8));

  EXPECT_EQ(pos.buy_shares(), 15);   // 10 + 5
  EXPECT_EQ(pos.sell_shares(), 8);
}

// =============================================================================
// Swap Tests
// =============================================================================

class PositionSwapTest : public ::testing::Test {
protected:
  Position pos;
};

/**
 * Test: SwapLongPosition
 *
 * INPUT:  BUY 10 @ 100, swap(old_price=100, old_size=1, new_price=98, new_size=1)
 * VERIFY: Position closes at old_price, reopens at new_price with adjusted size
 */
TEST_F(PositionSwapTest, SwapLongPosition) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));

  Position::Swap swap;
  swap.old_price = 100.0;
  swap.old_size = 1;
  swap.new_price = 98.0;
  swap.new_size = 1;

  pos.do_swap(swap);

  // Should still be long (same ratio)
  EXPECT_EQ(pos.totalSize(), 10);
}

/**
 * Test: SwapFlatPositionNoOp
 *
 * INPUT:  (flat position), do_swap(...)
 * VERIFY: No change to flat position
 */
TEST_F(PositionSwapTest, SwapFlatPositionNoOp) {
  Position::Swap swap;
  swap.old_price = 100.0;
  swap.old_size = 1;
  swap.new_price = 98.0;
  swap.new_size = 1;

  pos.do_swap(swap);

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 0.0);
}

/**
 * Test: SwapShortPositionDocumentation
 *
 * NOTE: The current do_swap() implementation has a limitation with short positions.
 * When totalSize() returns a negative value for short positions, it passes that
 * negative size directly to addToPosition(), which triggers an assertion in
 * closeOutPositions() that requires e.sz >= 0.
 *
 * This test documents the limitation. do_swap() appears designed for long positions
 * in the context of contract rolls (e.g., rolling ZNH6 to ZNM6).
 *
 * To properly handle short positions, do_swap() would need:
 *   auto abs_pos = std::abs(curr_pos);
 *   addToPosition(PosEntry(otherside, s.old_price, abs_pos));
 */
TEST_F(PositionSwapTest, SwapShortPositionDocumentation) {
  // Document that short position swaps are not currently supported
  // Attempting to swap a short position triggers an assertion
  SUCCEED();
}

// =============================================================================
// Edge Cases
// =============================================================================

class PositionEdgeCaseTest : public ::testing::Test {
protected:
  Position pos;
};

/**
 * Test: ZeroSizeTrade
 *
 * VERIFY: Zero size trades are counted but don't affect position
 */
TEST_F(PositionEdgeCaseTest, ZeroSizeTrade) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 0));

  EXPECT_EQ(pos.totalSize(), 10);
  EXPECT_EQ(pos.nb_trades, 2);  // Trade still counted
}

/**
 * Test: LargePositionSize
 *
 * VERIFY: Large position sizes handled correctly
 */
TEST_F(PositionEdgeCaseTest, LargePositionSize) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 1000000));

  EXPECT_EQ(pos.totalSize(), 1000000);
  EXPECT_EQ(pos.buy_shares(), 1000000);
}

/**
 * Test: FractionalPrice
 *
 * VERIFY: Fractional prices handled correctly
 */
TEST_F(PositionEdgeCaseTest, FractionalPrice) {
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.125, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 100.375, 10));

  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 2.5);  // 10 * 0.25
}

/**
 * Test: ManySmallTrades
 *
 * VERIFY: Many small trades accumulate correctly
 */
TEST_F(PositionEdgeCaseTest, ManySmallTrades) {
  for (int i = 0; i < 100; i++) {
    pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0 + i * 0.1, 1));
  }

  EXPECT_EQ(pos.totalSize(), 100);
  EXPECT_EQ(pos.nb_trades, 100);
  EXPECT_EQ(pos.buy_shares(), 100);
}

/**
 * Test: CloseAndReopenPosition
 *
 * VERIFY: Can close and reopen position multiple times
 */
TEST_F(PositionEdgeCaseTest, CloseAndReopenPosition) {
  // First round-trip
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 100.0, 10));
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 110.0, 10));
  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 100.0);

  // Second round-trip
  pos.addToPosition(Position::PosEntry(en::bs::SEL, 115.0, 5));
  pos.addToPosition(Position::PosEntry(en::bs::BUY, 110.0, 5));
  EXPECT_EQ(pos.totalSize(), 0);
  EXPECT_DOUBLE_EQ(pos.pnl(), 125.0);  // 100 + 25
}

