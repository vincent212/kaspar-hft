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
  // Sent by handler_if to the Reconstructor whenever an instrument definition
  // maps a CME securityID -> kaspr asset_id. This lets the Reconstructor keep its
  // OWN asset map, updated on ITS thread, instead of the two of them racing on
  // handler_if's shared map (which the inline definition path writes while the
  // Reconstructor reads it during routing). Low rate (definitions only).
  struct AssetMap : public actors::MessageT<AssetMap>,
                    public actors::MemoryPool<AssetMap, 8, 8, 512>
  {
    int32_t securityID;
    int32_t asset_id;

    AssetMap(int32_t s, int32_t a) noexcept : securityID(s), asset_id(a) {}
  };
}
