/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// CsReport: one session event, parsed on the session's thread and handed to
// ClearStreetOM with send().

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "clearstreet_om/CsFix.hpp"

namespace clearstreet::om::msg
{
  struct CsReport : public actors::MessageT<CsReport>
  {
    enum Kind
    {
      LOGGED_ON,
      LOGGED_OUT,
      EXEC,
      CANCEL_REJECT,
      SESSION_REJECT,
      UNPARSED,
    };
    Kind kind;
    uint64_t session = 0; // which session object; events from an older one are dropped
    ExecReport exec;
    CancelReject cxl;
    SessionReject rej;
    std::string text;
    CsReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };
}
