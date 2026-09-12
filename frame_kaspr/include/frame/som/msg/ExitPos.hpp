#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
            struct ExitPos : public actors::MessageT<ExitPos>
            {
                en::x venu;
                ExitPos(en::x _venu) :  venu(_venu) {}
            };
        }
    }
}
