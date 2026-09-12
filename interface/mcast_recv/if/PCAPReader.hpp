#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include <utility>
#include <vector>

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

// Filtered variant for the Databento "consolidated" pcap layout where every
// CME channel is muxed into the same file. The filter is a list of pairs:
//   dst_ip — network byte order uint32_t (e.g. from inet_pton); 0 = wildcard
//            (any IP) for the paired port. See the filtered PCAPReader ctor
//            for why callers should not use the wildcard.
//   dst_port — HOST byte order uint16_t. The reader ntohs's the packet's
//              dst_port before comparing, so the filter side stays in host
//              order; do NOT htons() values you pass in here.
actor_ptr create_PCAPReader_32_0_filtered(
    const std::string &_chan_nam,
    actor_ptr _msg_processor,
    const std::string &_pcap_filename,
    const std::vector<std::pair<uint32_t, uint16_t>> &_filter,
    bool _big_endian = false,
    const mcast_recv::TrailerSpec &_trailer_spec = mcast_recv::TrailerSpec{}
    )
{
    return new mcast_recv::PCAPReader<uint32_t, 0>(
        _chan_nam,
        _msg_processor,
        _pcap_filename,
        _filter,
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
