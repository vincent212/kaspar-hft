#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <netinet/in.h>
#include "actors/Actor.hpp"

actor_ptr create_SocketReader_32_0(
    const std::string &_chan_nam,
    bool _blocksock,
    const char _chan,
    actor_ptr _msg_processor,
    in_port_t _port,
    const char *_group,
    const char *_interface,
    const char *_desc,
    bool _big_endian = false
    );

actor_ptr create_SocketReader_64_10(
    const std::string &_chan_nam,
    bool _blocksock,
    const char _chan,
    actor_ptr _msg_processor,
    in_port_t _port,
    const char *_group,
    const char *_interface,
    const char *_desc,
    bool _big_endian = false
    );
