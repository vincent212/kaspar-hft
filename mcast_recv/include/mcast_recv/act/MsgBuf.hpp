#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "chutil/udp_socket.hpp"
#include <map>
#include <chrono>
#include "mcast_recv/message_buffer.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "logger/act/Logger.hpp"

namespace mcast_recv
{

  template <typename seqnumT>
  class MsgBuf : public actors::Actor
  {

  private:
    const char* get_name() const { return name; }
    char name[256];
    std::string chan_nam;
    actor_ptr msg_processor;
    char chan;
    uint64_t recv_ts = 0;
    int ts_cnt = 0;

  public:
    MsgBuf(
        const std::string &_chan_nam,
        int /* spin - not used in actors */,
        char _chan,
        actor_ptr _msg_processor)
        : msg_processor(_msg_processor),
          chan(_chan),
          chan_nam(_chan_nam)

    {
      // Initialize name once in constructor
      snprintf(name, sizeof(name), "%sMsgBuf %c", chan_nam.c_str(), chan);

      MESSAGE_HANDLER(msg::ProcessQ<seqnumT>, processq_handler);
    }

  private:
    void processq_handler(const msg::ProcessQ<seqnumT> *m) noexcept
    {
      msg_processor->fast_send(m, this);
    }
  };

}
