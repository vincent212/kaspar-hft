#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"

namespace frame::som::msg
{
  /**
   * ResetPositions - Clear all positions in database (for --reset-positions flag)
   * Sent by: SOM (on startup if reset_positions=true)
   * Received by: DB actor
   */
  struct ResetPositions : public actors::Message_N<263>, public actors::MemoryPool<ResetPositions>
  {
    ResetPositions() {}

    virtual ~ResetPositions() {}
  };
}
