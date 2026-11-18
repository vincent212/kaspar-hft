/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LmaxSession: one LMAX Broker Trading FIX session as the order manager and
// the post-trade client see it. NOT CERTIFIED.
//
// Implementations:
//   QfLmaxSession  QuickFIX initiator to LMAX (or to SimLmaxGateway on
//                  127.0.0.1): qf_session_factory()
//   FakeLmax       the test double (tests/FakeLmax.hpp)
//
// Rules every implementation keeps:
//   - events arrive on the implementation's thread (the fake may call them
//     from inside a request); the receiver only queues them
//   - reports are parsed before they are delivered; one that does not parse
//     is delivered as unparsed(), never dropped and never thrown back to
//     QuickFIX (which would send LMAX a Reject)
//   - a request made while not logged on returns Disconnected and sends
//     nothing: QuickFIX would otherwise queue it and send it on the next logon

#include <functional>
#include <memory>
#include <string>

#include "lmax_fix/LmaxFix.hpp"
#include "lmax_om/ExecParse.hpp"

namespace lmax::om
{
  enum class CallStatus
  {
    Success,
    Disconnected,
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_on() = 0;
    virtual void logged_out(const std::string &reason) = 0;
    virtual void exec(const ExecReport &r) = 0;
    virtual void cancel_reject(const CancelReject &r) = 0;
    virtual void trade_capture(const TradeCaptureReport &r) {}
    virtual void tcr_ack(const TcrAck &a) {}
    virtual void session_reject(const std::string &text) = 0; // 35=3 from LMAX
    virtual void unparsed(const std::string &what) = 0;
  };

  class LmaxSession
  {
  public:
    virtual ~LmaxSession() = default;
    virtual void start() = 0; // connect and log on; events follow
    virtual void stop() = 0;  // log out; no events after it returns
    virtual CallStatus send_new(const NewOrderReq &r) = 0;
    virtual CallStatus send_cancel(const CancelReq &r) = 0;
    virtual CallStatus send_replace(const ReplaceReq &r) = 0;
    virtual CallStatus send_status(const StatusReq &r) = 0;
    virtual CallStatus send_tcr_request(const TcrReq &r) = 0;
  };

  struct QfSessionConfig
  {
    fix::SessionParams params; // env, host, port, TLS, CompIDs, dictionary, store, log
    std::string username, password;
  };

  using SessionFactory = std::function<std::unique_ptr<LmaxSession>(SessionEvents &)>;

  // QuickFIX sessions with cfg; the host guard runs when the session is made
  // (std::runtime_error on refusal).
  SessionFactory qf_session_factory(QfSessionConfig cfg);
}
