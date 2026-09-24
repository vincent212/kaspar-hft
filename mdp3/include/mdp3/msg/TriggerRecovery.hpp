#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace mdp3::msg
{
  // Sent by the DataDecoder actor to MessageProcessor when a parallel worker
  // failed to decode a hot message (decode_one returned false). MessageProcessor
  // then initiates data recovery -- the same response the inline path gives to an
  // mbo_data failure -- so the book resyncs instead of silently diverging.
  struct TriggerRecovery : public actors::MessageT<TriggerRecovery>
  {
    TriggerRecovery() noexcept {}
  };
}
