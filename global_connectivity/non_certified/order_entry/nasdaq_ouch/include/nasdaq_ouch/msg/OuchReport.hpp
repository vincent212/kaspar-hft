/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// OuchReport: one session event, decoded on the session's thread and handed
// to OuchOM with send() (never fast_send: the session thread must not take
// the actor's lock). Interpreted on OuchOM's own thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "nasdaq_ouch/gen/ouch50.hpp"

namespace nasdaq::ouch::msg
{
  struct OuchReport : public actors::MessageT<OuchReport>
  {
    enum Kind
    {
      LOGGED_IN,
      LOGIN_REJECTED,
      DISCONNECTED,
      END_OF_SESSION,
      BUSY,
      AVAILABLE,
      MESSAGE,   // an OUCH report, decoded into msg
      UNDECODED, // an OUCH report that did not decode (unknown type, too short)
      RECONNECT, // from OuchOM's own reconnect timer
    };

    Kind kind;
    uint64_t session = 0;  // which session object sent it (stale ones are ignored)
    uint64_t sequence = 0; // SoupBinTCP sequence number (MESSAGE, UNDECODED), next one (LOGGED_IN)
    from_nasdaq::message_t msg;
    std::string text;      // soup session id, reject code, reason, or the undecoded type
    std::string appendage; // Optional Appendage of msg, when it has one

    OuchReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
