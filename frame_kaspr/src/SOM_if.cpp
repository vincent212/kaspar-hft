/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KASPR: SOM factory function

#include "frame/som/act/SOM.hpp"
#include "interface/som/if/SOM.hpp"

actor_ptr create_SOM(
    en::x venue,
    actor_ptr db,
    const boost::property_tree::ptree &pt,
    const std::vector<std::vector<actor_ptr>> &order_books,
    bool sim_mode,
    bool spin,
    bool reset_positions,
    actor_ptr fill_subscriber)
{
    return new frame::som::act::SOM(
        venue,
        db,
        pt,
        order_books,
        sim_mode,
        spin,
        reset_positions,
        fill_subscriber);
}
