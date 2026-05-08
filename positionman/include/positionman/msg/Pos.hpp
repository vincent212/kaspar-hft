#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <string>

namespace positionman::msg
{
  struct Pos : public actors::Message_N<163>
  {
    std::string instrument;
    int position;
    Pos(const std::string& instrument, int pos) : instrument(instrument), position(pos) {}
  };
}
