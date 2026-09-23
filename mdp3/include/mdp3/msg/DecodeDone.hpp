#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include <cstdint>

namespace mdp3::msg
{
  // A worker's completion signal back to the coordinator: "I have finished
  // reading the packet buffer for parent_id." The coordinator decrements that
  // packet's outstanding count; when it hits zero, every worker is done reading
  // the buffer and the coordinator frees the (no_delete) ProcessQ back to the
  // pool. Carrying parent_id lets multiple packets decode in parallel -- Dones
  // from different packets route to the right outstanding count.
  struct DecodeDone : public actors::MessageT<DecodeDone>,
                      public actors::MemoryPool<DecodeDone, 32, 32, 4096>
  {
    uint64_t parent_id;

    explicit DecodeDone(uint64_t pid) noexcept : parent_id(pid) {}
  };
}
