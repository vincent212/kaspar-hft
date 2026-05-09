#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"

actor_ptr create_MTD(
    std::string _name,
    const std::map<en::x, actor_ptr> & _som,
    const std::vector<std::vector<actor_ptr>> &_obs);
