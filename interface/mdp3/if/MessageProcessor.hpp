#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "mdp3/mbo_if.hpp"

actor_ptr create_MessageProcessor(
    const std::string &_chan_nam,
    actor_ptr _recovery_processor,
    mdp3::feed_handler_if *_cb,
    bool _dorecovery,
    bool _recoveryonstart,
    bool _disable_mbo,
    uint32_t _max_mbp_level,
    bool _debug);
