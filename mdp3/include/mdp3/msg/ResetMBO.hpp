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
  // Sent by handler_if to the Reconstructor on a ChannelReset: the exchange has
  // cleared the order book, so the Reconstructor must drop its orderID->securityID
  // map -- otherwise a reused orderID after the reset misroutes. Instrument
  // definitions persist across a reset, so the asset map is NOT cleared. Rare.
  struct ResetMBO : public actors::MessageT<ResetMBO>
  {
    ResetMBO() noexcept {}
  };
}
