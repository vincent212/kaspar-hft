#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "enum/trader.hpp"
#include "enum/buy_sell.hpp"

namespace frame::som::msg
{
  /**
   * UpdatePosition - SOM notifies DB of position change after fill
   * Sent by: SOM (on fill)
   * Received by: DB actor
   */
  struct UpdatePosition : public actors::Message_N<262>, public actors::MemoryPool<UpdatePosition>
  {
    int sym;           // Symbol ID
    en::trader trader; // Trader ID
    en::bs side;       // BUY or SEL
    int sz;            // Fill size

    UpdatePosition(int s, en::trader t, en::bs sd, int size)
      : sym(s), trader(t), side(sd), sz(size)
    {}

    virtual ~UpdatePosition() {}
  };
}
