#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <limits>
#include <cstdint>
#include "actors/Message.hpp"

namespace frame
{
  namespace mda
  {
    namespace msg
    {
      class Subscribe : public  actors::Message_N<18>
      {
        unsigned int sym;

      public:
        int prio;
        enum
        {
          LOW,
          HI,
          AGGR,
          DATA,
          ALL = 99999999
        };
        Subscribe(bool _prio = DATA, unsigned int _sym = ALL) : prio(_prio), sym(_sym) {}
        unsigned int get_sym() const
        {
          return sym;
        }
      };
    }
  }
}
