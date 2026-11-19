#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FillEvent: one FillSource event, sent from the source's thread to
// BybitFillsClient and handled on the client's own thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"

namespace bybit_pt::msg
{
  struct FillEvent : public actors::MessageT<FillEvent>
  {
    enum Kind
    {
      LOGGED_IN,
      LOGIN_REJECTED,
      DISCONNECTED,
      EXECUTION,
      RECONCILED,
      RECONNECT, // the client's own timer
      RECONCILE, // the client's own timer: periodic reconciliation
    };
    Kind kind;
    uint64_t source = 0; // which source object sent it
    std::string json;
    std::string category;
    std::string text;
    bool from_rest = false;
    bool ok = true;
    int pages = 0, count = 0;
    FillEvent(Kind k, uint64_t s) : kind(k), source(s) {}
  };
}
