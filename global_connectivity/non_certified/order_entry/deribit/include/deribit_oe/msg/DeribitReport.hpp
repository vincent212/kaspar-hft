#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitReport: one session event, handed from the session's thread to
// DeribitOM with send() and interpreted on DeribitOM's own thread.

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "actors/Message.hpp"

namespace deribit::oe::msg
{
  struct DeribitReport : public actors::MessageT<DeribitReport>
  {
    enum Kind
    {
      LOGGED_IN,
      LOGIN_REJECTED,
      DISCONNECTED,
      BUSY,
      AVAILABLE,
      RPC_RESULT,
      ORDER_UPDATE,
      TRADE,
      RECONNECT,        // DeribitOM's own reconnect timer
      DROP_CONNECTION,  // drop the session and reconnect (the testnet check's reconnect step)
    };

    Kind kind;
    uint64_t session = 0;  // which session object sent it (stale ones are ignored)
    nlohmann::json body;
    std::string text;

    DeribitReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
