#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "cfsm/Message.hpp"

namespace mq0::msg
{
  struct MQ0_Msg : public cfsm::MessageT<MQ0_Msg>
  {
    std::string msg;
    MQ0_Msg(const std::string &message) : msg(message)
    {
    }
  };
}