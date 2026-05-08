#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace mdp3
{
  namespace msg
  {
    struct EndInstrumentRecovery : public actors::Message_N<65>
    {
      EndInstrumentRecovery() {}
      virtual ~EndInstrumentRecovery() {}
    };
  }
}
