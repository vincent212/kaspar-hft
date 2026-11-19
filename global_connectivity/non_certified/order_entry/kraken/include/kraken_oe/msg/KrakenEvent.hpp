#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenEvent: one session event, handed from the session's thread to
// KrakenOM with send(). Interpreted on KrakenOM's thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "kraken_oe/KrakenSession.hpp"

namespace kraken::oe::msg
{
  struct KrakenEvent : public actors::MessageT<KrakenEvent>
  {
    enum Kind
    {
      LOGGED_IN,
      LOGIN_FAILED,
      DISCONNECTED,
      BUSY,
      AVAILABLE,
      REPORT,
      GAP,
      RECONNECT, // from KrakenOM's own reconnect timer
    };
    Kind kind;
    uint64_t session = 0; // which session object sent it; older ones are ignored
    Report report;
    std::string text;
    uint64_t expected = 0, got = 0;

    KrakenEvent(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
