#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
            struct GetPNL : public actors::MessageT<GetPNL>
            {
              int owner;
              GetPNL(int _owner) : owner(_owner) {}
            };
        }
    }
}
