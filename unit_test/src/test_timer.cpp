/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for Timer Actor
 *
 * These tests verify that Timer:
 * - Subscribes to OB on Start
 * - Advances time from EndOfBurst2.txtim
 * - Fires absolute alarms (at specific times like 9:30 AM)
 * - Fires relative alarms (after N seconds)
 * - Supports periodic alarms (repeat every N seconds)
 * - Maintains correct timer_id in alarm responses
 *
 * Message Flow:
 *   [Start] → Timer → [Subscribe] → OB
 *   [EndOfBurst2(txtim)] → Timer (advances internal clock)
 *   [AlarmClockSub] → Timer (registers alarm)
 *   Timer → [Alarm] → subscriber (when time condition met)
 *
 * Note: Timer has a static singleton (the_timer) that prevents multiple
 * instantiations. We use a single shared Timer across all tests and
 * reset state by clearing messages manually.
 */

#include <gtest/gtest.h>
#include "unit_test/MockActor.hpp"
#include "unit_test/FakeMarketData.hpp"
#include "unit_test/TestHelper.hpp"
#include "frame/mtim/act/Timer.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "frame/ob/msg/EndOfBurst2.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include "actors/msg/Start.hpp"

using namespace unit_test;
using namespace frame::mtim::act;
using namespace frame::mtim::msg;
using namespace frame::ob::msg;
using namespace frame::mda::msg;

/**
 * Shared Timer fixture to handle singleton constraint
 *
 * Timer's singleton pattern prevents creating multiple instances.
 * We create one Timer at first use and reuse it across all tests.
 */
class TimerTest : public ::testing::Test {
protected:
  // Shared fixtures across all tests
  static MockOB* shared_mock_ob;
  static Timer* shared_timer;
  static bool initialized;

  MockActor mock_subscriber{"Subscriber"};

  static void InitializeOnce() {
    if (!initialized) {
      shared_mock_ob = new MockOB("MockOB");
      shared_timer = new Timer(shared_mock_ob);
      initialized = true;
    }
  }

  void SetUp() override {
    InitializeOnce();
    shared_mock_ob->clear();
    mock_subscriber.clear();
    // Clear alarms from previous tests to ensure test isolation
    shared_timer->clear_alarms_for_test();
  }

  void process_msg(actors::Actor* actor, const actors::Message* msg,
                   actors::Actor* sender = nullptr) {
    TestHelper::invoke_handler(actor, msg, sender);
  }

  // Accessor for the shared timer
  Timer* timer() { return shared_timer; }
  MockOB& mock_ob() { return *shared_mock_ob; }
};

// Static member definitions
MockOB* TimerTest::shared_mock_ob = nullptr;
Timer* TimerTest::shared_timer = nullptr;
bool TimerTest::initialized = false;

/**
 * Test 4.1: StartSubscribesToOB
 *
 * SETUP:  Timer created with book=OB
 * INPUT:  [Start] → Timer
 * OUTPUT: Timer → [Subscribe(priority=LOW)] → OB
 * VERIFY: Timer subscribes to market data with LOW priority
 */
TEST_F(TimerTest, StartSubscribesToOB) {
  // Send Start
  actors::msg::Start start_msg;
  process_msg(timer(), &start_msg, nullptr);

  // OB should receive Subscribe
  EXPECT_EQ(mock_ob().message_count(), 1)
      << "OB should receive one Subscribe message";

  auto sub = mock_ob().get_message<Subscribe>(0);
  ASSERT_NE(sub, nullptr) << "Message should be Subscribe";
  EXPECT_EQ(sub->prio, Subscribe::LOW)
      << "Timer subscribes with LOW priority";
}

/**
 * Test 4.2: AbsoluteAlarmFires
 *
 * SETUP:  Timer time = 9:00 AM (via EndOfBurst2)
 * INPUT:  [AlarmClockSub(h=9,m=30,timer_id=42)] → Timer (sender=TestActor)
 *         [EndOfBurst2(txtim=9:30:00)] → Timer
 * OUTPUT: Timer → [Alarm(timer_id=42,rr=ALARMCLOCK)] → TestActor
 * VERIFY: Alarm fires exactly when time reaches 9:30
 *         Alarm contains correct timer_id and current time
 */
TEST_F(TimerTest, AbsoluteAlarmFires) {
  // Initialize timer's time to 9:00 AM via EOB2
  auto init_eob = make_eob2_at(2024, 1, 15, 9, 0, 0);
  process_msg(timer(), init_eob, &mock_ob());

  // Subscribe to alarm at 9:30 AM
  AlarmClockSub sub(9, 30, 0, 0, 42);  // h=9, m=30, s=0, ms=0, timer_id=42
  process_msg(timer(), &sub, &mock_subscriber);

  // Advance time to 9:30 - alarm should fire
  auto eob_930 = make_eob2_at(2024, 1, 15, 9, 30, 0);
  process_msg(timer(), eob_930, &mock_ob());

  // Subscriber should receive alarm
  EXPECT_EQ(mock_subscriber.message_count(), 1)
      << "Subscriber should receive alarm at 9:30";

  auto alarm = mock_subscriber.get_message<Alarm>(0);
  ASSERT_NE(alarm, nullptr);
  EXPECT_EQ(alarm->timer_id, 42) << "Alarm should have timer_id=42";
  EXPECT_EQ(alarm->rr, Alarm::ALARMCLOCK) << "Reply reason should be ALARMCLOCK";

  delete init_eob;
  delete eob_930;
}

/**
 * Test 4.3: AbsoluteAlarmNotFiresEarly
 *
 * SETUP:  Timer time = 9:00 AM
 * INPUT:  [AlarmClockSub(h=9,m=30,timer_id=42)] → Timer
 *         [EndOfBurst2(txtim=9:29:00)] → Timer
 * OUTPUT: (none - alarm not fired yet)
 * VERIFY: Alarm waits until scheduled time
 */
TEST_F(TimerTest, AbsoluteAlarmNotFiresEarly) {
  // Initialize timer's time to 9:00 AM
  auto init_eob = make_eob2_at(2024, 1, 15, 9, 0, 0);
  process_msg(timer(), init_eob, &mock_ob());

  // Subscribe to alarm at 9:30 AM
  AlarmClockSub sub(9, 30, 0, 0, 43);  // Use different timer_id to avoid conflict
  process_msg(timer(), &sub, &mock_subscriber);

  // Advance time to 9:29 - alarm should NOT fire yet
  auto eob_929 = make_eob2_at(2024, 1, 15, 9, 29, 0);
  process_msg(timer(), eob_929, &mock_ob());

  // Subscriber should NOT receive alarm yet
  EXPECT_EQ(mock_subscriber.message_count(), 0)
      << "Alarm should not fire before scheduled time";

  delete init_eob;
  delete eob_929;
}

/**
 * Test 4.4: RelativeAlarmFires
 *
 * SETUP:  Timer time = 9:00:00
 * INPUT:  [AlarmClockSub(s=300,ms=0,timer_id=99,periodic=false)] → Timer
 *         [EndOfBurst2(txtim=9:04:00)] → Timer  (4 min - not yet)
 *         [EndOfBurst2(txtim=9:05:00)] → Timer  (5 min - fires!)
 * OUTPUT: Alarm(timer_id=99) sent after second EndOfBurst2
 * VERIFY: Relative time calculated from registration time
 */
TEST_F(TimerTest, RelativeAlarmFires) {
  // Initialize timer's time to 9:00:00
  auto init_eob = make_eob2_at(2024, 1, 15, 9, 0, 0);
  process_msg(timer(), init_eob, &mock_ob());

  // Subscribe to relative alarm: 300 seconds (5 minutes) from now
  AlarmClockSub sub(300, 0, 99, false);  // s=300, ms=0, timer_id=99, periodic=false
  process_msg(timer(), &sub, &mock_subscriber);

  // Advance time to 9:04 - should NOT fire yet (only 4 min elapsed)
  auto eob_904 = make_eob2_at(2024, 1, 15, 9, 4, 0);
  process_msg(timer(), eob_904, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 0)
      << "Relative alarm should not fire before 5 minutes";

  // Advance time to 9:05 - should fire (5 min elapsed)
  auto eob_905 = make_eob2_at(2024, 1, 15, 9, 5, 0);
  process_msg(timer(), eob_905, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 1)
      << "Relative alarm should fire after 5 minutes";

  auto alarm = mock_subscriber.get_message<Alarm>(0);
  ASSERT_NE(alarm, nullptr);
  EXPECT_EQ(alarm->timer_id, 99);

  delete init_eob;
  delete eob_904;
  delete eob_905;
}

/**
 * Test 4.5: PeriodicAlarmRepeats
 *
 * SETUP:  Timer time = 9:00:00
 * INPUT:  [AlarmClockSub(s=60,periodic=true,timer_id=77)] → Timer
 *         [EndOfBurst2(txtim=9:01:00)] → Timer (first fires)
 *         [EndOfBurst2(txtim=9:02:00)] → Timer (reschedule to 9:03)
 *         [EndOfBurst2(txtim=9:03:00)] → Timer (second fires)
 *         [EndOfBurst2(txtim=9:04:00)] → Timer (reschedule to 9:05)
 *         [EndOfBurst2(txtim=9:05:00)] → Timer (third fires)
 * OUTPUT: Alarm(timer_id=77) sent 3 times (at 9:01, 9:03, 9:05)
 * VERIFY: Periodic alarm reschedules 60s from wake-up time check
 *         Same timer_id each time
 *
 * NOTE: Timer's periodic alarm reschedules from the time it's CHECKED after
 * firing, not from when it fired. This creates a "snooze" behavior.
 */
TEST_F(TimerTest, PeriodicAlarmRepeats) {
  // Initialize timer's time to 9:00:00
  auto init_eob = make_eob2_at(2024, 1, 15, 9, 0, 0);
  process_msg(timer(), init_eob, &mock_ob());

  // Subscribe to periodic alarm every 60 seconds
  AlarmClockSub sub(60, 0, 77, true);  // s=60, ms=0, timer_id=77, periodic=true
  process_msg(timer(), &sub, &mock_subscriber);

  // Advance to 9:01 - first alarm fires (registered at 9:00, wakes at 9:01)
  auto eob_901 = make_eob2_at(2024, 1, 15, 9, 1, 0);
  process_msg(timer(), eob_901, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 1) << "First periodic alarm at 9:01";

  // Advance to 9:02 - no alarm yet (rescheduled to 9:02+60s=9:03 on check)
  auto eob_902 = make_eob2_at(2024, 1, 15, 9, 2, 0);
  process_msg(timer(), eob_902, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 1) << "No alarm at 9:02 (next at 9:03)";

  // Advance to 9:03 - second alarm fires
  auto eob_903 = make_eob2_at(2024, 1, 15, 9, 3, 0);
  process_msg(timer(), eob_903, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 2) << "Second periodic alarm at 9:03";

  // Advance to 9:04 - no alarm (rescheduled to 9:05)
  auto eob_904 = make_eob2_at(2024, 1, 15, 9, 4, 0);
  process_msg(timer(), eob_904, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 2) << "No alarm at 9:04 (next at 9:05)";

  // Advance to 9:05 - third alarm fires
  auto eob_905 = make_eob2_at(2024, 1, 15, 9, 5, 0);
  process_msg(timer(), eob_905, &mock_ob());
  EXPECT_EQ(mock_subscriber.message_count(), 3) << "Third periodic alarm at 9:05";

  // All should have same timer_id
  for (size_t i = 0; i < 3; ++i) {
    auto alarm = mock_subscriber.get_message<Alarm>(i);
    ASSERT_NE(alarm, nullptr);
    EXPECT_EQ(alarm->timer_id, 77) << "Periodic alarm " << i << " should have timer_id=77";
  }

  delete init_eob;
  delete eob_901;
  delete eob_902;
  delete eob_903;
  delete eob_904;
  delete eob_905;
}

/**
 * Test 4.6: AlarmContainsCorrectTimerId
 *
 * SETUP:  Register two alarms with different IDs
 * INPUT:  [AlarmClockSub(h=9,m=15,timer_id=111)] → Timer
 *         [AlarmClockSub(h=9,m=30,timer_id=222)] → Timer
 *         [EndOfBurst2(txtim=9:15)] → Timer
 *         [EndOfBurst2(txtim=9:30)] → Timer
 * OUTPUT: First Alarm has timer_id=111
 *         Second Alarm has timer_id=222
 * VERIFY: Timer IDs distinguish different alarms
 */
TEST_F(TimerTest, AlarmContainsCorrectTimerId) {
  // Initialize timer's time to 9:00 AM
  auto init_eob = make_eob2_at(2024, 1, 15, 9, 0, 0);
  process_msg(timer(), init_eob, &mock_ob());

  // Register two alarms at different times
  AlarmClockSub sub1(9, 15, 0, 0, 111);  // 9:15, timer_id=111
  process_msg(timer(), &sub1, &mock_subscriber);

  AlarmClockSub sub2(9, 30, 0, 0, 222);  // 9:30, timer_id=222
  process_msg(timer(), &sub2, &mock_subscriber);

  // Advance to 9:15 - first alarm should fire
  auto eob_915 = make_eob2_at(2024, 1, 15, 9, 15, 0);
  process_msg(timer(), eob_915, &mock_ob());

  EXPECT_EQ(mock_subscriber.message_count(), 1);
  auto alarm1 = mock_subscriber.get_message<Alarm>(0);
  ASSERT_NE(alarm1, nullptr);
  EXPECT_EQ(alarm1->timer_id, 111) << "First alarm should have timer_id=111";

  // Advance to 9:30 - second alarm should fire
  auto eob_930 = make_eob2_at(2024, 1, 15, 9, 30, 0);
  process_msg(timer(), eob_930, &mock_ob());

  EXPECT_EQ(mock_subscriber.message_count(), 2);
  auto alarm2 = mock_subscriber.get_message<Alarm>(1);
  ASSERT_NE(alarm2, nullptr);
  EXPECT_EQ(alarm2->timer_id, 222) << "Second alarm should have timer_id=222";

  delete init_eob;
  delete eob_915;
  delete eob_930;
}
