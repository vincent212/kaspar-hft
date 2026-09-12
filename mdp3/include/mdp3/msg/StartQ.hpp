#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "chutil/PooledObject.hpp"

namespace mdp3
{
  namespace msg
  {
    struct StartQ : public chutil::PooledObject<StartQ, actors::MessageT<StartQ>>
    {
      StartQ()
          {}
    };
  }
}