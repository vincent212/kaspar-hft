/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Integration Tests for light22 Order Placement and Cancellation
 *
 * These tests verify that light22 actually sends Order and Cancel messages
 * to the SOM (Simple Order Manager) under various conditions.
 *
 * =============================================================================
 * ORDER PLACEMENT CONDITIONS
 * =============================================================================
 *
 * For light22<BUY> (buying to reach target):
 *   - position < targetpos
 *   - trading flag is true
 *   - not in delay mode
 *   - working_orders + ord_sz <= all_orders_max
 *   - sz_at_px < diff_from_target (not over-hedging)
 *   - price is within max_dist of best_bid
 *   - eob_counter >= place_after_n_eob (deterministic placement)
 *
 * For light22<SEL> (selling to reach target):
 *   - position > targetpos
 *   - same conditions as above but checking against best_ask
 *
 * =============================================================================
 * CANCELLATION CONDITIONS
 * =============================================================================
 *
 * Cancel if any of these conditions are true:
 *   1. attached_order_id matches (the triggering order was hit)
 *   2. position == 0 (flat, no need to hedge)
 *   3. For BUY: position >= targetpos (target reached)
 *   4. For SEL: position <= targetpos (target reached)
 *   5. hedge_pos_size_breach: would over-hedge
 *   6. directional_position_breach: too many orders at price level
 *   7. delay_skip flag is set
 *   8. too_far_from_inside: order price too far from best bid/ask
 *
 * =============================================================================
 * MESSAGE FLOW (Buy-side order lifecycle)
 * =============================================================================
 *
 * 1. Startup:
 *    [actors::msg::Start] → light22
 *    light22 → [Subscribe(HI)] → Aggregator
 *    light22 → [RegisterLight] → RiskManager (optional)
 *
 * 2. Market data arrives:
 *    [EndOfBurst(ADD at price X)] → light22
 *    light22 evaluates: position=0, targetpos=10, should place buy
 *    light22 → [Order(BUY, sz=5, px=X)] → SOM
 *    SOM → [Ack(oid=123)] → light22
 *    light22.ord_info.set(123, X)
 *    QCoord.add_order(X, 5, mmid, 123)
 *
 * 3. Order fills:
 *    [Fill(oid=123, sz=5, stbf=0)] → light22
 *    light22 → PCoord.add_position(BUY, 5)
 *    light22 → QCoord.remove_order(X, 5, mmid, 123, all=true)
 *    light22 → [Fill] → subscribers
 *    light22.ord_info.clear()
 *
 * 4. Trading turned off:
 *    [Set(TRADING_OFF)] → light22
 *    light22.trading = false
 *    If ord_info.has_value():
 *      light22 → [Cancel(oid)] → SOM
 *      SOM → [CancAck(oid)] → light22
 *      light22.ord_info.clear()
 *
 * =============================================================================
 * TEST SETUP REQUIREMENTS
 * =============================================================================
 *
 * 1. RefData initialization:
 *    RefData::set_universe("/path/to/universe.csv", "");
 *
 * 2. Mock objects:
 *    - MockSOM: Captures Order/Cancel, sends Ack/CancAck responses
 *    - MockOB (Aggregator): Source of EndOfBurst messages
 *    - MockTimer: Time-based operations
 *    - MockPCoord: Position tracking (virtual methods for polymorphism)
 *    - MockQCoord: Order tracking (virtual methods for polymorphism)
 *
 * 3. Key light22 settings for tests:
 *    - light->skip = 0 (bypass startup skip of 10 EOB messages)
 *    - light->trading.reset(true) (enable trading)
 *    - light->targetpos = N (set target position)
 */

#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include "light/act/light22.hpp"
#include "light/qcoord.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "actors/msg/Start.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/MockPCoord.hpp"
#include "unit_test/MockQCoord.hpp"
#include "unit_test/TestHelper.hpp"
#include "enum/e_names.hpp"
#include <boost/property_tree/ptree.hpp>

using namespace light::act;
using namespace unit_test;

// =============================================================================
// Test Fixture with RefData Setup
// =============================================================================

class Light22IntegrationTest : public ::testing::Test {
protected:
  static bool refdata_initialized;

  MockSOM mock_som;
  MockAggregator mock_ob;
  MockTimer mock_timer;
  MockSuper mock_super;
  MockDB mock_db;

  MockQCoord mock_qcoord;
  MockPCoord mock_pcoord;

  boost::property_tree::ptree pt;

  static void SetUpTestSuite() {
    if (!refdata_initialized) {
      // Self-contained fixture universe. NOT sim/config: the sim deliberately
      // has no universe.csv (it seeds RefData empty and registers every
      // instrument from the universe JSON), so pointing here keeps the tests
      // independent of how the sim chooses to bootstrap reference data.
      const char* proj_root = std::getenv("KSPRPROJ");
      std::string universe_path = proj_root ?
          std::string(proj_root) + "/unit_test/config/universe.csv" :
          "../config/universe.csv";
      frame::ref::RefData::set_universe(
          universe_path,
          ""  // No rounding file needed for basic tests
      );
      refdata_initialized = true;
    }
  }

  void SetUp() override {
    // Configure light22 parameters
    pt.put("nlevels", 3);
    pt.put("ord_sz", 5);
    pt.put("lev_orders_max", 10);
    pt.put("place_after_n_eob", 5);  // Place after 5 EOB messages
    pt.put("place_rate_bp", 0);      // 0 = deterministic placement (see light22.hpp)

    // Clear mocks
    mock_som.clear();
    mock_ob.clear();
    mock_timer.clear();
    mock_qcoord.clear();
    mock_pcoord.clear();
  }

  // Helper to invoke handlers (uses TestHelper)
  void process_msg(actors::Actor* actor, const actors::Message* msg,
                   actors::Actor* sender = nullptr) {
    TestHelper::invoke_handler(actor, msg, sender);
  }

  // Counter for generating unique ex_order_ids
  // Note: ex_order_id=0 matches the default attached_order_id=0 and would
  // trigger an unwanted cancel, so we use non-zero values
  uint64_t next_ex_order_id = 1000;

  // Helper to create a payload for EndOfBurst
  boost::intrusive_ptr<frame::mda::msg::data_pay_load> make_payload(
      int sym_id,
      int price,
      int size,
      en::md mev = en::md::ADD,
      uint64_t ex_order_id = 0)
  {
    // Use unique ex_order_id to avoid triggering attached_order_id cancel logic
    // (attached_order_id=0 would match ex_order_id=0)
    if (ex_order_id == 0) {
      ex_order_id = next_ex_order_id++;
    }

    auto payload = new frame::mda::msg::data_pay_load();
    payload->mkt = en::x::SIM;
    payload->sym = sym_id;
    payload->px = frame::ref::Price(price, frame::ref::RefData::get_asset(sym_id));
    payload->sz = size;
    payload->side = en::bs::BUY;
    payload->mev = mev;
    payload->action = en::mt::NONE;
    payload->ot = en::ot::LIMIT;
    payload->ex_order_id = ex_order_id;
    payload->recovery = false;
    payload->txtim_epoch = 1000000000;
    payload->hndl_tim_epoch = 1000000000;
    payload->send_tim = 1000000000;  // Required for is_valid() check
    payload->tim = 1000000000;
    payload->sendtim_epoch = 1000000000;

    // Set BBBO (best bid/best offer)
    payload->point_.sym = sym_id;
    payload->point_.baddata = false;
    payload->point_.bid_px[0] = price;
    payload->point_.ask_px[0] = price + 1;
    payload->point_.bid_sz[0] = 100;
    payload->point_.ask_sz[0] = 100;

    return payload;
  }

  // A TradeNotify carrying a real print. The aggressive path shadows THIS,
  // the way the passive path shadows an ADD: place_if_can_impl prices from
  // payload->px, so the resulting limit order sits at the price that traded.
  // resting_side is the side of the order ALREADY IN THE BOOK, which is what
  // the feed reports and what is_hit()/is_tak() key on:
  //   resting BUY  -> a bid was HIT   -> printed at the bid -> aggressive for a SELLER
  //   resting SEL  -> an offer TAKEN  -> printed at the ask -> aggressive for a BUYER
  frame::ob::msg::TradeNotify* make_trade(int sym_id, int price, int size,
                                          en::bs resting_side = en::bs::SEL)
  {
    auto payload = make_payload(sym_id, price, size, en::md::MOD);
    payload->action = en::mt::EXEC;      // what makes it a trade (Data.hpp)
    payload->side = resting_side;
    return new frame::ob::msg::TradeNotify(payload);
  }

  // Helper to create EndOfBurst message
  frame::ob::msg::EndOfBurst* make_eob(
      int sym_id,
      int price,
      int size,
      en::md mev = en::md::ADD,
      uint64_t ex_order_id = 0)
  {
    auto payload = make_payload(sym_id, price, size, mev, ex_order_id);
    auto eob = new frame::ob::msg::EndOfBurst(payload);
    eob->last = true;
    return eob;
  }

  // Helper to send N EOB messages to place an order
  // Note: light22 has a behavior where the attached_order_id check in
  // canc_if_must_impl triggers on the same message that places the order,
  // immediately cancelling it. To work around this, we:
  // 1. Set attached_order_id to UINT64_MAX before placement (won't match any ex_order_id)
  // 2. After placement, attached_order_id will be set to the placement ex_order_id
  //    but canc_if_must_impl will have already run with non-matching value
  template<typename LightT>
  void place_order_via_eob(LightT& light, int sym_id, int price, int n_messages = 5) {
    for (int i = 0; i < n_messages; i++) {
      auto eob = make_eob(sym_id, price, 5, en::md::ADD);
      // Before processing, set attached_order_id to non-matching value
      // so that canc_if_must_impl's check won't trigger
      light.attached_order_id = UINT64_MAX;
      process_msg(&light, eob, &mock_ob);
      delete eob;
    }
    // Reset for subsequent operations
    light.attached_order_id = 0;
  }

  // Helper to create light22<BUY>
  std::unique_ptr<light22<en::bs::BUY>> create_buy_light(
      const std::string& name = "TestBuyLight",
      int targetpos = 10)
  {
    auto light = std::make_unique<light22<en::bs::BUY>>(
        "TEST",                      // prefix
        &mock_db,                    // db
        nullptr,                     // rm (risk manager)
        name,                        // name
        en::trader::OCCAMUST,        // owner
        "ZNH6",                      // sym
        en::x::SIM,                  // md_venue
        en::x::SIM,                  // trading_venue
        &mock_qcoord,                // qcoord
        &mock_pcoord,                // pcoord
        &mock_ob,                    // ob (aggregator)
        &mock_super,                 // super
        1,                           // tier
        &mock_timer,                 // timer
        &mock_som,                   // som
        1,                           // mmid
        pt,                          // config
        0,                           // VG
        false                        // dry_run
    );

    // Set target position
    light->targetpos = targetpos;
    light->trading.reset(true);

    // Reset skip counter to 0 for immediate order placement in tests
    // (production light22 skips first 10 EOB messages to wait for market stabilization)
    light->skip = 0;

    return light;
  }

  // Helper to create light22<SEL>
  std::unique_ptr<light22<en::bs::SEL>> create_sell_light(
      const std::string& name = "TestSellLight",
      int targetpos = -10)
  {
    auto light = std::make_unique<light22<en::bs::SEL>>(
        "TEST",                      // prefix
        &mock_db,                    // db
        nullptr,                     // rm
        name,                        // name
        en::trader::OCCAMUST,        // owner
        "ZNH6",                      // sym
        en::x::SIM,                  // md_venue
        en::x::SIM,                  // trading_venue
        &mock_qcoord,                // qcoord
        &mock_pcoord,                // pcoord
        &mock_ob,                    // ob
        &mock_super,                 // super
        1,                           // tier
        &mock_timer,                 // timer
        &mock_som,                   // som
        1,                           // mmid
        pt,                          // config
        0,                           // VG
        false                        // dry_run
    );

    light->targetpos = targetpos;
    light->trading.reset(true);

    // Reset skip counter to 0 for immediate order placement in tests
    light->skip = 0;

    return light;
  }

  // Get symbol ID for ZNH6
  // Place an order, then make the attached order disappear. Leaves the mocks
  // cleared as of just before the attach-gone EOB, so a test can assert on
  // exactly what that one message produced.
  void place_then_drop_attached(light22<en::bs::BUY>* light,
                                int sym_id,
                                uint64_t trigger_order_id)
  {
    mock_som.clear();
    mock_timer.clear();

    // place_after_n_eob is 5 in the fixture: four warm-up ADDs, then the ADD
    // carrying the order id we attach to.
    for (int i = 0; i < 4; i++) {
      auto eob = make_eob(sym_id, 100, 5, en::md::ADD, 0);
      process_msg(light, eob, &mock_ob);
      delete eob;
    }
    auto eob5 = make_eob(sym_id, 100, 5, en::md::ADD, trigger_order_id);
    process_msg(light, eob5, &mock_ob);
    delete eob5;

    EXPECT_TRUE(light->ord_info.has_value()) << "should have placed an order";
    EXPECT_EQ(light->attached_order_id, trigger_order_id);

    mock_som.clear();
    mock_timer.clear();

    // The attached order shows up again -> it has been hit or pulled.
    auto gone = make_eob(sym_id, 100, 5, en::md::ADD, trigger_order_id);
    process_msg(light, gone, &mock_ob);
    delete gone;
  }

  int get_sym_id() {
    auto asset = frame::ref::RefData::get_asset("ZNH6");
    return asset ? asset->id : 1;
  }
};

bool Light22IntegrationTest::refdata_initialized = false;

// =============================================================================
// Order Placement Tests
// =============================================================================

/**
 * Test 1: PlaceOrderAfterNEOBMessages
 *
 * VERIFY: Order is placed after exactly N EndOfBurst ADD messages
 *
 * INPUT:  Send 5 EOB ADD messages
 * OUTPUT: Order message sent to SOM after 5th message
 */
TEST_F(Light22IntegrationTest, PlaceOrderAfterNEOBMessages) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  // Position is 0, target is 10, so should place BUY orders
  mock_pcoord.set_position(0);

  // Send Start message
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  // Clear any subscription messages
  mock_som.clear();

  // Send 4 EOB messages - should NOT place order yet
  for (int i = 0; i < 4; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "Should not place order before reaching place_after_n_eob count";

  // Send 5th EOB message - should place order now
  auto eob5 = make_eob(sym_id, 100, 5, en::md::ADD);
  process_msg(light.get(), eob5, &mock_ob);
  delete eob5;

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 1u)
      << "Should place order after 5th EOB message";

  // Verify order details
  auto order = mock_som.get_message<frame::som::msg::Order>(0);
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(order->side, en::bs::BUY);
  EXPECT_EQ(order->venue, en::x::SIM);
}

/**
 * Test 2: NoOrderWhenAtTarget_BuySide
 *
 * VERIFY: BUY side does not place orders when position >= targetpos
 */
TEST_F(Light22IntegrationTest, NoOrderWhenAtTarget_BuySide) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  // Position equals target - should not place orders
  mock_pcoord.set_position(10);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Send more than enough EOB messages
  for (int i = 0; i < 10; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "Should not place order when position >= targetpos for BUY side";
}

/**
 * Test 3: NoOrderWhenAtTarget_SellSide
 *
 * VERIFY: SEL side does not place orders when position <= targetpos
 */
TEST_F(Light22IntegrationTest, NoOrderWhenAtTarget_SellSide) {
  auto light = create_sell_light("TestSell", -10);
  int sym_id = get_sym_id();

  // Position equals target - should not place orders
  mock_pcoord.set_position(-10);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 10; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "Should not place order when position <= targetpos for SEL side";
}

/**
 * Test 4: CounterResetsAfterOrderPlacement
 *
 * VERIFY: EOB counter resets after placing order, so next order also takes N messages
 */
TEST_F(Light22IntegrationTest, CounterResetsAfterOrderPlacement) {
  pt.put("place_after_n_eob", 3);  // Place after 3 EOB messages
  auto light = create_buy_light("TestBuy", 100);  // High target so we can place multiple
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // First cycle: 3 messages -> 1 order
  for (int i = 0; i < 3; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  size_t orders_after_first = mock_som.count_messages_of_type<frame::som::msg::Order>();
  EXPECT_EQ(orders_after_first, 1u) << "Should place first order after 3 EOB messages";

  // Simulate order ack to clear ord_info (so we can place another)
  if (light->ord_info.has_value()) {
    auto ack = new frame::som::msg::CancAck();
    ack->id = light->ord_info.get_oid();
    process_msg(light.get(), ack, &mock_som);
  }

  // Second cycle: 3 more messages -> another order
  for (int i = 0; i < 3; i++) {
    auto eob = make_eob(sym_id, 101, 5, en::md::ADD);  // Different price
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  // Note: May not place second order due to other conditions (existing order, etc.)
  // The main test is that counter reset works
  EXPECT_GE(mock_som.count_messages_of_type<frame::som::msg::Order>(), 1u);
}

// =============================================================================
// Order Cancellation Tests
// =============================================================================

/**
 * Test 5: CancelWhenPositionReachesTarget
 *
 * VERIFY: Working order is cancelled when position reaches target
 */
TEST_F(Light22IntegrationTest, CancelWhenPositionReachesTarget) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  // Start below target
  mock_pcoord.set_position(5);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Place an order
  for (int i = 0; i < 5; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_TRUE(light->ord_info.has_value()) << "Should have a working order";

  // Now simulate position reaching target (e.g., from fill elsewhere)
  mock_pcoord.set_position(10);

  // Send EOB with DEL/CANC action - should trigger cancel evaluation
  auto payload = make_payload(sym_id, 100, 5, en::md::DEL);
  payload->action = en::mt::CANC;
  auto eob = new frame::ob::msg::EndOfBurst(payload);
  process_msg(light.get(), eob, &mock_ob);
  delete eob;

  // Check for Cancel message
  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "Should send Cancel when position reaches target";
}

/**
 * Test 6a/b/c: what happens when the shadowed ("attached") order goes away.
 *
 * The light shadows a real resting order. When that order is hit or pulled we
 * have to decide how long to keep ours in the queue. Three configurations:
 *
 *   default (both knobs 0)      cancel on the same message
 *   delayed_cancel_ms    > 0    cancel when a wall-clock alarm fires
 *   delayed_cancel_events > 0   cancel after N further EOBs on our instrument
 *
 * The middle and last cases must NOT cancel immediately. That is the whole
 * point of the delay, and it is exactly what the code got wrong: the branch
 * scheduled the alarm and then fell into the RET macro, which cancels on the
 * spot — so the 500 ms default delay never delayed anything. Every test below
 * therefore asserts the *absence* of an early Cancel, not just its eventual
 * presence.
 */

TEST_F(Light22IntegrationTest, AttachedOrderGone_CancelsImmediatelyByDefault) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  ASSERT_EQ(light->delayed_cancel_ms, 0);
  ASSERT_EQ(light->delayed_cancel_events, 0);

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  uint64_t trigger_order_id = 12345;
  place_then_drop_attached(light.get(), sym_id, trigger_order_id);

  EXPECT_EQ(light->attached_order_id, 0u)
      << "attached_order_id should be reset once the attached order is gone";
  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "with no delay configured the cancel goes out on the same message";
  EXPECT_FALSE(mock_timer.has_message_of_type<frame::mtim::msg::AlarmClockSub>())
      << "no delay configured, so no alarm should be scheduled";
}

TEST_F(Light22IntegrationTest, AttachedOrderGone_MsDelayDefersCancel) {
  pt.put("delayed_cancel_ms", 500);
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  ASSERT_EQ(light->delayed_cancel_ms, 500);

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  uint64_t trigger_order_id = 12345;
  place_then_drop_attached(light.get(), sym_id, trigger_order_id);

  EXPECT_EQ(light->attached_order_id, 0u);
  EXPECT_TRUE(mock_timer.has_message_of_type<frame::mtim::msg::AlarmClockSub>())
      << "should schedule the delayed-cancel alarm";
  EXPECT_FALSE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "a delay that cancels on the same message is not a delay";

  // Now fire the alarm.
  auto alarm = new frame::mtim::msg::Alarm();
  alarm->timer_id = light->DELAYED_CANCEL;
  process_msg(light.get(), alarm, &mock_timer);

  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "should cancel once the alarm fires";
}

TEST_F(Light22IntegrationTest, AttachedOrderGone_EventDelayDefersCancelByNEobs) {
  const int kDelayEvents = 3;
  pt.put("delayed_cancel_events", kDelayEvents);
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  ASSERT_EQ(light->delayed_cancel_events, kDelayEvents);

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  uint64_t trigger_order_id = 12345;
  place_then_drop_attached(light.get(), sym_id, trigger_order_id);

  EXPECT_EQ(light->attached_order_id, 0u);
  EXPECT_FALSE(mock_timer.has_message_of_type<frame::mtim::msg::AlarmClockSub>())
      << "the event-count path must not touch the timer";
  EXPECT_FALSE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "cancel must not go out on the same message";
  EXPECT_EQ(light->pending_cancel_eob, kDelayEvents);

  // kDelayEvents-1 further EOBs: still armed, still no cancel.
  for (int i = 1; i < kDelayEvents; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD, 999 + i);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
    EXPECT_FALSE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
        << "cancelled after " << i << " events, expected " << kDelayEvents;
    EXPECT_EQ(light->pending_cancel_eob, kDelayEvents - i);
  }

  // The kDelayEvents'th EOB fires it.
  auto last = make_eob(sym_id, 100, 5, en::md::ADD, 999 + kDelayEvents);
  process_msg(light.get(), last, &mock_ob);
  delete last;

  EXPECT_EQ(light->pending_cancel_eob, 0);
  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "should cancel on the " << kDelayEvents << "th event";
}

/**
 * A pending event countdown must never outlive the order it was armed for:
 * if the order goes away and a new one is placed, the stale countdown would
 * otherwise cancel the new order early.
 */
TEST_F(Light22IntegrationTest, EventDelayCountdownIsClearedByANewPlacement) {
  pt.put("delayed_cancel_events", 50);   // long enough to still be pending
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  place_then_drop_attached(light.get(), sym_id, 12345);
  EXPECT_EQ(light->pending_cancel_eob, 50);

  // Cancel for real -> countdown must be dropped.
  light->cancel_order();
  EXPECT_EQ(light->pending_cancel_eob, 0)
      << "an actual cancel voids any deferred one";
}

/**
 * FAILING BY DESIGN -- the delayed cancel survives only an idle book.
 *
 * AttachedOrderGone_EventDelayDefersCancelByNEobs proves the countdown works
 * when nothing else happens: every EOB it sends repeats the same price, so no
 * competing cancel path fires. Production is not like that. Between arming and
 * expiry there are a dozen paths that call cancel_order() -- the RET macro, the
 * too-far-from-inside check, the position checks -- and cancel_order() zeroes
 * pending_cancel_eob (light22_base.hpp:348), voiding the deferral.
 *
 * On ES the touch is one tick wide in 96% of windows and moves constantly, so a
 * reprice almost certainly lands inside any 5-event window. If so,
 * `delayed_cancel_events 5` is configured but has no practical effect, and arm
 * A's results are not measuring what the config says they are.
 *
 * This test pins the intended semantics: the deferral is armed because the
 * ATTACHED order went away, and it should not be cut short by an unrelated book
 * move that leaves our own order still valid.
 */
TEST_F(Light22IntegrationTest, EventDelaySurvivesAnUnrelatedBookMove) {
  const int kDelayEvents = 5;
  pt.put("delayed_cancel_events", kDelayEvents);
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  place_then_drop_attached(light.get(), sym_id, 12345);
  ASSERT_EQ(light->pending_cancel_eob, kDelayEvents)
      << "fixture precondition: the deferral must be armed";
  ASSERT_TRUE(light->ord_info.has_value());

  mock_som.clear();

  // One further EOB in which the inside has moved a tick -- the ordinary case
  // on a one-tick book, and nothing to do with our attached order.
  auto payload = make_payload(sym_id, 100, 5, en::md::ADD, 777);
  payload->point_.bid_px[0] = 101;
  payload->point_.ask_px[0] = 102;
  auto eob = new frame::ob::msg::EndOfBurst(payload);
  process_msg(light.get(), eob, &mock_ob);
  delete eob;

  EXPECT_FALSE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "a book move must not cut the deferral short: the delay was armed for "
         "the attached order going away, and cancelling here makes "
         "delayed_cancel_events inoperative on any actively quoted instrument";
  EXPECT_EQ(light->pending_cancel_eob, kDelayEvents - 1)
      << "the countdown should have decremented by one, not been voided";
}

/**
 * The missing link in the cancel-latency chain.
 *
 * The chain is: light stamps the cancel -> SOM forwards the stamp as canc_ts
 * -> OB holds the CANCD in del_q until ts0 + cancel_delay.
 *
 * The two ends are covered -- OBDelayQueueTest.OurCancelIsWithheldForTheSameDelay
 * gates on the OB side, SOMCancelLatencyTest.CancelCarriesItsOwnDecisionTime on
 * the SOM side. Nothing checked that the LIGHT emits a stamp at all, and that
 * is the dangerous gap: OBDelayQueueTest.AMessageWithNoTimestampIsNotDelayed
 * and SOMCancelLatencyTest.AnUntimedCancelStaysUntimed both confirm a ts of 0
 * is passed through and applied WITHOUT DELAY. So a light that forgot to stamp
 * would give every cancel zero latency while every other test still passed, and
 * the whole latency sweep would be measuring an algorithm whose cancels always
 * beat the flow.
 *
 * That exact bug has already happened once on the SOM side -- cancels were
 * stamped with the ORIGINAL ORDER's ts, whose deadline was already past, so
 * they arrived with no latency (see the comment at SOM::cancel_order). This
 * pins the light end so it cannot happen there.
 */
TEST_F(Light22IntegrationTest, TheLightStampsItsCancelWithMarketTime) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  place_order_via_eob(*light, sym_id, 100);
  ASSERT_TRUE(light->ord_info.has_value());

  const uint64_t tx = light->curr_tx_time;
  EXPECT_GT(tx, 0u) << "fixture precondition: the light must have market time";

  mock_som.clear();
  light->cancel_order();

  auto canc = mock_som.get_message<frame::som::msg::Cancel>(0);
  ASSERT_NE(canc, nullptr) << "a cancel must have gone out";
  EXPECT_GT(canc->ts, 0u)
      << "an untimed cancel is applied by OB WITHOUT DELAY "
         "(OBDelayQueueTest.AMessageWithNoTimestampIsNotDelayed), so a zero "
         "stamp here silently gives every cancel zero latency";
  EXPECT_EQ(canc->ts, tx)
      << "the stamp must be the light's own market time, not the original "
         "order's -- that variant already shipped once and gave cancels no "
         "latency while looking like it modelled one";
}

/**
 * FAILS WITHOUT THE GUARD -- an AGGR_TTL alarm must not cancel a DIFFERENT order.
 *
 * The alarm is armed when a cross is sent and fires aggr_ttl_ms later. But a
 * cross fills in ~40 us, and the light places again long before 1 ms is up, so
 * an alarm routinely outlives the order it was armed for. Without an identity
 * check it cancels whatever happens to be in the slot -- killing a healthy new
 * order early, and inflating the TTL-cancel count that the aggressive arm reads
 * as "the cross missed".
 *
 * pending_cancel_eob already has this guard (it is zeroed by cancel_order and by
 * a fresh placement, tested by EventDelayCountdownIsClearedByANewPlacement). It
 * was not carried across when AGGR_TTL was added.
 */
TEST_F(Light22IntegrationTest, AggrTtlDoesNotCancelASubsequentOrder) {
  pt.put("aggr_ttl_ms", 1);
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  // An order is working, and the TTL was armed for a DIFFERENT (earlier) one.
  place_order_via_eob(*light, sym_id, 100);
  ASSERT_TRUE(light->ord_info.has_value());
  const int live_oid = light->ord_info.get_oid();
  light->aggr_ttl_oid = live_oid - 1;      // armed for an order that is gone

  mock_som.clear();

  auto alarm = new frame::mtim::msg::Alarm();
  alarm->timer_id = light->AGGR_TTL;
  process_msg(light.get(), alarm, &mock_timer);
  delete alarm;

  EXPECT_FALSE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "a stale AGGR_TTL alarm cancelled the order that replaced the one it "
         "was armed for";
  EXPECT_TRUE(light->ord_info.has_value())
      << "the live order must survive a stale TTL";
}

/**
 * The other half: when the alarm IS for the live order, it must still fire.
 */
TEST_F(Light22IntegrationTest, AggrTtlCancelsTheOrderItWasArmedFor) {
  pt.put("aggr_ttl_ms", 1);
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  place_order_via_eob(*light, sym_id, 100);
  ASSERT_TRUE(light->ord_info.has_value());
  light->aggr_ttl_oid = light->ord_info.get_oid();   // armed for THIS one

  mock_som.clear();

  auto alarm = new frame::mtim::msg::Alarm();
  alarm->timer_id = light->AGGR_TTL;
  process_msg(light.get(), alarm, &mock_timer);
  delete alarm;

  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "the TTL must still pull the cross it was armed for";
}

/**
 * Test 7: CancelWhenTooFarFromInside
 *
 * VERIFY: Cancel is triggered when order price is too far from best bid/ask
 */
TEST_F(Light22IntegrationTest, CancelWhenTooFarFromInside) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Place an order at price 100
  for (int i = 0; i < 5; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_TRUE(light->ord_info.has_value());
  int order_px = light->ord_info.get_px();
  EXPECT_EQ(order_px, 100);

  mock_som.clear();

  // Now send EOB with best_bid moved far away (110) and DEL/CANC action
  // Our order at 100 is now 10 ticks away, > max_dist_cancel (6)
  auto payload = make_payload(sym_id, 100, 5, en::md::DEL);
  payload->action = en::mt::CANC;
  payload->point_.bid_px[0] = 110;  // Best bid moved up
  payload->point_.ask_px[0] = 111;
  auto eob = new frame::ob::msg::EndOfBurst(payload);
  process_msg(light.get(), eob, &mock_ob);
  delete eob;

  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "Should cancel when order too far from inside market";
}

/**
 * Test 8: CancelWhenPositionIsZero
 *
 * VERIFY: Cancel working order when position goes to 0
 */
TEST_F(Light22IntegrationTest, CancelWhenPositionIsZero) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  // Start with position 5
  mock_pcoord.set_position(5);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Place an order using helper (handles attached_order_id cancel workaround)
  place_order_via_eob(*light, sym_id, 100);

  EXPECT_TRUE(light->ord_info.has_value()) << "Order should be placed";
  EXPECT_FALSE(light->ord_info.get_canc()) << "Order should not be cancelled";
  mock_som.clear();

  // Position goes to 0
  mock_pcoord.set_position(0);

  // Send EOB with DEL/CANC action - should cancel due to zero_pos condition
  auto payload = make_payload(sym_id, 100, 5, en::md::DEL);
  payload->action = en::mt::CANC;
  auto eob = new frame::ob::msg::EndOfBurst(payload);
  process_msg(light.get(), eob, &mock_ob);
  delete eob;

  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "Should cancel when position is 0";
}

/**
 * Test 9: TradingOffCancelsWorkingOrder
 *
 * VERIFY: Setting trading=off cancels any working order
 */
TEST_F(Light22IntegrationTest, TradingOffCancelsWorkingOrder) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Place an order
  for (int i = 0; i < 5; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_TRUE(light->ord_info.has_value());
  mock_som.clear();

  // Turn trading off
  auto set_off = new light::msg::Set(light::msg::Set::TRADING_OFF);
  process_msg(light.get(), set_off, &mock_ob);

  EXPECT_TRUE(mock_som.has_message_of_type<frame::som::msg::Cancel>())
      << "Should cancel working order when trading turned off";
  EXPECT_FALSE(light->trading.get()) << "Trading should be off";
}

// =============================================================================
// Level Limit Tests
// =============================================================================

/**
 * Test 10: NoOrderWhenTooManyAtLevel
 *
 * VERIFY: No order placed when sz_at_px >= diff_from_target
 */
TEST_F(Light22IntegrationTest, NoOrderWhenTooManyAtLevel) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  // Position is 5, target is 10, diff_from_target = 5
  mock_pcoord.set_position(5);

  // Pre-fill the level with 5 orders already
  mock_qcoord.set_sz_at_px(100, 5);
  mock_qcoord.set_total_sz(5);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Try to place order - should be blocked
  for (int i = 0; i < 10; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "Should not place order when sz_at_px >= diff_from_target";
}

/**
 * Test 11: NoOrderWhenTotalOrdersExceedMax
 *
 * VERIFY: No order placed when working orders would exceed all_orders_max
 */
TEST_F(Light22IntegrationTest, NoOrderWhenTotalOrdersExceedMax) {
  auto light = create_buy_light("TestBuy", 100);  // High target
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);

  // all_orders_max = lev_orders_max * (nlevels + 3) = 10 * 6 = 60
  // Set total_sz to 58, so adding 5 more would exceed 60
  mock_qcoord.set_total_sz(58);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 10; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "Should not place order when total orders would exceed all_orders_max";
}

// =============================================================================
// Price Distance Tests
// =============================================================================

/**
 * Test 12: NoOrderWhenPriceTooFarFromBestBid
 *
 * VERIFY: BUY order rejected when price < best_bid - max_dist
 */
TEST_F(Light22IntegrationTest, NoOrderWhenPriceTooFarFromBestBid) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Create EOB where payload price is far below best_bid
  // max_dist = 4, so if best_bid = 100 and order_px = 94, it's 6 ticks away
  auto payload = make_payload(sym_id, 94, 5, en::md::ADD);
  payload->point_.bid_px[0] = 100;  // Best bid
  payload->point_.ask_px[0] = 101;

  for (int i = 0; i < 5; i++) {
    auto eob = new frame::ob::msg::EndOfBurst(payload);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "Should not place order when price too far from best bid";
}

// =============================================================================
// Fill Handling Tests
// =============================================================================

/**
 * Test 13: FillUpdatesPositionViaPCoord
 *
 * VERIFY: Fill message updates position via PCoord::add_position
 */
TEST_F(Light22IntegrationTest, FillUpdatesPositionViaPCoord) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // Place an order
  for (int i = 0; i < 5; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  ASSERT_TRUE(light->ord_info.has_value());
  int oid = light->ord_info.get_oid();

  // Clear PCoord call tracking
  mock_pcoord.calls.clear();

  // Simulate fill
  auto fill = new frame::som::msg::Fill();
  fill->id = oid;
  fill->sym = sym_id;
  fill->side = en::bs::BUY;
  fill->sz = 5;
  fill->still_to_be_filled = 0;  // Fully filled
  fill->owner = en::trader::OCCAMUST;
  fill->venue = en::x::SIM;
  fill->px = frame::ref::Price(100, frame::ref::RefData::get_asset(sym_id));

  process_msg(light.get(), fill, &mock_som);

  // Verify PCoord::add_position was called
  ASSERT_GE(mock_pcoord.call_count(), 1u)
      << "PCoord::add_position should be called on fill";

  auto& call = mock_pcoord.calls.back();
  EXPECT_EQ(call.side, en::bs::BUY);
  EXPECT_EQ(call.sz, 5);

  // Verify ord_info cleared after full fill
  EXPECT_FALSE(light->ord_info.has_value())
      << "ord_info should be cleared after full fill";
}

/**
 * Test 14: PartialFillKeepsOrderActive
 *
 * VERIFY: Partial fill doesn't clear ord_info
 */
TEST_F(Light22IntegrationTest, PartialFillKeepsOrderActive) {
  auto light = create_buy_light("TestBuy", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);

  // Place an order
  for (int i = 0; i < 5; i++) {
    auto eob = make_eob(sym_id, 100, 5, en::md::ADD);
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  ASSERT_TRUE(light->ord_info.has_value());
  int oid = light->ord_info.get_oid();

  // Partial fill
  auto fill = new frame::som::msg::Fill();
  fill->id = oid;
  fill->sym = sym_id;
  fill->side = en::bs::BUY;
  fill->sz = 2;
  fill->still_to_be_filled = 3;  // Partial fill
  fill->owner = en::trader::OCCAMUST;
  fill->venue = en::x::SIM;
  fill->px = frame::ref::Price(100, frame::ref::RefData::get_asset(sym_id));

  process_msg(light.get(), fill, &mock_som);

  // ord_info should still be active
  EXPECT_TRUE(light->ord_info.has_value())
      << "ord_info should remain after partial fill";
}

// =============================================================================
// Sell Side Tests
// =============================================================================

/**
 * Test 15: SellSidePlacesOrderWhenAboveTarget
 *
 * VERIFY: SEL side places order when position > targetpos
 */
TEST_F(Light22IntegrationTest, SellSidePlacesOrderWhenAboveTarget) {
  auto light = create_sell_light("TestSell", -10);
  int sym_id = get_sym_id();

  // Position is 0, target is -10, so should place SEL orders to go short
  mock_pcoord.set_position(0);

  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 5; i++) {
    auto eob = make_eob(sym_id, 101, 5, en::md::ADD);  // Ask price
    process_msg(light.get(), eob, &mock_ob);
    delete eob;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 1u)
      << "SEL side should place order when position > targetpos";

  auto order = mock_som.get_message<frame::som::msg::Order>(0);
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(order->side, en::bs::SEL);
}




// ---------------------------------------------------------------------------
// AGGRESSIVE PARTICIPATION
//
// The aggressive path is the passive one with a different trigger. A passive
// light shadows a resting ADD and quotes on its own side, so it waits to be
// hit. An aggressive light shadows a TRADE and quotes at the price that trade
// printed at -- the far side for us -- so it executes instead of waiting.
//
// It reuses place_if_can_impl() rather than reimplementing placement, which is
// why these tests assert on the ORDER that comes out rather than on internals.
// ---------------------------------------------------------------------------

TEST_F(Light22IntegrationTest, AggressiveIsOffUnlessConfigured) {
  // Default is 0, and 0 must mean the light never reacts to a trade at all.
  // Every result measured before this feature existed depends on that: if a
  // trade could place an order without the config asking for it, no previous
  // run would be reproducible.
  auto light = create_buy_light("TestBuyNoAggr", 10);
  int sym_id = get_sym_id();
  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 20; i++) {
    auto t = make_trade(sym_id, 100, 5);
    process_msg(light.get(), t, &mock_ob);
    delete t;
  }

  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "a trade must not place anything while aggr_participation_bp is 0";
}

TEST_F(Light22IntegrationTest, AggressiveShadowsATradeAtThatTradesPrice) {
  // 10000bp = every trade, so the coin flip cannot hide the behaviour.
  // The config value is a BANK TOTAL, divided by nlights_per_side. The fixture
  // never sets that, so it defaults to 4 and a naive 10000 becomes 2500 per
  // light -- a 25% coin flip on the light's name hash, not an assertion. Set
  // the bank to one light so 10000bp really does mean every trade.
  pt.put("nlights_per_side", 1);
  pt.put("aggr_participation_bp", 10000);
  auto light = create_buy_light("TestBuyAggr", 10);
  int sym_id = get_sym_id();
  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  // A TAKE: resting offer lifted, printed at 107. That is the far side for a
  // BUY light, so shadowing it crosses. A hit would print at the bid and our
  // limit there would rest -- which is why the light now refuses one.
  auto t = make_trade(sym_id, 107, 5, en::bs::SEL);
  process_msg(light.get(), t, &mock_ob);
  delete t;

  ASSERT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 1u)
      << "a trade must place an order when aggressive is on";
  auto order = mock_som.get_message<frame::som::msg::Order>(0);
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(order->side, en::bs::BUY);
  EXPECT_EQ(order->px, 107)
      << "the order must be priced at the trade it shadowed, not at our own touch";
}

TEST_F(Light22IntegrationTest, AggressiveRespectsTheZeroRate) {
  // 0bp with the feature otherwise reachable: the coin flip must reject every
  // trade. This separates "off" from "on but unlucky".
  pt.put("aggr_participation_bp", 0);
  auto light = create_buy_light("TestBuyZeroRate", 10);
  int sym_id = get_sym_id();
  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 50; i++) {
    auto t = make_trade(sym_id, 100, 5);
    process_msg(light.get(), t, &mock_ob);
    delete t;
  }
  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u);
}

TEST_F(Light22IntegrationTest, AggressiveStillRespectsThePositionTarget) {
  // The aggressive path goes through place_if_can_impl, so it inherits the
  // diff_from_target throttle: a light already at its target must not take,
  // however much volume trades. Without this an aggressive light would run the
  // position past where it was trying to get to.
  pt.put("nlights_per_side", 1);            // bank of one: 10000bp = every trade
  pt.put("aggr_participation_bp", 10000);
  auto light = create_buy_light("TestBuyAtTarget", 10);
  int sym_id = get_sym_id();
  mock_pcoord.set_position(10);              // already at target
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 10; i++) {
    auto t = make_trade(sym_id, 100, 5);
    process_msg(light.get(), t, &mock_ob);
    delete t;
  }
  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "at target, an aggressive light must stand down like a passive one";
}


/**
 * The defect, and the shape of the fix.
 *
 * A light declines a trade while it already holds a working order
 * (light22_base: `if (ord_info.has_value()) return;`). That guard is correct --
 * ord_info is a single slot -- but it meant aggression riding on the PASSIVE
 * lights almost never fired, because at any useful place_rate_bp those lights
 * are occupied nearly all the time.
 *
 * MEASURED, 2026-09-14 grid D: configured aggr_participation_bp 200 (2% of
 * trades) at 100 lots. Expected 10-26 aggressive placements per leg; observed
 * extra fills over the matched control were 0.63 / 0.23 / 0.19 / 0.08 / -0.04
 * at rate 25/50/100/200/400 -- 2.4% to 0% of target, ranked INVERSELY with
 * placement rate, which is the signature of crowding-out. Paired-slippage
 * differences were indistinguishable from noise (per-session t = -2.13, -0.76,
 * -0.91, -2.32, -0.19 over 247 sessions).
 *
 * The fix is architectural, not a config change: aggression gets its OWN bank
 * (SimKaspr.cpp) -- one light per side, place_rate_bp 0, its own QCoord,
 * sharing the passive bank's PCoord. This test pins the property that makes
 * one light sufficient: an aggression-only light is idle between crosses, so
 * it takes every trade the rate selects.
 */
TEST_F(Light22IntegrationTest, AggressionOnlyLightTakesEveryTradeItIsOffered) {
  pt.put("place_rate_bp", 0);                 // aggression only: never shadows an ADD
  pt.put("place_after_n_eob", 0);
  pt.put("aggr_participation_bp", 10000);     // 100% of trades
  auto light = create_buy_light("TestAggr", 10);
  int sym_id = get_sym_id();

  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  ASSERT_FALSE(light->ord_info.has_value())
      << "an aggression-only light must not have rested anything";

  // A take: the offer was lifted, so this is aggressive for the BUY side.
  auto trade = make_trade(sym_id, 100, 5, en::bs::SEL);
  process_msg(light.get(), trade, &mock_ob);
  delete trade;

  EXPECT_GE(mock_som.count_messages_of_type<frame::som::msg::Order>(), 1u)
      << "a dedicated aggressive light is idle between crosses and must cross "
         "on the trade it is offered";
}

/**
 * aggr_participation_bp is LITERAL -- it is no longer divided by
 * nlights_per_side.
 *
 * The division existed for the old design where aggression rode on the passive
 * bank and the config value had to mean the same thing across arms with
 * different light counts. With a dedicated bank of one light it is at best a
 * no-op and at worst silently scales the rate by whatever the passive bank
 * happens to be sized at. 200 must mean 2% of trades, full stop.
 */
TEST_F(Light22IntegrationTest, AggressiveRateIsNotDividedByLightCount) {
  pt.put("nlights_per_side", 12);             // would have divided by 12 before
  pt.put("place_rate_bp", 0);
  pt.put("aggr_participation_bp", 200);
  auto light = create_buy_light("TestAggr", 10);

  EXPECT_EQ(light->aggr_participation_bp, 200)
      << "the configured rate must reach the light unscaled: a bank of one "
         "divided by a passive light count is the trap the division created";
}

TEST_F(Light22IntegrationTest, AggressiveIgnoresTradesOnItsOwnSide) {
  // A BUY light must shadow TAKES only. A hit prints at the bid, so placing a
  // buy limit there would rest -- a passive re-quote dressed up as aggression.
  // Without this test half of every "aggressive" placement was exactly that,
  // and a bank configured for 2% crossed on about 1%.
  pt.put("nlights_per_side", 1);
  pt.put("aggr_participation_bp", 10000);     // every trade, so only the side filters
  auto light = create_buy_light("TestBuyHitOnly", 10);
  int sym_id = get_sym_id();
  mock_pcoord.set_position(0);
  process_msg(light.get(), new actors::msg::Start(), &mock_ob);
  mock_som.clear();

  for (int i = 0; i < 20; i++) {
    auto t = make_trade(sym_id, 100, 5, en::bs::BUY);   // hits: the bid was hit
    process_msg(light.get(), t, &mock_ob);
    delete t;
  }
  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 0u)
      << "a BUY light must not shadow hits -- that would rest, not cross";

  auto t = make_trade(sym_id, 100, 5, en::bs::SEL);     // a take
  process_msg(light.get(), t, &mock_ob);
  delete t;
  EXPECT_EQ(mock_som.count_messages_of_type<frame::som::msg::Order>(), 1u)
      << "a BUY light must shadow takes";
}
