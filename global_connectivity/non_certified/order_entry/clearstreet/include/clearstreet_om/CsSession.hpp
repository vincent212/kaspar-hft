/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// CsSession: one Clear Street FIX order-entry session as the order manager
// sees it. NOT CERTIFIED.
//
// Implementations:
//   QfCsSession     QuickFIX initiator (qf_session_factory); never connected
//                   to Clear Street
//   SimClearStreet  in-process simulated broker (sim_session_factory)
//   FakeClearStreet the test double (tests/FakeClearStreet.hpp)
//
// Rules every implementation keeps:
//   - events arrive on the implementation's thread (the fake may call them
//     from inside a request); the receiver only queues them
//   - reports are parsed before they are delivered; one that does not parse
//     is delivered as unparsed(), never dropped and never thrown back to
//     QuickFIX (which would send Clear Street a Reject)
//   - a request made while not logged on returns Disconnected and sends
//     nothing: QuickFIX would otherwise store it, and Clear Street would ask
//     for it in a ResendRequest after the next logon -- an order sent late
//   - after stop() returns, no more events

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "clearstreet_om/CsFix.hpp"

namespace clearstreet::om
{
  enum class CallStatus
  {
    Success,
    Disconnected,
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    // text: Text (58) of Clear Street's Logon, the accounts the session may trade
    virtual void logged_on(const std::string &text) = 0;
    virtual void logged_out(const std::string &reason) = 0;
    virtual void exec(const ExecReport &r) = 0;
    virtual void cancel_reject(const CancelReject &r) = 0;
    virtual void session_reject(const SessionReject &r) = 0;
    virtual void unparsed(const std::string &what) = 0;
  };

  class CsSession
  {
  public:
    virtual ~CsSession() = default;
    virtual void start() = 0; // connect and log on; events follow
    virtual void stop() = 0;  // log out; no events after it returns
    virtual CallStatus send_new(const NewOrderReq &r) = 0;
    virtual CallStatus send_cancel(const CancelReq &r) = 0;
    virtual CallStatus send_replace(const ReplaceReq &r) = 0;
  };

  using SessionFactory = std::function<std::unique_ptr<CsSession>(SessionEvents &)>;

  struct QfSessionConfig
  {
    std::string host;
    int port = 0;
    bool tls = false;               // QuickFIX SSLSocketInitiator; else plain TCP (TLS outside FIX)
    std::string sender_comp_id;     // 49, also Username (553)
    std::string target_comp_id;     // 56, provisioned by Clear Street
    std::string api_key;            // Password (554); from the key file, never in the repository
    int heartbeat = 30;             // 108
    std::string dictionary;         // build/ClearStreet-FIX42.xml
    std::string store_path;         // FileStore: keep between runs (sequence numbers)
    std::string log_path;           // FIX message log; 554 is redacted
    std::string start_day = "Monday", start_time = "01:00:00"; // UTC; see feed.md section 5
    std::string end_day = "Saturday", end_time = "00:00:00";
    int reconnect_interval = 5;
    std::map<std::string, std::string> extra; // more QuickFIX session settings (qf.<Key> in the config)
  };

  // QuickFIX sessions with cfg.
  SessionFactory qf_session_factory(QfSessionConfig cfg);

  // Replaces the value of every 554= field in a FIX string with ***.
  std::string redact(const std::string &fix);
}
