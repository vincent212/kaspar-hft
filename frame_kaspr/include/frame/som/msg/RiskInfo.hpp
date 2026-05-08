#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
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
            struct RiskInfo : public actors::Message_N<45>
            {
                double pnl;
                en::x venu;
                RiskInfo(double _pnl, en::x _venu) : pnl(_pnl), venu(_venu) {}
            };
        }
    }
}
