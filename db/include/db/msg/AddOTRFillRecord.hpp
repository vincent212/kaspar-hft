#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace postrade::msg
{
  struct AddOTRFillRecord : public actors::Message_N<181>
  {
        int bench_px;
        int offtr_px;
        double fill_px;
        std::string side;
        std::string venue;
        int fill_sz;
        int stbf;
        int fill_oid;
        uint64_t fill_ts;
        int last_bestpx;
        int last_offtrpx;
        int last_bb;
        int last_bs;
        std::string offtr_sym;
        std::string name;

        AddOTRFillRecord(
            int bench_px,
            int offtr_px,
            double fill_px,
            const std::string& side,
            const std::string& venue,
            int fill_sz,
            int stbf,
            int fill_oid,
            uint64_t fill_ts,
            int last_bestpx,
            int last_offtrpx,
            int last_bb,
            int last_bs,
            const std::string& offtr_sym,
            const std::string& name
        )
            : bench_px(bench_px),
              offtr_px(offtr_px),
              fill_px(fill_px),
              side(side),
              venue(venue),
              fill_sz(fill_sz),
              stbf(stbf),
              fill_oid(fill_oid),
              fill_ts(fill_ts),
              last_bestpx(last_bestpx),
              last_offtrpx(last_offtrpx),
              last_bb(last_bb),
              last_bs(last_bs),
              offtr_sym(offtr_sym),
              name(name)
        {
        }

        
  };
}
