#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <any>
#include <string>

namespace actors::msg {
  /// Set a variable on an actor
  struct Set : public Message_N<4> {
    std::string varname;
    std::any value;
    Set(const std::string& name, const std::any& val = false)
      : varname(name), value(val) {}
  };
}
