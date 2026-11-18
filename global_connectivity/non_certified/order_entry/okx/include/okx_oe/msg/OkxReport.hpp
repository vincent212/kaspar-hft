#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxReport: one session event, handed from the session's thread to OkxOM
// with send() (never fast_send). Interpreted on OkxOM's own thread.

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "actors/Message.hpp"

namespace okx::oe::msg
{
  struct OkxReport : public actors::MessageT<OkxReport>
  {
    enum Kind
    {
      LOGGED_IN,
      LOGIN_REJECTED,
      DISCONNECTED,
      BUSY,
      AVAILABLE,
      OP_RESULT,
      ORDER_UPDATE,
      RECONNECT, // OkxOM's own reconnect timer
    };

    Kind kind;
    uint64_t session = 0; // which session object sent it (stale ones are ignored)
    nlohmann::json body;  // OP_RESULT, ORDER_UPDATE
    std::string text;     // reason or error code and message

    OkxReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
