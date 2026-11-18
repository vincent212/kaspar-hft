/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// DropSession: the connection that delivers DROP lines, as the client actor
// sees it. SoupSession is the real one (SoupBinTCP over TCP); the tests use a
// fake that delivers events on the test's own thread.
//
// A session runs on its own thread (the fake: on the caller's). It calls the
// listener for each event; the listener parses and sends actor messages, and
// never touches the actor's state.

#include <cstdint>
#include <string>
#include <string_view>

namespace nasdaq_post_trade
{
  struct LoginParams
  {
    std::string username;      // at most 6 characters
    std::string password;      // at most 10 characters
    std::string session;       // blank: the currently active session
    uint64_t next_seq = 1;     // 1: replay the session from its start; 0: most recent only
  };

  class DropSessionListener
  {
  public:
    virtual ~DropSessionListener() = default;
    virtual void on_login_accepted(const std::string &session, uint64_t next_seq) = 0;
    virtual void on_login_rejected(char reason) = 0; // A not authorized, S session not available
    virtual void on_line(const std::string &session, uint64_t seq, std::string_view payload) = 0;
    virtual void on_end_of_session(const std::string &session) = 0;
    virtual void on_disconnected(const std::string &reason, bool will_retry) = 0;
  };

  class DropSession
  {
  public:
    virtual ~DropSession() = default;

    // Connect and log in; returns at once. After a lost connection the session
    // logs in again by itself, to the same session at the next sequence number
    // it has not delivered. It stops for good on a login reject or an end of
    // session.
    virtual void start(const LoginParams &p, DropSessionListener *l) = 0;

    // Log out and stop; returns when the session's thread has finished and no
    // listener call is in progress.
    virtual void stop() = 0;
  };
}
