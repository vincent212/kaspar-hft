#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "frame/som/msg/Fill.hpp"

namespace light
{
  namespace msg
  {
    struct LightInfo : public  actors::Message_N<81>
    {
      typedef std::tuple<frame::som::msg::Fill, uint64_t> last_fill_msg_t; // with timestamp
      typedef std::list<std::tuple<frame::som::msg::Fill, uint64_t>> last_fill_msg_list_t;
      int pos;
      last_fill_msg_list_t last_fill_msgs;
      LightInfo(int pos, last_fill_msg_list_t last_fill_msgs)
          : pos(pos), last_fill_msgs(last_fill_msgs) {}
    };
  }
}
