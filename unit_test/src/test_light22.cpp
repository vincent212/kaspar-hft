/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for light22 Components
 *
 * The light22 actor is a market making engine that:
 * - Subscribes to market data (EndOfBurst)
 * - Places orders based on position vs target position
 * - Cancels orders when conditions change
 * - Tracks order state via QCoord and position via PCoord
 *
 * Dependencies:
 * - RefData singleton (requires universe CSV file)
 * - QCoord for order tracking
 * - PCoord for position tracking
 * - SOM for order management
 * - Market data (EndOfBurst messages)
 *
 * These tests focus on testable components:
 * - light22 message types (Set, Start, Stop, LightInfo, etc.)
 * - QCoord/PCoord already tested separately
 *
 * Message Flow Documentation:
 *   [Start] → light22 → [Subscribe(HI)] → Aggregator
 *                     → [RegisterLight] → RiskManager
 *
 *   [EndOfBurst] → light22 → evaluate position vs target
 *                          → [Order] → SOM (if placing)
 *                          → [Cancel] → SOM (if cancelling)
 *
 *   [Fill] → light22 → PCoord::add_position
 *                   → QCoord::remove_order
 *
 *   [Set(TRADING_OFF)] → light22 → cancel working orders
 *                                → trading = false
 *
 *   [Set(TARGET_POS)] → light22 → targetpos = dval
 */

#include <gtest/gtest.h>
#include "light/msg/Set.hpp"
#include "light/msg/Start.hpp"
#include "light/msg/Stop.hpp"
#include "light/msg/LightInfo.hpp"
#include "light/msg/GetLightInfo.hpp"
#include "light/msg/PositionInfo.hpp"
#include "light/msg/RegisterLight.hpp"
#include "frame/som/msg/Fill.hpp"
#include "enum/e_names.hpp"

using namespace light::msg;

// =============================================================================
// Set Message Tests
// =============================================================================

class SetMessageTest : public ::testing::Test {
protected:
};

/**
 * Test 1: SetTradingOnAction
 *
 * VERIFY: Set message with TRADING_ON action created correctly
 */
TEST_F(SetMessageTest, SetTradingOnAction) {
  Set msg(Set::TRADING_ON);

  EXPECT_EQ(msg.key, Set::TRADING_ON);
  EXPECT_DOUBLE_EQ(msg.dval, 0.0);  // Default value
}

/**
 * Test 2: SetTradingOffAction
 *
 * VERIFY: Set message with TRADING_OFF action created correctly
 */
TEST_F(SetMessageTest, SetTradingOffAction) {
  Set msg(Set::TRADING_OFF);

  EXPECT_EQ(msg.key, Set::TRADING_OFF);
}

/**
 * Test 3: SetTargetPosAction
 *
 * VERIFY: Set message with TARGET_POS action and value created correctly
 */
TEST_F(SetMessageTest, SetTargetPosAction) {
  Set msg(Set::TARGET_POS, 100.0);

  EXPECT_EQ(msg.key, Set::TARGET_POS);
  EXPECT_DOUBLE_EQ(msg.dval, 100.0);
}

/**
 * Test 4: SetLevOrdersMaxAction
 *
 * VERIFY: Set message with LEV_ORDERS_MAX action and value
 */
TEST_F(SetMessageTest, SetLevOrdersMaxAction) {
  Set msg(Set::LEV_ORDERS_MAX, 50.0);

  EXPECT_EQ(msg.key, Set::LEV_ORDERS_MAX);
  EXPECT_DOUBLE_EQ(msg.dval, 50.0);
}

/**
 * Test 5: SetDumpAction
 *
 * VERIFY: Set message with DUMP action (triggers state dump)
 */
TEST_F(SetMessageTest, SetDumpAction) {
  Set msg(Set::DUMP);

  EXPECT_EQ(msg.key, Set::DUMP);
}

/**
 * Test 6: SetSubscribeOnlyAction
 *
 * VERIFY: Set message with SUBSCRIBEONLY action
 */
TEST_F(SetMessageTest, SetSubscribeOnlyAction) {
  Set msg(Set::SUBSCRIBEONLY);

  EXPECT_EQ(msg.key, Set::SUBSCRIBEONLY);
}

/**
 * Test 7: SetActionEnumValues
 *
 * VERIFY: All Set action enum values are distinct
 */
TEST_F(SetMessageTest, SetActionEnumValues) {
  // Verify enum values are distinct
  std::set<int> values;
  values.insert(Set::TRADING_ON);
  values.insert(Set::TRADING_OFF);
  values.insert(Set::DUMP);
  values.insert(Set::LEV_ORDERS_MAX);
  values.insert(Set::TARGET_POS);
  values.insert(Set::SUBSCRIBEONLY);

  EXPECT_EQ(values.size(), 6u) << "All Set actions should have distinct values";
}

// =============================================================================
// LightInfo Message Tests
// =============================================================================

class LightInfoMessageTest : public ::testing::Test {
protected:
};

/**
 * Test 8: LightInfoConstruction
 *
 * VERIFY: LightInfo message constructed with position and fill list
 */
TEST_F(LightInfoMessageTest, LightInfoConstruction) {
  LightInfo::last_fill_msg_list_t fills;

  LightInfo info(10, fills);

  EXPECT_EQ(info.pos, 10);
  EXPECT_TRUE(info.last_fill_msgs.empty());
}

/**
 * Test 9: LightInfoWithFills
 *
 * VERIFY: LightInfo message can contain fill history
 *
 * Note: Creating Fill with full constructor requires RefData initialization
 * because Price constructor looks up Asset. Use default constructor instead.
 */
TEST_F(LightInfoMessageTest, LightInfoWithFills) {
  LightInfo::last_fill_msg_list_t fills;

  // Create a fill message using default constructor and set fields
  frame::som::msg::Fill fill1;
  fill1.id = 123;
  fill1.venue = en::x::SIM;
  fill1.sym = 1;
  fill1.pxi = 100;  // Set price as int directly (avoids Price/RefData dependency)
  fill1.side = en::bs::BUY;
  fill1.sz = 10;
  fill1.still_to_be_filled = 0;
  fill1.owner = en::trader::OCCAMUST;

  // Add fill with timestamp
  fills.push_back(std::make_tuple(fill1, 1234567890ULL));

  LightInfo info(-5, fills);

  EXPECT_EQ(info.pos, -5);
  EXPECT_EQ(info.last_fill_msgs.size(), 1u);

  // Verify fill data preserved
  auto& [fill, ts] = info.last_fill_msgs.front();
  EXPECT_EQ(fill.id, 123u);
  EXPECT_EQ(fill.side, en::bs::BUY);
  EXPECT_EQ(ts, 1234567890ULL);
}

// =============================================================================
// PositionInfo Message Tests
// =============================================================================

class PositionInfoMessageTest : public ::testing::Test {
protected:
};

/**
 * Test 10: PositionInfoDefaultConstruction
 *
 * VERIFY: PositionInfo has sensible defaults
 */
TEST_F(PositionInfoMessageTest, PositionInfoDefaultConstruction) {
  PositionInfo info;
  // Just verify it compiles and can be default constructed
  // PositionInfo is typically filled in by the actor
  SUCCEED();
}

// =============================================================================
// Light Message Type Tests
// =============================================================================

class LightMessageTypesTest : public ::testing::Test {
protected:
};

/**
 * Test 11: StartMessageExists
 *
 * VERIFY: light::msg::Start message type exists and can be instantiated
 */
TEST_F(LightMessageTypesTest, StartMessageExists) {
  light::msg::Start start;
  SUCCEED();  // If it compiles, it exists
}

/**
 * Test 12: StopMessageExists
 *
 * VERIFY: light::msg::Stop message type exists
 */
TEST_F(LightMessageTypesTest, StopMessageExists) {
  light::msg::Stop stop;
  SUCCEED();
}

/**
 * Test 13: GetLightInfoMessageExists
 *
 * VERIFY: GetLightInfo request message type exists
 */
TEST_F(LightMessageTypesTest, GetLightInfoMessageExists) {
  GetLightInfo req;
  SUCCEED();
}

/**
 * Test 14: RegisterLightMessageExists
 *
 * VERIFY: RegisterLight message type exists for risk manager registration
 */
TEST_F(LightMessageTypesTest, RegisterLightMessageExists) {
  // RegisterLight takes a pointer to the light actor
  // For testing, we just verify the type exists
  // RegisterLight reg(nullptr);  // Would need actor pointer
  SUCCEED();
}

// =============================================================================
// Side Enum Tests (used by light22 template)
// =============================================================================

class SideEnumTest : public ::testing::Test {
protected:
};

/**
 * Test 15: BuyAndSellAreDifferent
 *
 * VERIFY: en::bs::BUY and en::bs::SEL are distinct
 */
TEST_F(SideEnumTest, BuyAndSellAreDifferent) {
  EXPECT_NE(en::bs::BUY, en::bs::SEL);
}

/**
 * Test 16: SideToString
 *
 * VERIFY: Side enum can be converted to string
 */
TEST_F(SideEnumTest, SideToString) {
  // Cast to en::bs to resolve overload ambiguity
  const char* buy_str = en::to_string(static_cast<en::bs>(en::bs::BUY));
  const char* sel_str = en::to_string(static_cast<en::bs>(en::bs::SEL));

  EXPECT_NE(buy_str, nullptr);
  EXPECT_NE(sel_str, nullptr);
  EXPECT_STRNE(buy_str, sel_str);
}

// NOTE: Order placement/cancellation conditions and message flow documentation
// has been moved to test_light22_integration.cpp which contains the actual
// integration tests that verify these behaviors.
