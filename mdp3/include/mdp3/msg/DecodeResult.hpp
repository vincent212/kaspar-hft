#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"

namespace mdp3::msg
{
  // Reply from the DataDecoder actor's decode handler back to MessageProcessor's
  // fast_send. `rc` == mbo_data's return (false => the caller triggers recovery);
  // `is_channel_reset` mirrors the old out-param. Owned by the caller's unique_ptr
  // (fast_send return value), which deletes it -> pooled so that delete recycles.
  struct DecodeResult : public actors::MessageT<DecodeResult>,
                        public actors::MemoryPool<DecodeResult, 16, 16, 2048>
  {
    bool rc;
    bool is_channel_reset;

    DecodeResult(bool r, bool cr) noexcept : rc(r), is_channel_reset(cr) {}
  };
}
