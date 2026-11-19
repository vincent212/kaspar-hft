/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Messages from the DROP session's thread to NasdaqDropClient. The session
// thread parses; the actor applies.

#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "nasdaq_post_trade/DropLine.hpp"

namespace nasdaq_post_trade::msg
{
  struct LoginAccepted : public actors::MessageT<LoginAccepted>
  {
    std::string session;
    uint64_t next_seq = 0;
    LoginAccepted(std::string s, uint64_t n) : session(std::move(s)), next_seq(n) {}
  };

  struct LoginRejected : public actors::MessageT<LoginRejected>
  {
    char reason = 0;
    explicit LoginRejected(char r) : reason(r) {}
  };

  // One Sequenced Data packet. ok is false when the line did not parse; error
  // then says why, and report is empty.
  struct Report : public actors::MessageT<Report>
  {
    std::string session;
    uint64_t seq = 0;
    bool ok = false;
    std::string error;
    DropReport report;
    std::string raw;
  };

  struct EndOfSession : public actors::MessageT<EndOfSession>
  {
    std::string session;
    explicit EndOfSession(std::string s) : session(std::move(s)) {}
  };

  struct Disconnected : public actors::MessageT<Disconnected>
  {
    std::string reason;
    bool will_retry = false;
    Disconnected(std::string r, bool w) : reason(std::move(r)), will_retry(w) {}
  };
}
