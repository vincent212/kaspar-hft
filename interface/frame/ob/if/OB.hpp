#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"
#include <boost/property_tree/ptree.hpp>

cfsmp create_OB(
    cfsmp binrec,
    bool spin,
    bool do_cross_check,
    actors::Manager *man,
    uint _sym,
    boost::property_tree::ptree _pt = boost::property_tree::ptree()
);

void ob_set_debug(cfsmp ob, uint64_t start_debug = 0);

void ob_set_delay(cfsmp ob, int _d);