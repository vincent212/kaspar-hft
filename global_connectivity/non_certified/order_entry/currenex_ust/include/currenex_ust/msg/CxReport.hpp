/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// CxReport: one session event, decoded on the session's thread and handed
// to CurrenexOM with send() (never fast_send: the session thread must not
// take the actor's lock). Interpreted on CurrenexOM's own thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "currenex_ust/CxCodec.hpp"

namespace currenex::ouch::msg
{
  struct CxReport : public actors::MessageT<CxReport>
  {
    enum Kind
    {
      LOGGED_IN,  // session_id, sequence (of the Logon response)
      LOGGED_OUT, // text: reason code; before_login
      DISCONNECTED,
      BUSY,
      AVAILABLE,
      MESSAGE,    // a Currenex message, decoded into msg
      UNDECODED,  // a numbered message that did not decode; text: its type
      RECONNECT,  // from CurrenexOM's own reconnect timer
    };

    Kind kind;
    uint64_t session = 0;   // which session object sent it (stale ones are ignored)
    uint32_t sequence = 0;  // header sequence number (MESSAGE, UNDECODED, LOGGED_IN)
    int32_t ts_ms = 0;      // header timestamp, ms past midnight UTC
    int32_t session_id = 0; // LOGGED_IN
    bool before_login = false;
    from::message_t msg;
    std::string text;

    CxReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
