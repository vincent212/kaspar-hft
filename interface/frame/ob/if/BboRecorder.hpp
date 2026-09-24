#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <vector>

#include "actors/Actor.hpp"

// Create a BboRecorder actor that writes a gzipped CSV of BBBOChg events to
// _outfilename. _session_date is a free-form label (e.g. "20250310") logged at
// start-up; the row payload itself carries only tx_time,venue,sym,best_bid,
// best_ask -- see frame/ob/act/BboRecorder.hpp for the schema. _obs is the
// list of OB actors this recorder subscribes to on Start via BBBOSub.
actors::Actor* create_BboRecorder(
    const std::string &_outfilename,
    const std::string &_session_date,
    const std::vector<actors::Actor*> &_obs);
