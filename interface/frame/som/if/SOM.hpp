#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <boost/property_tree/ptree.hpp>
#include "actors/Actor.hpp"
#include "enum/e_names.hpp"

actor_ptr create_SOM(
    en::x venue,
    actor_ptr _db,
    const boost::property_tree::ptree &pt,
    const std::vector<std::vector<actor_ptr>> &_order_books,
    bool _sim_mode = true,
    bool _spin = false
    );
