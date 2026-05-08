#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace frame
{
    namespace som
    {
        namespace msg
        {
            struct GetPNL : public actors::Message_N<37>
            {
              int owner;
              GetPNL(int _owner) : owner(_owner) {}
            };
        }
    }
}
