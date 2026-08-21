#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "mcast_recv/act/PCAPReader.hpp"

actor_ptr create_PCAPReader_32_0(
    const std::string &_chan_nam,
    actor_ptr _msg_processor,
    const std::string &_pcap_filename,
    bool _big_endian = false,
    const mcast_recv::TrailerSpec &_trailer_spec = mcast_recv::TrailerSpec{}
    )
{
    return new mcast_recv::PCAPReader<uint32_t, 0>(
        _chan_nam,
        _msg_processor,
        _pcap_filename,
        _big_endian,
        _trailer_spec
    );
}

actor_ptr create_PCAPReader_64_10(
    const std::string &_chan_nam,
    actor_ptr _msg_processor,
    const std::string &_pcap_filename,
    bool _big_endian = false,
    const mcast_recv::TrailerSpec &_trailer_spec = mcast_recv::TrailerSpec{}
    )
{
    return new mcast_recv::PCAPReader<uint64_t, 10>(
        _chan_nam,
        _msg_processor,
        _pcap_filename,
        _big_endian,
        _trailer_spec
    );
}
