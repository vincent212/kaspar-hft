#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/exch_code.hpp"
#include <string>

// Test-only factory: builds a TTFIXOM that doesn't register with any SOM.
// Used by standalone test programs that drive the FIX client directly.
cfsmp create_TTFIXOM(
  const std::string &_fixConfig
);

// Production factory: builds a TTFIXOM, gives it the SOM to register with,
// and stamps the venue. On actors::msg::Start the TTFIXOM sends
// frame::som::msg::SetExchManager(venue, this) to `som`, mirroring ilink's
// arbiter and fenics's OMProxy.
cfsmp create_TTOM(
  actors::Actor *_som,
  const std::string &_fixConfig,
  en::x _venue = en::x::TT
);