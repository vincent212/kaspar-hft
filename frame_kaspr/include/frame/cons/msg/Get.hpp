#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <map>

#include "actors/Message.hpp"

namespace frame
{
  namespace cons
  {
    namespace msg
    {
      struct Get : public  actors::Message_N<132>
      {
        std::string what;
        std::map<std::string, std::string> kv;

        Get(const std::string &_what,
            const std::map<std::string, std::string> &_kv =
                std::map<std::string, std::string>())
            : kv(_kv), what(_what)
        {
        }
      };
    }
  }
}
