#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <netinet/in.h>
#include "actors/Actor.hpp"
#include "mdp3/mbo_if.hpp"

cfsmp create_DataRecoveryRecorder(
    mdp3::feed_handler_if *_cb,
    in_port_t _port_dr,
    const char *_group_dr,
    const char *_interface);