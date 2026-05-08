#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <time.h>

#include "actors/Message.hpp"
#include "frame/mtim/msg/Alarm.hpp"
#include "chutil/Time.hpp"

namespace frame
{
  namespace mtim
  {
    namespace msg
    {

      class AlarmClockSub : public actors::Message_N<20>
      {
        int h,m,s,ms;
        bool is_relative, adjusted;

      public:

        bool periodic;

        chutil::Time tim_to_wake_up;
        int timer_id; // set to the same id as was given in req
        uint sym;

        // wake up at particular time
        AlarmClockSub(int _h, int _m, int _s, int _ms, int _timer_id)
        {
          h = _h;
          m = _m;
          s = _s;
          ms= _ms;
          is_relative = false;
          timer_id = _timer_id;
          adjusted = false;
          sym = std::numeric_limits<uint>::max();
          periodic=false;
          ASSERT(h<24,"bad hour");
          ASSERT(m<60,"bad miniute");
          ASSERT(s<60,"bad second");
        }

        // wake up in a few seconds
        AlarmClockSub(int _s,
                      int _ms,
                      int _timer_id,
                      bool _periodic)
        {
          periodic=_periodic;
          s = _s;
          ms=_ms;
          is_relative = true;
          timer_id = _timer_id;
          adjusted = false;
          sym = std::numeric_limits<uint>::max();
        }

        inline void resetEvents()
        {
          adjusted=false;
        }

        //
        // shouldnt this adjustment be done in the constructor?
        //
        inline void adjustToCurrentTime(const chutil::Time &tim)
        {
          if (adjusted)
            return;
          adjusted = true;

          if (is_relative)
            tim_to_wake_up = tim + s*1000 + ms;
          else
            {
              int dt;
              // Check if scheduled time has already passed today (check hour, minute, AND second)
              if (tim.hour()>h ||
                  (tim.hour()==h && tim.minute()>m) ||
                  (tim.hour()==h && tim.minute()==m && tim.second()>=s))
                dt=chutil::Time::next_day(tim).date;
              else
                dt=tim.get_date();
              tim_to_wake_up=chutil::Time(dt,0)+(h*3600+m*60+s)*1000+ms;
            }
        }

        virtual ~AlarmClockSub() {}

      };

    }
  }
}
