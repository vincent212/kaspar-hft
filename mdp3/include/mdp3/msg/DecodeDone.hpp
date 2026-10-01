#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <cstdint>

namespace mdp3::msg
{
  // HandlerIfActor -> MessageProcessor, one per dispatched packet, in dispatch
  // order. `applied` is false when the packet was dropped because an earlier
  // packet in the same epoch failed to decode.
  struct DecodeDone : public actors::MessageT<DecodeDone>
  {
    uint64_t dispatch_id;
    uint32_t sn;
    bool rc;
    bool is_channel_reset;
    bool applied;

    DecodeDone(uint64_t id, uint32_t s, bool r, bool cr, bool a) noexcept
        : dispatch_id(id), sn(s), rc(r), is_channel_reset(cr), applied(a) {}
  };
}
