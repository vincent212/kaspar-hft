#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <array>

namespace mcast_recv
{
    constexpr std::size_t msgsz = 2000;
    struct message_buffer
    {
        uint32_t seqnum;
        std::array<char, msgsz> message;
        std::size_t len;
        uint64_t recv_ts;
        char chan;
        uint32_t src_ip;   // IPv4 source address, network byte order
        uint16_t dst_port; // UDP destination port, host byte order
    };

}