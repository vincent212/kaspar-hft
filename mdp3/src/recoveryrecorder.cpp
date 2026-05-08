
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mdp3/act/DataRecoveryRecorder.hpp"

actor_ptr create_DataRecoveryRecorder(
    mdp3::feed_handler_if *_cb,
    in_port_t _port_dr,
    const char *_group_dr,
    const char *_interface)
{
    return new mdp3::DataRecoveryRecorder(
        _cb,
        _port_dr,
        _group_dr,
        _interface);
}
