#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace actors::msg {
  /// Used for continuation/callback patterns
  struct Continue : public Message_N<1> {
    int id;
    Continue(int _id = 0) : id(_id) {}
  };
}
