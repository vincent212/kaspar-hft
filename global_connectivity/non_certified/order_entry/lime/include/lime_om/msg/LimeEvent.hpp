/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LimeEvent: one Lime API callback, copied on the API's callback thread and
// handed to LimeOM with send() (never fast_send: the callback thread must
// not take the actor's lock). Interpreted on LimeOM's own thread.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "lime_om/LimeApi.hpp"

namespace lime::msg
{
  struct LimeEvent : public actors::MessageT<LimeEvent>
  {
    enum Kind
    {
      LOGIN_ACCEPTED,
      LOGIN_FAILED,      // text: reason
      CONNECTION_FAILED, // text: reason
      BUSY,
      AVAILABLE,
      ACCEPTED,          // id, lime_id
      FILLED,            // id, fill
      CANCELED,          // id, text
      PARTIAL_CANCELED,  // id, quantity (left open)
      REPLACED,          // id, replace_id, lime_id
      REJECTED,          // id, text
      CANCEL_REJECTED,   // id, text
      REPLACE_REJECTED,  // id, replace_id, text
      MANUAL_ORDER,      // id, symbol, route, quantity, price, side
      MANUAL_REPLACE,    // id, replace_id, quantity, price
      RECONNECT,         // from LimeOM's own reconnect timer
    };

    Kind kind;
    uint64_t session = 0; // which session object sent it (stale ones are ignored)
    uint64_t event_id = 0;
    OrderId id = 0, replace_id = 0, lime_id = 0;
    int quantity = 0;
    ScaledPrice price = 0;
    Side side = Side::Buy;
    FillInfo fill;
    std::string text, symbol, route;

    LimeEvent(Kind k, uint64_t s) : kind(k), session(s) {}
  };

  const char *to_string(LimeEvent::Kind k);
}
