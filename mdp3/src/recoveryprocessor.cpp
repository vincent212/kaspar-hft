
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mdp3/act/RecoveryProcessor.hpp"

actor_ptr create_RecoveryProcessor(
    const std::string &_chan_nam,
    mdp3::feed_handler_if *_cb,
    in_port_t _port_dr,
    in_port_t _port_ir,
    const char *_group_dr,
    const char *_group_ir,
    const char *_interface,
    bool _debug)
{
  return new mdp3::RecoveryProcessor(
      _chan_nam,
      _cb,
      _port_dr,
      _port_ir,
      _group_dr,
      _group_ir,
      _interface,
      _debug);
}