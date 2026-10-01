#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "mdp3/RecordingHandler.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace mdp3::msg
{
  // DataDecoderActor -> HandlerIfActor. Exactly one per dispatched packet, even
  // when the packet decoded to zero callbacks, so the resequencer never waits on
  // a packet that will not come.
  //
  // `data` owns the packet bytes. Recorded char* arguments point into it, so it
  // must outlive the replay of `calls`. Both are mutable so HandlerIfActor can
  // move them into its reorder buffer when the packet arrives early.
  struct DecodedPacket : public actors::MessageT<DecodedPacket>
  {
    uint64_t dispatch_id = 0;
    uint32_t epoch = 0;
    uint32_t sn = 0;
    uint32_t qlen = 0;
    bool rc = true;
    bool is_channel_reset = false;
    mutable std::unique_ptr<char[]> data;
    mutable std::vector<RecordedCall> calls;
  };
}
