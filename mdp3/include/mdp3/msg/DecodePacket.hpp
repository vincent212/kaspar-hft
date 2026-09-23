#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <cstddef>
#include <cstdint>

namespace mdp3::msg
{
  // fast_send request from MessageProcessor into the DataDecoder actor: "decode
  // this one packet." `data` points at the packet (starts with the 12-byte
  // MsgSeqNum+SendingTime header). Stack-allocated by the caller -- fast_send
  // borrows it and never queues/deletes it -- so it is NOT pooled.
  struct DecodePacket : public actors::MessageT<DecodePacket>
  {
    const char *data;
    std::size_t len;
    uint64_t    ts;
    uint32_t    qlen; // ingress mailbox depth this packet queued behind

    DecodePacket(const char *d, std::size_t l, uint64_t t, uint32_t q) noexcept
        : data(d), len(l), ts(t), qlen(q) {}
  };
}
