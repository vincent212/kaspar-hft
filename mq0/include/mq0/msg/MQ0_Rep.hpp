#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "cfsm/Message.hpp"

namespace mq0::msg
{
  struct MQ0_Rep : public cfsm::Message_N<191>
  {
    std::string msg;
    int status = 0; // 0 = success, 1 = error
    MQ0_Rep(const std::string &message) : msg(message)
    {
    }
    MQ0_Rep() : status(1) {}
  };
}