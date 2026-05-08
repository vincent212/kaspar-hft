#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "enum/trader.hpp"

namespace frame::som::msg
{
  /**
   * PositionResponse - DB replies with current position from database
   * Sent by: DB actor
   * Received by: SOM
   */
  struct PositionResponse : public actors::Message_N<261>, public actors::MemoryPool<PositionResponse>
  {
    int sym;           // Symbol ID
    en::trader trader; // Trader ID
    int position;      // Net position (+long, -short)

    PositionResponse(int s, en::trader t, int pos)
      : sym(s), trader(t), position(pos)
    {}

    virtual ~PositionResponse() {}
  };
}
