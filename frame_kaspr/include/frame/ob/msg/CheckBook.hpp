#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/e_names.hpp"

namespace frame
{
  namespace ob
  {
    namespace msg
    {
      struct CheckBook : public  actors::Message_N<25>
      {
        CheckBook(uint _sym, en::bs _side, int _px, uint32_t _sz)
          :sym(_sym), side(_side), px(_px), sz(_sz)
        {}
        uint sym;
        int px;
        uint32_t sz;
        en::bs side;
      };
    }
  }
}
