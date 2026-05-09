#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/Message.hpp"

namespace light::msg
{
  struct RegisterLight : public actors::Message_N<183>
  {
    cfsmp light;
    RegisterLight(cfsmp _light)
        : light(_light)
    {
    }
  };

}
