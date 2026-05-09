#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "chutil/Time.hpp"
#include "enum/e_names.hpp"

#include "frame/ob/pub/nlevels.hpp"

using book_arry_t = std::array<int, NLEVELS>;

namespace frame::ob::msg
{
  struct EndOfBurst2 : public  actors::Message_N<74>  , public actors::MemoryPool<EndOfBurst2, 32, 16, 1024>
  {
    uint sym = 0;
    int bb = 0, ba = 0;
    uint bs = 0, as = 0;
    double mid = 0;
    uint64_t txtim = 0;
    int32_t volumefound = 0;
    int sz;
    int px;
    en::md mev;
    en::mt action;
    en::bs side;
    int num_trad;

    EndOfBurst2(
        uint _sym,
        uint64_t txtim,
        int _bb,
        int _ba,
        uint _bs,
        uint _as,
        double _mid,
        en::md _mev,
        en::mt _action,
        en::bs _side,
        int32_t _volumefound,
        int _num_trad = 0)
        : sym(_sym),
          txtim(txtim),
          bb(_bb), ba(_ba),
          bs(_bs), as(_as),
          mid(_mid),
          mev(_mev),
          action(_action),
          side(_side),
          volumefound(_volumefound),
          num_trad(_num_trad)
    {
      if (txtim > 0) {
        ASSERT(txtim > 0, "invalid txtim");
      }
    }

    EndOfBurst2() = default;
  };
}