/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LmaxReport: one session event, parsed on the session's thread and handed
// to LmaxOM (or LmaxTradeClient) with send(). ExecCopy: an ExecutionReport
// the order manager passes on to the post-trade client.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "lmax_om/ExecParse.hpp"

namespace lmax::om::msg
{
  struct LmaxReport : public actors::MessageT<LmaxReport>
  {
    enum Kind
    {
      LOGGED_ON,
      LOGGED_OUT,
      EXEC,
      CANCEL_REJECT,
      TRADE_CAPTURE,
      TCR_ACK,
      SESSION_REJECT,
      UNPARSED,
      TICK, // the actor's own timer
    };
    Kind kind;
    uint64_t session = 0; // which session object (events from older ones are dropped)
    ExecReport exec;
    CancelReject cxl;
    TradeCaptureReport tcr;
    TcrAck ack;
    std::string text;
    LmaxReport(Kind k, uint64_t s) : kind(k), session(s) {}
  };

  // An ExecutionReport of a trade, a trade correction or a trade cancel
  // (ExecType F, G, H), forwarded by LmaxOM to its post-trade sink.
  struct ExecCopy : public actors::MessageT<ExecCopy>
  {
    ExecReport exec;
    explicit ExecCopy(const ExecReport &e) : exec(e) {}
  };
}
