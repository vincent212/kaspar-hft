#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
      // Move the per-message queue depth off the Message and onto the buffer.
      //
      // actors::Actor::add_message_to_queue already stamped m->qlen with this
      // mailbox's depth at the moment SocketReader send()'d the packet here.
      // That is the number we want: both feeds A and B send() into this one
      // MsgBuf (see create_all_mdp3), so it is the depth at the A/B merge
      // point, and unlike the QLen gauge it cannot miss a burst that fills and
      // drains inside a 100 ms tick.
      //
      // It must be copied onto buf *here*, before the hop below. The next stop,
      // MessageProcessor::processq_handler, stores the packet in its reorder
      // map by value -- msg_q[m->buf.seqnum] = m->buf -- so anything still
      // living on the Message is dropped at that copy and never reaches the
      // decoder, the book, or the probe.
      //
      // const_cast: the handler signature is const, and buf is a plain member
      // of ProcessQ (Message::qlen gets away with this by being `mutable`).
      // Safe in practice -- ProcessQ is heap-allocated from the message pool
      // and never const-qualified at the definition -- but it is a wart. See
      // the `mutable message_buffer buf` note in ProcessQ.hpp.
      const_cast<msg::ProcessQ<seqnumT> *>(m)->buf.qlen = m->qlen;

      msg_processor->fast_send(m, this);
    }
  };

}
