#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace frame
{
  namespace ob
  {
    namespace msg
    {
      struct GapDetected : public  actors::Message_N<73>
      {
        int sym;
        GapDetected(int _sym)
            : sym(_sym) {}
      };
    }
  }
}