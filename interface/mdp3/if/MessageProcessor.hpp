#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "mdp3/mbo_if.hpp"

actor_ptr create_MessageProcessor(
    const std::string &_chan_nam,
    actor_ptr _recovery_processor,
    actor_ptr _decoder, // the DataDecoder actor (built in kaspr)
    bool _dorecovery,
    bool _recoveryonstart
#ifdef MDP3_VERIFY_TEE
    // Dual-path verification tee; null = off. The parameter itself only exists
    // under VERIFY_TEE -- see mk_kaspr/glob_begin.mk. The define MUST be global:
    // this declaration, its definition in libmdp3, and the kaspr call site are
    // three translation units that have to agree on the signature.
    , actor_ptr _decoder_shadow = nullptr
#endif
    );
