
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mcast_recv/act/SocketReader.hpp"

actor_ptr create_SocketReader(
    const std::string &_chan_nam,
    bool _blocksock,
    const char _chan,
    actors::Actor *_msg_processor,
    in_port_t _port,
    const char *_group,
    const char *_interface,
    const char *_desc
    )
{
  return new mcast_recv::SocketReader<uint32_t,0>(
      _chan_nam,
      _blocksock,
      _chan,
      _msg_processor,
      _port,
      _group,
      _interface,
      _desc
      );
}