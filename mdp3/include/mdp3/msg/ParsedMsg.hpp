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
  // PERF: entries live INLINE in the pooled block, not in a heap vector.
  //
  // This used to be a std::vector that DecodeSink::flush() swap()'d into. The
  // swap handed the sink's reserve(64)'d storage away and took back an empty
  // vector, so the next message's first emplace_back reallocated from zero:
  // one malloc per message on the decode hot path, plus the matching free when
  // the ParsedMsg was recycled. ParsedMsg itself is pooled; the vector was the
  // one part that was not.
  //
  // INLINE_CAP is sized from the feed, not guessed. Measured over 1.31M CME
  // packets (ES/NQ/ZN, 900s): messages per packet mean 1.06-1.10, p50 1, p90 1,
  // p99 2-4, max 34. Entries per message tracks that. 8 covers p99 with room;
  // the overflow vector catches the max-34 case and anything fatter, following
  // the house circular-buffer-plus-overflow pattern (BQueue, HybridBuffer).
  static constexpr std::size_t INLINE_CAP = 8;

  struct ParsedMsg : public actors::MessageT<ParsedMsg>,
                     public actors::MemoryPool<ParsedMsg, 32, 32, 4096>
  {
    bfile::l3_t inl[INLINE_CAP];          // fast path: no allocation
    std::vector<bfile::l3_t> overflow;    // only touched when n > INLINE_CAP
    uint32_t                 n = 0;       // total entries across inl + overflow
    uint64_t                 order_seq;   // dense per-message resequence key

    explicit ParsedMsg(uint64_t seq) : order_seq(seq) {}

    void push(const bfile::l3_t &e) noexcept
    {
      if (n < INLINE_CAP)
        inl[n] = e;
      else
        overflow.push_back(e);
      ++n;
    }

    void clear() noexcept
    {
      n = 0;
      overflow.clear(); // keeps capacity if it ever grew
    }

    std::size_t size() const noexcept { return n; }
    bool empty() const noexcept { return n == 0; }

    // Entry i in wire order, inline block first then overflow.
    const bfile::l3_t &operator[](std::size_t i) const noexcept
    {
      return i < INLINE_CAP ? inl[i] : overflow[i - INLINE_CAP];
    }

    // Copy every entry, in wire order, into any push_back-able sink. Used by the
    // Reconstructor when a batch has to be buffered for resequencing.
    template <typename Out>
    void copy_to(Out &out) const
    {
      for (std::size_t i = 0; i < n; ++i)
        out.push_back((*this)[i]);
    }
  };
}
