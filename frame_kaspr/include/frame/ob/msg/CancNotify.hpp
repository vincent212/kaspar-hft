#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/e_names.hpp"

namespace frame
{
  namespace ob
  {
    namespace msg
    {
      struct CancNotify : public  actors::Message_N<24>
      {
        CancNotify(
        en::bs _side,
        int _sym,
        en::x _mkt,
        uint _px,
        int _sz,
        int _disp_sz,
        unsigned long long _oid,
        unsigned long long _prev_oid,
        unsigned long long _next_oid,
        double _w)
        :
        side(_side),
        sym(_sym),
        mkt(_mkt),
        px(_px),
        sz(_sz),
        disp_sz(_disp_sz),
        oid(_oid),
        prev_oid(_prev_oid),
        next_oid(_next_oid),
        w(_w)
        {}
        en::bs side;
        int sym;
        en::x mkt;
        uint px;
        int sz;
        int disp_sz;
        unsigned long long oid;
        unsigned long long prev_oid;
        unsigned long long next_oid;
        double w;
      };
    }
  }
}
