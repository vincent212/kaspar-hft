/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "positionman/act/PositionManager.hpp"
#include "interface/positionman/if/PositionManager.hpp"

actor_ptr create_PositionManager(
    const std::string& prefix,
    const std::map<std::string, light::PCoord*>& pcoords)
{
  return new positionman::PositionManager(prefix, pcoords);
}
