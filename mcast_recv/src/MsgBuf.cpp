
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mcast_recv/act/MsgBuf.hpp"

actor_ptr create_MsgBuf_32(
    const std::string &_chan_nam,
    int spin,
    char _chan,
    actor_ptr _msg_processor)
{
  return new mcast_recv::MsgBuf<uint32_t>(
      _chan_nam,
      spin,
      _chan,
      _msg_processor);
}

actor_ptr create_MsgBuf_64(
    const std::string &_chan_nam,
    int spin,
    char _chan,
    actor_ptr _msg_processor)
{
  return new mcast_recv::MsgBuf<uint64_t>(
      _chan_nam,
      spin,
      _chan,
      _msg_processor);
}
