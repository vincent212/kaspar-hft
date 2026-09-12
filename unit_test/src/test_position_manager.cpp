/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for PositionManager Actor
 *
 * Legacy tests (BuyIncreasesPosition, SellDecreasesPosition, ...)
 * verify the outright AddToPos → PCoord path.  Spread-pulse tests
 * (Chunks*, LockStep*, Netting*, MissingPcoord*, SpreadAdd*, ...)
 * cover positionman/docs/spread_pulse_plan.md §§1-9 and its Test
 * Plan items 1-17.
 *
 * Message Flow:
 *   [AddToPos]        → PositionManager → PCoord::add_position()
 *   [GetPos]          → PositionManager → reply([Pos])
 *   [Get(spread_add)] → PositionManager → (validates + dispatches)
 *   [AddSpreadOrder]  → PositionManager → (net + maybe_fire + tick timer)
 *   [Timeout]         → PositionManager → fires next chunk on all pulses
 */

#include <gtest/gtest.h>
#include "unit_test/MockActor.hpp"
#include "unit_test/MockPCoord.hpp"
#include "unit_test/TestHelper.hpp"
#include "positionman/act/PositionManager.hpp"
#include "positionman/msg/AddToPos.hpp"
#include "positionman/msg/GetPos.hpp"
#include "positionman/msg/Pos.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Timeout.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"

using namespace unit_test;
using namespace positionman;
using namespace positionman::msg;

// TestablePM, invoke_with_reply_to and simulate_fill_to_zero from the upstream
// file are not ported: every one of them exists to drive the spread pulse,
// which this repo's PositionManager does not have.

class PositionManagerTest : public ::testing::Test {
protected:
  MockPCoord mock_pcoord_zn;

  std::unique_ptr<PositionManager> create_position_manager(
      const std::map<std::string, light::PCoord*>& pcoords = {}) {
    return std::make_unique<PositionManager>("test", pcoords);
  }

  void process_msg(actors::Actor* actor, const actors::Message* msg,
                   actors::Actor* sender = nullptr) {
    TestHelper::invoke_handler(actor, msg, sender);
  }

  void SetUp() override {
    mock_pcoord_zn.clear();
  }
};

// =====================================================================
// Legacy tests — outright add/get path.  Also verifies Test-Plan #17
// (BuyIncreasesPosition doesn't UAF, thanks to §5's guarded arm).
// =====================================================================

TEST_F(PositionManagerTest, BuyIncreasesPosition) {
  std::map<std::string, light::PCoord*> pcoords = {{"ZNH6", &mock_pcoord_zn}};
  auto pm = create_position_manager(pcoords);

  actors::msg::Start start_msg;
  process_msg(pm.get(), &start_msg, nullptr);

  AddToPos add_msg("ZNH6", en::bs::BUY, 10);
  process_msg(pm.get(), &add_msg, nullptr);

  EXPECT_EQ(pm->get_pos("ZNH6"), 10);
  EXPECT_EQ(mock_pcoord_zn.get_position(), 10);
}

TEST_F(PositionManagerTest, SellDecreasesPosition) {
  std::map<std::string, light::PCoord*> pcoords = {{"ZNH6", &mock_pcoord_zn}};
  auto pm = create_position_manager(pcoords);

  AddToPos buy_msg("ZNH6", en::bs::BUY, 10);
  process_msg(pm.get(), &buy_msg, nullptr);

  AddToPos sell_msg("ZNH6", en::bs::SEL, 3);
  process_msg(pm.get(), &sell_msg, nullptr);

  EXPECT_EQ(pm->get_pos("ZNH6"), 7);
  EXPECT_EQ(mock_pcoord_zn.get_position(), 7);
}

TEST_F(PositionManagerTest, PositionCanGoNegative) {
  std::map<std::string, light::PCoord*> pcoords = {{"ZNH6", &mock_pcoord_zn}};
  auto pm = create_position_manager(pcoords);

  AddToPos sell_msg("ZNH6", en::bs::SEL, 5);
  process_msg(pm.get(), &sell_msg, nullptr);

  EXPECT_EQ(pm->get_pos("ZNH6"), -5);
  EXPECT_EQ(mock_pcoord_zn.get_position(), -5);
}

TEST_F(PositionManagerTest, GetPosReturnsZeroForUnknown) {
  auto pm = create_position_manager();
  EXPECT_EQ(pm->get_pos("UNKNOWN"), 0);
  EXPECT_EQ(pm->get_pos(""), 0);
}

TEST_F(PositionManagerTest, NoPCoordDoesNotCrash) {
  std::map<std::string, light::PCoord*> pcoords = {{"ZNH6", &mock_pcoord_zn}};
  auto pm = create_position_manager(pcoords);

  AddToPos add_msg("OTHER", en::bs::BUY, 10);
  EXPECT_NO_THROW(process_msg(pm.get(), &add_msg, nullptr));
  EXPECT_EQ(pm->get_pos("OTHER"), 10);
  EXPECT_EQ(mock_pcoord_zn.get_position(), 0);
}

// NOTE: the SpreadPulseTest suite from the upstream file is not ported. The
// spread path (AddSpreadOrder, the ratio-preserving chunk pulse, the leg
// netting ledger) does not exist in this repo — PositionManager here is
// outright-only. What remains is the AddToPos -> PCoord path, which is the
// part the sim actually exercises.
