#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/buy_sell.hpp"
#include <string>

namespace positionman::msg
{
  struct AddToPos : public actors::Message_N<160>
  {
    std::string instrument;
    en::bs side;
    double sz;

    AddToPos() : instrument(""), side(en::bs::BUY), sz(0.0) {}
    AddToPos(const std::string& instrument, en::bs side, double sz)
        : instrument(instrument), side(side), sz(sz) {}
  };
}
