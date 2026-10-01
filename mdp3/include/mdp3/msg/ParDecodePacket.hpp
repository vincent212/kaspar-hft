#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace mdp3::msg
{
  // MessageProcessor -> DataDecoderActor (parallel decode). Unlike DecodePacket
  // it owns a copy of the bytes, because the send is async and MessageProcessor
  // erases the reorder-map node as soon as it dispatches. `data` is mutable so
  // the worker can move the buffer into the DecodedPacket it forwards.
  struct ParDecodePacket : public actors::MessageT<ParDecodePacket>
  {
    uint64_t dispatch_id; // dense 0,1,2,... per channel; the resequencing key
    uint32_t epoch;       // bumped on a decode failure; HandlerIfActor drops stale epochs
    uint32_t sn;          // MDP3 packet seq, for logs and MessageProcessor
    uint64_t ts;
    uint32_t qlen;
    std::size_t len;
    mutable std::unique_ptr<char[]> data;

    ParDecodePacket(uint64_t id, uint32_t ep, uint32_t s, const char *d,
                    std::size_t l, uint64_t t, uint32_t q)
        : dispatch_id(id), epoch(ep), sn(s), ts(t), qlen(q), len(l),
          data(new char[l])
    {
      std::memcpy(data.get(), d, l);
    }
  };
}
