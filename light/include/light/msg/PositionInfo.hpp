#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/e_names.hpp"
#include <sstream>

namespace light
{
  namespace msg
  {
    // Message sent from MarketMaker to all lights after each fill
    struct PositionInfo : public actors::Message_N<255>, public actors::MemoryPool<PositionInfo> {
        int position;      // always positive (magnitude), use current_side for direction
        int last_px;
        en::bs current_side;  // BUY = long, SEL = short

        PositionInfo() : position(0), last_px(0), current_side(en::bs::BUY) {}

        PositionInfo(int pos, int lpx, en::bs side)
            : position(pos), last_px(lpx), current_side(side) {
            // Ensure position is always positive (magnitude)
            if (position < 0) {
                position = -position;
            }
        }

        std::string to_string() const {
            std::ostringstream oss;
            oss << "Position: " << position
                << ", Side: " << en::to_string(current_side)
                << ", Last_px: " << last_px;
            return oss.str();
        }
    };
  }
}
