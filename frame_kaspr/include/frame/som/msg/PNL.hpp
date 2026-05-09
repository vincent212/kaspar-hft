#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

#include "frame/pos/Position.hpp"

namespace frame
{
    namespace som
    {
        namespace msg
        {
            struct PNL : public  actors::Message_N<39>
            {
                std::vector<pos::Position> pos;
                std::vector<int> pnl;

                PNL(
                    const std::vector<pos::Position> &_pos,
                    const std::vector<int> &_pnl) : pos(_pos),
                                                    pnl(_pnl)
                {
                }
            };
        }
    }
}
