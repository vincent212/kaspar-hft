/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "ttom/act/TTFIXOM.hpp"

cfsmp create_TTFIXOM(
    const std::string &fixConfig)
{
  return new ttfixom::act::TTFIXOM(
      fixConfig);
}

cfsmp create_TTOM(
    actors::Actor *som,
    const std::string &fixConfig,
    en::x venue)
{
  return new ttfixom::act::TTFIXOM(
      fixConfig, som, venue);
}
