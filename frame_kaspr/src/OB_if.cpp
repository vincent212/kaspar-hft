/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KASPR: OB factory function

#include "frame/ob/act/OB.hpp"
#include "interface/frame/ob/if/OB.hpp"

actor_ptr create_OB(
    actor_ptr binrec,
    [[maybe_unused]] bool spin,
    bool do_cross_check,
    actors::Manager *man,
    uint _sym,
    boost::property_tree::ptree _pt)
{
    // OB's constructor takes (binrec, do_cross_check, man, sym, pt) — it has no
    // `spin` parameter. The call previously passed `spin` into the
    // do_cross_check slot, so OB::do_cross_check was driven by `spin` and the
    // caller's do_cross_check was dropped. Forward do_cross_check as intended;
    // `spin` is unused here.
    return new frame::ob::act::OB(binrec, do_cross_check, man, _sym, _pt);
}

void ob_set_debug(actor_ptr ob, uint64_t start_debug)
{
    auto ob_ptr = dynamic_cast<frame::ob::act::OB*>(ob);
    if (ob_ptr) {
        ob_ptr->set_debug(start_debug);
    }
}

void ob_set_delay(actor_ptr ob, int _d)
{
    auto ob_ptr = dynamic_cast<frame::ob::act::OB*>(ob);
    if (ob_ptr) {
        ob_ptr->set_delay(_d);
    }
}
