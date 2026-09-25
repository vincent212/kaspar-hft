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
  // Sent by handler_if to the Reconstructor for each resting order recovered from
  // a snapshot (SnapshotFullRefreshOrderBook), carrying orderID -> securityID.
  //
  // Why this exists: recovery runs through handler_if (RecoveryProcessor calls the
  // cb directly), which fills handler_if's own orderid_to_securityid map. On the
  // PARALLEL path the live feed instead resolves trades against the Reconstructor's
  // orderid_to_securityid_ -- a different map. Without this seed, a live trade that
  // references a snapshot-resting order would not resolve in the Reconstructor and
  // the trade would be dropped. This forwards the snapshot's orderID->securityID so
  // the Reconstructor's map matches what recovery rebuilt.
  //
  // Only sent when handler_if::reconstructor is non-null (parallel decode wired);
  // the serial path never allocates one. Bursty but rare (recovery only), so the
  // pool is sized generously; it grows on demand regardless.
  struct OrderMap : public actors::MessageT<OrderMap>,
                    public actors::MemoryPool<OrderMap, 128, 128, 8192>
  {
    uint64_t orderID;
    int32_t  securityID;

    OrderMap(uint64_t o, int32_t s) noexcept : orderID(o), securityID(s) {}
  };
}
