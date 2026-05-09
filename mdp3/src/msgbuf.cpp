
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mcast_recv/act/MsgBuf.hpp"

actor_ptr craete_MsgBuf(
    const std::string &_chan_nam,
    bool spin,
    char _chan,
    actor_ptr _msg_processor)
{
  return new mcast_recv::MsgBuf<uint32_t>(
      _chan_nam,
      spin,
      _chan,
      _msg_processor);
}
