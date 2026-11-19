#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitReport: one session event, handed to BybitOM with send() from the
// session's thread and interpreted on BybitOM's own thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "bybit_oe/BybitSession.hpp"

namespace bybit::oe::msg
{
  struct BybitReport : public actors::MessageT<BybitReport>
  {
    enum Kind
    {
      LOGGED_IN,
      LOGIN_REJECTED,
      DISCONNECTED,
      BUSY,
      AVAILABLE,
      ORDER,     // json: one order update
      EXECUTION, // json: one execution
      RESPONSE,  // REST answer: request, link, ret_code, text
      RECONNECT, // from BybitOM's own reconnect timer
    };

    Kind kind;
    uint64_t session = 0; // which session object sent it
    std::string json;     // ORDER, EXECUTION
    std::string text;     // reason, retMsg
    RequestKind request = RequestKind::Place;
    std::string link;     // RESPONSE: orderLinkId
    int ret_code = 0;

    BybitReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
