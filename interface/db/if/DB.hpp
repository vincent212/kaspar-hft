#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"
#include <map>
#include <string>

actor_ptr create_DB(
    en::x mmVenue = en::x::UNI,
    const std::vector<std::vector<actor_ptr>> &order_books = {},
    actor_ptr som = nullptr);
