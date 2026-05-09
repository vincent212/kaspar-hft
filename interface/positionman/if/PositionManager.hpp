#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "light/qcoord.hpp"
#include <map>
#include <string>

actor_ptr create_PositionManager(
    const std::string& prefix,
    const std::map<std::string, light::PCoord*>& pcoords);
