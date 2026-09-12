/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for SOM (Simple Order Manager) Components
 *
 * The SOM actor has significant dependencies on:
 * - RefData singleton (requires universe CSV file)
 * - Order books (simulated or real)
 * - Position limits configuration
 *
 * These tests focus on testable components:
 * - SOM message types (serialization/deserialization)
 *
 * For full SOM integration tests, a RefData universe file and
 * mock order books would need to be configured.
 *
 * Message Flow Documentation:
 *   [Order] → SOM → [Ack] → client
 *                 → [mda::Data] → OB (sim_mode)
 *   [Fill] → SOM → [Fill] → client
 *                → [Fill] → subscribers (FillSub)
 *   [Cancel] → SOM → [mda::Data(CANCD)] → OB
 *                  → [CancAck] → client
 *   [Stop] → SOM → cancel_all_orders()
 *   [Start] → SOM → trading enabled
 *
 * Note: Position class tests are in test_position.cpp
 */

#include <gtest/gtest.h>
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "enum/e_names.hpp"

using namespace frame::som::msg;

// =============================================================================
// SOM Message Serialization Tests
// =============================================================================

class SOMMessageTest : public ::testing::Test {
protected:
};

/**
 * Test 13: OrderSerialization
 *
 * VERIFY: Order can be serialized to JSON and deserialized back
 */
TEST_F(SOMMessageTest, OrderSerialization) {
  Order orig;
  orig.venue = en::x::SIM;
  orig.sym = 1;
  orig.sz = 100;
  orig.px = 1000;
  orig.side = en::bs::BUY;
  orig.oid = 12345;
  orig.cusip = "CUSIP123";

  // Serialize to JSON
  std::string json = orig.to_json();
  EXPECT_FALSE(json.empty());

  // Deserialize back
  Order restored = Order::from_json(json);

  EXPECT_EQ(restored.venue, en::x::SIM);
  EXPECT_EQ(restored.sym, 1);
  EXPECT_DOUBLE_EQ(restored.sz, 100.0);
  EXPECT_EQ(restored.px, 1000);
  EXPECT_EQ(restored.side, en::bs::BUY);
  EXPECT_EQ(restored.oid, 12345);
  EXPECT_EQ(restored.cusip, "CUSIP123");
}

/**
 * Test 14: CancelSerialization
 *
 * VERIFY: Cancel can be serialized to JSON and deserialized back
 */
TEST_F(SOMMessageTest, CancelSerialization) {
  Cancel orig(12345);

  std::string json = orig.to_json();
  EXPECT_FALSE(json.empty());

  Cancel restored = Cancel::from_json(json);
  EXPECT_EQ(restored.id, 12345u);
}

/**
 * Test 15: AckSerialization
 *
 * VERIFY: Ack can be serialized to JSON and deserialized back
 */
TEST_F(SOMMessageTest, AckSerialization) {
  Ack orig(12345, 67890);

  std::string json = orig.to_json();
  EXPECT_FALSE(json.empty());

  Ack restored = Ack::from_json(json);
  EXPECT_EQ(restored.id, 12345u);
  EXPECT_EQ(restored.xordid, 67890u);
}

/**
 * Test 16: RejectSerialization
 *
 * VERIFY: Reject can be serialized to JSON and deserialized back
 */
TEST_F(SOMMessageTest, RejectSerialization) {
  Reject orig(12345, Reject::OVERLIMIT);

  std::string json = orig.to_json();
  EXPECT_FALSE(json.empty());

  Reject restored = Reject::from_json(json);
  EXPECT_EQ(restored.id, 12345u);
  EXPECT_EQ(restored.reason, Reject::OVERLIMIT);
}

/**
 * Test 17: CancRejectReasons
 *
 * VERIFY: CancReject reason codes are correctly defined
 */
TEST_F(SOMMessageTest, CancRejectReasons) {
  // Verify all reason codes are distinct
  EXPECT_NE(CancReject::NOTFOUND, CancReject::FILLEDALREADY);
  EXPECT_NE(CancReject::FILLEDALREADY, CancReject::CANCELLEDALREADY);
  EXPECT_NE(CancReject::CANCELLEDALREADY, CancReject::REJECTEDPREVIOUSLY);
  EXPECT_NE(CancReject::REJECTEDPREVIOUSLY, CancReject::NOTACKED);

  // Verify specific values for documentation
  EXPECT_EQ(CancReject::NOTFOUND, 0);
  EXPECT_EQ(CancReject::FILLEDALREADY, 1);
  EXPECT_EQ(CancReject::CANCELLEDALREADY, 2);
}

/**
 * Test 18: RejectReasons
 *
 * VERIFY: Reject reason codes are correctly defined
 */
TEST_F(SOMMessageTest, RejectReasons) {
  EXPECT_EQ(Reject::UNKNOWN, 0);
  EXPECT_EQ(Reject::OVERLIMIT, 1);
  EXPECT_EQ(Reject::THROTTLE, 2);
  EXPECT_EQ(Reject::EXCHLIMIT, 3);
  EXPECT_EQ(Reject::DISCONNECTED, 4);
  EXPECT_EQ(Reject::NOTLOGGEDIN, 5);
}

/**
 * Test 19: OrderDefaultValues
 *
 * VERIFY: Order has sensible defaults
 */
TEST_F(SOMMessageTest, OrderDefaultValues) {
  Order o;

  EXPECT_EQ(o.oid, -1);  // Not assigned yet
  EXPECT_FALSE(o.filled);
  EXPECT_FALSE(o.canced);
  EXPECT_FALSE(o.rejed);
  EXPECT_FALSE(o.acked);
  EXPECT_FALSE(o.unacked_slated_for_canc);
  EXPECT_EQ(o.still_to_be_filled, 0);
}

/**
 * Test 20: OrderConstructorSetsFields
 *
 * VERIFY: Order constructor properly initializes all fields
 */
TEST_F(SOMMessageTest, OrderConstructorSetsFields) {
  Order o("PREFIX", en::x::SIM, 1, 100.0, 1000, en::bs::BUY, 0);

  EXPECT_EQ(o.prefix, "PREFIX");
  EXPECT_EQ(o.venue, en::x::SIM);
  EXPECT_EQ(o.sym, 1);
  EXPECT_DOUBLE_EQ(o.sz, 100.0);
  EXPECT_EQ(o.px, 1000);
  EXPECT_EQ(o.side, en::bs::BUY);
  EXPECT_EQ(o.owner, 0);
  EXPECT_EQ(o.still_to_be_filled, 100);  // Set from sz
}
