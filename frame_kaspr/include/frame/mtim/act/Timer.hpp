#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <list>
#include <vector>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "frame/ob/msg/EndOfBurst2.hpp"
#include "frame/mtim/msg/TimeOutSub.hpp"
#include "frame/mtim/msg/AlarmClockSub.hpp"
#include "enum/e_names.hpp"

namespace frame
{
  namespace mtim
  {
    namespace act
    {
      class Timer : public actors::Actor
      {
      private:
        actor_ptr book;
        std::list<msg::TimeOutSub> timer_list;
        std::list<msg::AlarmClockSub> alarmList;
        chutil::Time currtim;

      public:

        const char* get_name() const { return "Timer"; }

        Timer(
          actor_ptr book
        );

      private:
        static Timer* the_timer;

      public:

        static Timer* get_instance() { return the_timer; }

        // wake up at a particular time
        static void set_alarm(
          int _h,
          int _m,
          int _s,
          int _ms,
          int _timer_id,
          actors::Actor* caller)
        {
          get_instance()->send(new msg::AlarmClockSub(_h, _m, _s, _ms, _timer_id), caller);
        }

        // wake up in a few s/ms
        static void set_timer(
          int _s,
          int _ms,
          int _timer_id,
          bool _periodic,
          actors::Actor* caller
        )
        {
          get_instance()->send(new msg::AlarmClockSub(_s, _ms, _timer_id, _periodic), caller);
        }

      public:
        // Message handlers
        void start_handler(const actors::msg::Start*) noexcept;
        void eob2_handler(const frame::ob::msg::EndOfBurst2*) noexcept;
        void timeout_sub_handler(const msg::TimeOutSub*) noexcept;
        void alarm_sub_handler(const msg::AlarmClockSub*) noexcept;

        // For testing: clear all registered alarms
        void clear_alarms_for_test() {
          alarmList.clear();
          timer_list.clear();
        }

      private:
        msg::Alarm* createDatMsg(msg::Alarm::reply_reason_t reply_reason, int timer_id);
        void processAlarmClock();
        inline void processTimeOuts(int volume = 1);

      };

    }
  }
}
