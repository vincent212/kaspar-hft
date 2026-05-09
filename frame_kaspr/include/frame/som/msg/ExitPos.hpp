#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/e_names.hpp"

namespace frame
{
    namespace som
    {
        namespace msg
        {
            struct ExitPos : public actors::Message_N<44>
            {
                en::x venu;
                ExitPos(en::x _venu) :  venu(_venu) {}
            };
        }
    }
}
