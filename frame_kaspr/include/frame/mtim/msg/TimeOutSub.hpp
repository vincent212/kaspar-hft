#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace frame
{

  namespace mtim
  {

    namespace msg
    {

      class TimeOutSub : public  actors::Message_N<21>
      {
      public:

        TimeOutSub(
          int _num_events,
          bool _periodic,
          int _timer_id = 0)
        {
          num_events = _num_events;
          timer_id = _timer_id;
          periodic = _periodic;
          orig_events = num_events;
          ERR("do not use");
        }

        int num_events, orig_events;
        int timer_id; // reply with this
        bool periodic;

        void resetEvents()
        {
          num_events += orig_events;
        }

      };

    }

  }

}
