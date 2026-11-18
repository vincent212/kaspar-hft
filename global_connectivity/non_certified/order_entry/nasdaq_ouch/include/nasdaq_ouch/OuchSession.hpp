/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// OuchSession: the connection to one Nasdaq OUCH 5.0 port, as OuchOM sees it.
//
// OUCH 5.0 runs over SoupBinTCP 3.0: the client logs in, sends each OUCH
// request in an Unsequenced Data packet, and receives each OUCH report in a
// Sequenced Data packet whose sequence number is implied by counting from the
// one in Login Accepted (SoupBinTCP 3.0 1.2 and 2.2.3).
//
// Three implementations:
//   SoupSession  the real one: SoupBinTCP over a TCP socket (Boost.Asio)
//   SimOuch      an in-process Nasdaq for --sim runs (SimOuch.hpp)
//   FakeOuch     the test double (tests/FakeOuch.hpp)
//
// Rules every implementation keeps:
//   - events are delivered on the implementation's own thread (the test
//     double may also deliver from inside a call), and never after the
//     session's destructor has returned; the receiver only queues them, so
//     it never calls back into the session from an event
//   - message() carries each OUCH report exactly as received, with its
//     SoupBinTCP sequence number; the receiver decodes it
//   - requests are never queued across a disconnect: a call made while the
//     session is not logged in returns Disconnected

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "nasdaq_ouch/gen/ouch50.hpp"

namespace nasdaq::ouch
{
  enum class CallStatus
  {
    Success,      // handed to the connection
    Busy,         // the outbound queue is full; wait for available()
    Disconnected, // not logged in; nothing was sent
  };

  // SoupBinTCP Login Request (SoupBinTCP 3.0 2.3.1).
  struct LogonParams
  {
    std::string username;  // 6 characters at most
    std::string password;  // 10 characters at most
    std::string session;   // empty: the currently active session
    uint64_t sequence = 0; // next sequence number wanted; 0: from the most recent message
  };

  // What a session reports.
  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_in(const std::string &session, uint64_t next_sequence) = 0;
    virtual void login_rejected(char reason) = 0; // 'A' not authorized, 'S' session not available
    virtual void disconnected(const std::string &reason) = 0;
    virtual void end_of_session() = 0;
    virtual void busy() = 0;      // a request returned Busy
    virtual void available() = 0; // the queue has drained; requests are accepted again
    // One OUCH outbound message (the payload of a Sequenced Data packet).
    virtual void message(uint64_t sequence, const char *data, size_t len) = 0;
  };

  class OuchSession
  {
  public:
    virtual ~OuchSession() = default;
    // Connect and send the Login Request; the answer comes as logged_in,
    // login_rejected or disconnected. Called once per session object.
    virtual void logon(const LogonParams &p) = 0;
    // Enter Order (OUCH 5.0 2.1) with its Optional Appendage, already encoded
    // as TagValue elements (AppendageLength is set from it).
    virtual CallStatus enter(const to_nasdaq::EnterOrder &m, std::string_view appendage) = 0;
    // Cancel Order Request (OUCH 5.0 2.3).
    virtual CallStatus cancel(const to_nasdaq::CancelOrderRequest &m) = 0;
    // Account Query Request (OUCH 5.0 2.8): the answer is an Account Query
    // Response with the next available UserRefNum.
    virtual CallStatus account_query() = 0;
    // Logout Request (SoupBinTCP 3.0 2.3.4). No events follow.
    virtual void logout() = 0;
  };

  // Where and how to connect. Credentials come from a deployment file outside
  // the repository (OuchConfig.hpp).
  struct SessionConfig
  {
    std::string host;
    std::string port;
    int heartbeat_ms = 1000;            // send a Client Heartbeat after this long without sending (SoupBinTCP 1.3)
    int receive_timeout_ms = 15000;     // no data or heartbeat for this long: the link is down
    int connect_timeout_ms = 5000;
    size_t max_queued_bytes = 1 << 20;  // above this, requests return Busy
  };

  using SessionFactory =
      std::function<std::unique_ptr<OuchSession>(const SessionConfig &, SessionEvents &)>;

  // The real SoupBinTCP session (SoupSession.cpp).
  SessionFactory soup_session_factory();
}
