#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <limits>

namespace actors::msg {
  /// Sent when a timer expires
  struct Timeout : public Message_N<8> {
    int data;
    Timeout(int d = std::numeric_limits<int>::max()) : data(d) {}
  };
}
