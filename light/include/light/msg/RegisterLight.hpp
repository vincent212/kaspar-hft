#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/Message.hpp"

namespace light::msg
{
  struct RegisterLight : public actors::MessageT<RegisterLight>
  {
    cfsmp light;
    RegisterLight(cfsmp _light)
        : light(_light)
    {
    }
  };

}
