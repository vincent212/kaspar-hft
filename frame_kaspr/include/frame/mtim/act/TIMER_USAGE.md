<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Timer Actor Usage

The Timer actor provides time-based scheduling for other actors.

## Location
`frame_kaspr/include/frame/mtim/act/Timer.hpp`

## Messages

### AlarmClockSub (Request)
`frame/mtim/msg/AlarmClockSub.hpp`

Two constructors for different use cases:

**Absolute time (wake up at specific time of day):**
```cpp
// Wake up at 9:00 AM
timer->send(new frame::mtim::msg::AlarmClockSub(9, 0, 0, 0, MY_TIMER_ID));
// Parameters: hour, minute, second, millisecond, timer_id
```

**Relative time (wake up after delay):**
```cpp
// Wake up in 15 minutes (900 seconds)
timer->send(new frame::mtim::msg::AlarmClockSub(900, 0, MY_TIMER_ID, false));
// Parameters: seconds, milliseconds, timer_id, periodic

// Wake up every 15 minutes (periodic)
timer->send(new frame::mtim::msg::AlarmClockSub(900, 0, MY_TIMER_ID, true));
// Parameters: seconds, milliseconds, timer_id, periodic=true
```

### Alarm (Response)
`frame/mtim/msg/Alarm.hpp`

When timer fires, you receive an Alarm message:
```cpp
void alarm_handler(const frame::mtim::msg::Alarm* m) noexcept {
  switch (m->timer_id) {
    case MY_TIMER_ID:
      // Timer fired
      // m->currtim contains the current time
      // m->rr is reply_reason_t (TIME_OUT or ALARMCLOCK)
      break;
    default:
      ERR("unknown timer_id %d", m->timer_id);
  }
}
```

### Static Methods on Timer

```cpp
// Schedule alarm at absolute time
Timer::set_alarm(9, 0, 0, 0, MY_TIMER_ID, this);

// Schedule relative timer
Timer::set_timer(900, 0, MY_TIMER_ID, false, this);
```

## Example

```cpp
class MyActor : public actors::Actor {
  cfsmp timer;
  static constexpr int TIMER_WAKEUP = 1;

public:
  MyActor(cfsmp _timer) : timer(_timer) {
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(frame::mtim::msg::Alarm, alarm_handler);
  }

  void start_handler(const actors::msg::Start*) noexcept {
    // Wake up at 9:00 AM
    timer->send(new frame::mtim::msg::AlarmClockSub(9, 0, 0, 0, TIMER_WAKEUP));
  }

  void alarm_handler(const frame::mtim::msg::Alarm* m) noexcept {
    switch (m->timer_id) {
      case TIMER_WAKEUP:
        log_inf("It's 9:00 AM!");
        // Schedule again for tomorrow or use relative time for next event
        break;
      default:
        ERR("unknown timer_id %d", m->timer_id);
    }
  }
};
```

## IMPORTANT

In simulation mode (using BFA), all timestamps come from market data.
Do NOT calculate current time - just schedule alarms at absolute times.
The Timer will fire when the market data timestamps reach those times.
