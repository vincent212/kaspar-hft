#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "chutil/Time.hpp"

namespace frame
{
    namespace mtim
    {
        namespace msg
        {
            class Alarm : public actors::Message_N<19>
            {
            public:

                typedef enum {TIME_OUT, ALARMCLOCK} reply_reason_t;

                Alarm(reply_reason_t _rr=TIME_OUT)
                {
                    rr = _rr;
                    timer_id = 0;
                    sym=std::numeric_limits<uint>::max();
                }

                reply_reason_t rr;
                int timer_id;
                chutil::Time currtim;
                uint sym;

            };

        }

    }

}
