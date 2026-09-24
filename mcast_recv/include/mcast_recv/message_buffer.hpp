#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
        uint64_t recv_ts;  // software capture ts (pcap record header / socket clock), ns since epoch
        uint64_t hw_ts;    // hardware capture ts from NIC/tap trailer (e.g. Metamako), ns since epoch; 0 = not present
        char chan;
        uint32_t src_ip;   // IPv4 source address, network byte order
        uint16_t dst_port; // UDP destination port, host byte order

        // Depth of the MsgBuf mailbox at the instant this packet was enqueued
        // onto it -- the backlog THIS packet personally queued behind.
        //
        // Copied off actors::Message::qlen by MsgBuf::processq_handler. It has
        // to be moved onto the buffer there because MessageProcessor's reorder
        // map stores message_buffer by value (msg_q[seqnum] = m->buf), so
        // anything living on the Message is dropped at that copy.
        //
        // This is NOT the QLen sample. QLen is a gauge on a 100 ms grid and
        // cannot see a burst that fills and drains between two ticks; this
        // number rides the packet, so latency can be joined against it with no
        // time alignment. Both feeds A and B send() into the same MsgBuf, so
        // this is the depth at the A/B merge point -- the one place in the
        // ingress path where two feed threads contend.
        //
        // Ring occupancy only, read without the mailbox lock: approximate, and
        // a value equal to ACTOR_BQUEUE_SIZE means "at least this deep". Zero
        // on any path that did not enqueue (PCAP replay, fast_send).
        uint32_t qlen;
    };

}