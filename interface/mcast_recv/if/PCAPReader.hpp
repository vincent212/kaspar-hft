#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "mcast_recv/act/PCAPReader.hpp"

actor_ptr create_PCAPReader_32_0(
    const std::string &_chan_nam,
    actor_ptr _msg_processor,
    const std::string &_pcap_filename,
    bool _big_endian = false
    )
{
    return new mcast_recv::PCAPReader<uint32_t, 0>(
        _chan_nam,
        _msg_processor,
        _pcap_filename,
        _big_endian
    );
}

actor_ptr create_PCAPReader_64_10(
    const std::string &_chan_nam,
    actor_ptr _msg_processor,
    const std::string &_pcap_filename,
    bool _big_endian = false
    )
{
    return new mcast_recv::PCAPReader<uint64_t, 10>(
        _chan_nam,
        _msg_processor,
        _pcap_filename,
        _big_endian
    );
}
