#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "bfile/r_l3.hpp"
#include <cstdint>
#include <vector>

namespace mdp3::msg
{
  // The decoded entries of ONE SBE message, produced by a stateless DecodeWorker
  // and sent to the single Reconstructor. Carrying the whole message's entries as
  // a batch (rather than one message per entry) makes the resequence trivial:
  // order_seq is a DENSE, contiguous per-message key, so the Reconstructor just
  // drains order_seq = expected, expected+1, ... applying each batch's entries in
  // their built (wire) order. Routing (orderid map, asset lookup, BOOKSEND) is
  // the Reconstructor's job -- the entries here carry unresolved l3 only.
  //
  // NOTE (perf, optimize later): the std::vector heap-allocates per message; the
  // pooled message block does not pool it. A fixed inline buffer is the follow-up.
  struct ParsedMsg : public actors::MessageT<ParsedMsg>,
                     public actors::MemoryPool<ParsedMsg, 32, 32, 4096>
  {
    std::vector<bfile::l3_t> entries;   // built MBO order/trade records, in wire order
    uint64_t                 order_seq; // dense per-message resequence key

    explicit ParsedMsg(uint64_t seq) : order_seq(seq) {}
  };
}
