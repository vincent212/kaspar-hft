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
  // One SBE message dispatched to a warm decode worker.
  //
  // ZERO-COPY: `msg` points directly into the owning ProcessQ packet buffer.
  // Safe because the coordinator sets `no_delete` on that ProcessQ and keeps it
  // alive until every worker for it has reported DecodeDone -- so the buffer
  // outlives all worker reads. (If dispatch ever stops tracking completion,
  // `msg` must become a copy.)
  //
  // The worker: parses `msg` -> sends the parsed result (tagged `order_seq`) to
  // the Reconstructor -> sends DecodeDone{parent_id} back to the coordinator.
  //
  // Pooled so the per-message `new` on the dispatch hot path never hits malloc;
  // MessageT auto-assigns a collision-free id.
  struct DecodeReq : public actors::MessageT<DecodeReq>,
                     public actors::MemoryPool<DecodeReq, 32, 32, 4096>
  {
    const char *msg;          // start of the SBE message (its 10-byte header) in the packet
    uint16_t    len;          // MsgSize: total message length incl. the 10-byte SBE header
    uint32_t    msg_seq;      // packet MsgSeqNum (same for every message in the packet)
    uint64_t    ts;           // recv timestamp (recv_time), passed through to decode
    uint64_t    sending_time; // packet SendingTime (read once from the 12-byte packet header)
    uint64_t    order_seq;    // global monotonic ordering key -> Reconstructor resequence
    uint64_t    parent_id;    // owning packet id -> echoed in DecodeDone for buffer refcount
    uint32_t    qlen;         // owning packet's ingress mailbox depth -> stamped on every l3

    DecodeReq(const char *m, uint16_t l, uint32_t mseq, uint64_t t, uint64_t st,
              uint64_t seq, uint64_t pid, uint32_t q) noexcept
        : msg(m), len(l), msg_seq(mseq), ts(t), sending_time(st), order_seq(seq),
          parent_id(pid), qlen(q) {}
  };
}
