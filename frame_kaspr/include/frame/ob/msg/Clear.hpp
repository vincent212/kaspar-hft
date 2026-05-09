#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "chutil/Time.hpp"

namespace frame
{
  namespace ob
  {
    namespace msg
    {
      struct Clear : public  actors::Message_N<28>
      {
        int sym;
        Clear(int _sym)
            : sym(_sym) {}
      };
    }
  }
}