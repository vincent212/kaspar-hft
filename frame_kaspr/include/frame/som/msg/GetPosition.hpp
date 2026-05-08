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
   * GetPosition - SOM queries DB for current position on startup
   * Sent by: SOM (on startup)
   * Received by: DB actor
   */
  struct GetPosition : public actors::Message_N<260>, public actors::MemoryPool<GetPosition>
  {
    int sym;           // Symbol ID (0=ES, 1=NQ, etc.)
    en::trader trader; // Trader ID (SIMULATOR, etc.)

    GetPosition(int s, en::trader t)
      : sym(s), trader(t)
    {}

    virtual ~GetPosition() {}
  };
}
