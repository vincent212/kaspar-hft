#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"

cfsmp craete_MsgBuf(
    const std::string &_chan_nam,
    bool spin,
    char _chan,
    cfsmp _msg_processor);