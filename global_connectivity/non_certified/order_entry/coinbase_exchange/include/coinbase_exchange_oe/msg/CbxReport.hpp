#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CbxReport: one session event, handed from the session's thread to the
// actor with send() (never fast_send: the session thread must not take the
// actor's lock), and interpreted on the actor's thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "coinbase_exchange_oe/FixMsg.hpp"

namespace coinbase_exchange::oe::msg
{
  struct CbxReport : public actors::MessageT<CbxReport>
  {
    enum Kind
    {
      LOGGED_ON,
      LOGON_REJECTED,
      DISCONNECTED,
      BUSY,
      AVAILABLE,
      MESSAGE,       // an application message, in fix
      RECONNECT,     // from the actor's own reconnect timer
      LOGON_TIMEOUT, // from the actor's own timer: no logged_on since connect
      PURGE,         // from the actor's own timer: forget finished orders
    };

    Kind kind;
    uint64_t session = 0; // which session object sent it (stale ones are ignored)
    fix::FixMsg fix;
    std::string text;

    CbxReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
