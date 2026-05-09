#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace mdp3
{
  namespace msg
  {
    struct EndDataRecovery : public actors::Message_N<63>
    {
      uint64_t last_seq;
      EndDataRecovery(uint64_t _last_seq) {last_seq = _last_seq;}
      virtual ~EndDataRecovery(){}
    };
  }
}
