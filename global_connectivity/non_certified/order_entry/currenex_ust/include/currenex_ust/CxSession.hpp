/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// CxSession: the connection to one Currenex US Treasury OUCH 2.0 session,
// as CurrenexOM sees it.
//
// OUCH runs directly over TCP (UST OUCH 3, p.5). Each direction numbers its
// messages from the Logon on; the client's numbering starts at 1 on every
// connection (5.1.1, p.8); Currenex's continues across connections for the
// trading week when persistence is configured (5.5, p.9) and restarts at 1
// otherwise.
//
// Three implementations:
//   TcpSession   the real one: OUCH over a TCP socket (Boost.Asio)
//   SimCurrenex  an in-process Currenex for --sim runs (SimCurrenex.hpp)
//   FakeCurrenex the test double (tests/FakeCurrenex.hpp)
//
// What a session does: framing (SOH ... ETX), its own outbound sequence
// numbers and timestamps, the Logon, answering each Currenex Heartbeat,
// answering a Currenex Logout, the SessionID in the requests that carry it.
// What it leaves to the receiver: gap detection and ResendRequest, because
// the last sequence number applied outlives the connection.
//
// Rules every implementation keeps:
//   - events are delivered on the implementation's own thread (the test
//     double may also deliver from inside a call), and never after the
//     session's destructor has returned; the receiver only queues them, so
//     it never calls back into the session from an event
//   - message() carries every sequenced Currenex message after the Logon
//     response, Heartbeats and GapFills included, exactly as received from
//     its type code, with the header's sequence number and timestamp; the
//     receiver decodes it
//   - requests are never queued across a disconnect: a call made while the
//     session is not logged in returns Disconnected

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "currenex_ust/CxCodec.hpp"

namespace currenex::ouch
{
  enum class CallStatus
  {
    Success,      // handed to the connection
    Busy,         // the outbound queue is full; wait for available()
    Disconnected, // not logged in; nothing was sent
  };

  // Logon (UST OUCH 10.1, p.18).
  struct LogonParams
  {
    std::string user;     // 20 characters at most
    std::string password; // 20 characters at most
    int16_t version = VERSION;
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    // Currenex's Logon response: its SessionID, and the sequence number in
    // its header (the session's current outbound number, 5.5, p.9).
    virtual void logged_in(int32_t session_id, uint32_t sequence) = 0;
    // A Logout from Currenex, already answered with a Logout; the connection
    // is closed and no disconnected() follows. before_login: it came in
    // place of the Logon response (the logon was refused).
    virtual void logged_out(const std::string &reason, bool before_login) = 0;
    virtual void disconnected(const std::string &reason) = 0;
    virtual void busy() = 0;      // a request returned Busy
    virtual void available() = 0; // the queue has drained; requests are accepted again
    // One sequenced Currenex message from its type code.
    virtual void message(uint32_t sequence, int32_t ts_ms, const char *data, size_t len) = 0;
    // A frame whose type the table does not list (sequence and type known).
    virtual void undecoded(uint32_t sequence, char type) = 0;
  };

  class CxSession
  {
  public:
    virtual ~CxSession() = default;
    // Connect and send the Logon; the answer comes as logged_in, logged_out
    // or disconnected. Called once per session object.
    virtual void logon(const LogonParams &p) = 0;
    // NewOrder (10.9). The session does not change any field.
    virtual CallStatus new_order(const to::NewOrder &m) = 0;
    // OrderCancelRequest (10.11).
    virtual CallStatus cancel(const to::OrderCancelRequest &m) = 0;
    // InstrumentInfoRequest (10.4) and RoutingUserInfoRequest (10.7): the
    // session sets the SessionID.
    virtual CallStatus instrument_info_request() = 0;
    virtual CallStatus routing_user_info_request() = 0;
    // ResendRequest (10.18) from begin on.
    virtual CallStatus resend(uint32_t begin) = 0;
    // Logout (10.2) with reason A6, then close. No events follow.
    virtual void logout() = 0;
  };

  // Where and how to connect. Credentials come from a deployment file outside
  // the repository (CxConfig.cpp).
  struct SessionConfig
  {
    std::string host;
    std::string port;
    // Currenex sends a Heartbeat every 3 s (5.3, p.8). Nothing for this long:
    // the link is down.
    int receive_timeout_ms = 10000;
    int connect_timeout_ms = 5000;
    // No Logon response for this long after connecting: the link is down.
    int logon_timeout_ms = 10000;
    size_t max_queued_bytes = 1 << 20; // above this, requests return Busy
  };

  using SessionFactory =
      std::function<std::unique_ptr<CxSession>(const SessionConfig &, SessionEvents &)>;

  // The real TCP session (TcpSession.cpp).
  SessionFactory tcp_session_factory();
}
