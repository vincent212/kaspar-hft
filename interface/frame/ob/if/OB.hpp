#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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

// Modelled latency to the matching engine, microseconds. cancel_us < 0 means
// "same as order_us"; it may not be smaller, since a cancel is never faster
// than a new order.
void ob_set_delay(cfsmp ob, int order_us, int cancel_us = -1);
// Inbound feed latency, exchange -> us, in microseconds. Separate from
// ob_set_delay because the two legs are independent: a colocated trader has a
// short outbound hop and still pays the feed's own propagation and handling.
// 0 (the default) publishes market data immediately, exactly as before.
void ob_set_feed_delay(cfsmp ob, int feed_us);