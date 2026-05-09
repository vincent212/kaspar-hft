#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace frame::som::msg
{
    struct UnStash : public
         actors::Message_N<51>
    {
        uint64_t ts;
        UnStash(uint64_t _ts) : ts(_ts)
        {
        }
    };
}
