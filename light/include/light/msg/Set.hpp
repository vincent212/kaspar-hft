#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace light
{
  namespace msg
  {
    struct Set : public  actors::Message_N<70>
    {
      enum action_t
      {
        TRADING_ON,
        TRADING_OFF,
        DUMP,
        LEV_ORDERS_MAX,
        TARGET_POS,
        SUBSCRIBEONLY,
      };
      action_t key;
      double dval;
      Set(action_t _key, double _dval = 0)
          : key(_key), dval(_dval)
      {
      }
    };
  }
}
