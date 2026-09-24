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
#ifdef MDP3_VERIFY_TEE
#include "mcast_recv/message_buffer.hpp"
#include <array>
#include <cstring>
#endif

namespace mdp3::msg
{
  // fast_send request from MessageProcessor into the DataDecoder actor: "decode
  // this one packet." `data` points at the packet (starts with the 12-byte
  // MsgSeqNum+SendingTime header). Stack-allocated by the caller -- fast_send
  // borrows it and never queues/deletes it -- so it is NOT pooled.
  //
  // Under MDP3_VERIFY_TEE it has a second, OWNING form. One message type with
  // two constructors, not two message types: the payload is identical and the
  // handler is identical, so a second type would only have bought a second
  // MESSAGE_HANDLER registration that ran the same decode.
  //
  // It carries no "am I async" bit either. The two decoders are separate actor
  // INSTANCES with separate configuration -- the shadow is told once, at wiring
  // time, that it is fed by send() and must not reply, exactly as it is told
  // its worker count. See DataDecoder::set_async_input.
  struct DecodePacket : public actors::MessageT<DecodePacket>
  {
    const char *data;
    std::size_t len;
    uint64_t    ts;
    uint32_t    qlen; // ingress mailbox depth this packet queued behind

    // BORROWING form -- the production path. Stack, fast_send, replies.
    DecodePacket(const char *d, std::size_t l, uint64_t t, uint32_t q) noexcept
        : data(d), len(l), ts(t), qlen(q) {}

#ifdef MDP3_VERIFY_TEE
    // Storage for the owning form. `data` points here.
    //
    // sizeof(DecodePacket) grows by msgsz in a VERIFY_TEE build, including for
    // the stack-allocated borrowing instance on the primary path. That costs a
    // larger stack-pointer decrement and nothing else: the borrowing
    // constructor never touches this array, so no page of it is written or
    // read. In a default build the member does not exist.
    std::array<char, mcast_recv::msgsz> owned;

    // OWNING form -- the verification tee only. Heap, send(), no reply.
    //
    // It exists because the borrowing form cannot be queued: `data` points
    // into MessageProcessor's reorder map, which is erased later in the same
    // loop iteration, so by the time a receiving thread woke up the bytes are
    // gone. That lifetime is the only reason the tee used to be a second
    // fast_send -- which forced the shadow to run AFTER the primary's whole
    // inline decode on the same thread, and produced a 2.00x paired median
    // that was measuring the handicap rather than the decoder.
    //
    // The copy is msgsz bytes on a heap allocation, per packet, on the
    // MessageProcessor thread. That is real and it is not free. It is also
    // never built in a default build.
    //
    // `len` is trusted from the socket layer and clamped anyway: message_buffer
    // is the same array the reorder map holds, so a packet that did not fit
    // there cannot reach here.
    struct owning_t {};
    DecodePacket(owning_t, const char *d, std::size_t l, uint64_t t,
                 uint32_t q) noexcept
        : data(nullptr), len(l > mcast_recv::msgsz ? mcast_recv::msgsz : l),
          ts(t), qlen(q)
    {
      std::memcpy(owned.data(), d, len);
      data = owned.data();
    }
#endif
  };
}
