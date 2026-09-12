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
      struct CheckRes : public actors::MessageT<CheckRes>
      {
        CheckRes(bool _res)
          :res(_res)
        {}
        bool res;
      };
    }
  }
}
