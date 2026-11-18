/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeDropSession: a DropSession driven by the test. It has no thread and no
// socket: start() and stop() are recorded, and the test calls the event
// methods, which call the listener on the test's thread, as the real
// session's thread would. Like the real session, it numbers Sequenced Data
// from the sequence number of the last Login Accepted, and a relogin asks for
// the next number not yet delivered.

#include <cstdint>
#include <string>
#include <vector>

#include "nasdaq_post_trade/DropSession.hpp"

namespace nasdaq_post_trade::test
{
  class FakeDropSession : public DropSession
  {
  public:
    void start(const LoginParams &p, DropSessionListener *l) override
    {
      logins.push_back(p);
      listener = l;
      started = true;
      session = p.session;
      next_seq = p.next_seq;
    }
    void stop() override
    {
      started = false;
      ++stops;
    }

    // The venue's side of a login: accepted at seq (the venue may give a
    // lower number than requested, which resends lines).
    void accept(const std::string &s, uint64_t seq)
    {
      session = s;
      next_seq = seq;
      listener->on_login_accepted(s, seq);
    }
    void reject(char reason) { listener->on_login_rejected(reason); }

    // One Sequenced Data packet carrying a DROP line.
    void line(const std::string &payload)
    {
      listener->on_line(session, next_seq, payload);
      ++next_seq;
    }

    // A lost connection, then the session's own relogin: the request it
    // would send (same session, next sequence number) is recorded.
    void lose_connection()
    {
      listener->on_disconnected("connection reset", true);
      LoginParams p = logins.front();
      p.session = session;
      p.next_seq = next_seq;
      logins.push_back(p);
    }
    void end_of_session() { listener->on_end_of_session(session); }

    std::vector<LoginParams> logins;
    DropSessionListener *listener = nullptr;
    bool started = false;
    int stops = 0;
    std::string session;
    uint64_t next_seq = 0;
  };
}
