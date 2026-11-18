#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimKraken: an in-process Kraken for --sim runs, demos and tests, behind
// KrakenSession. Not a model of Kraken's matching: it acknowledges every
// order, fills it after fill_ms in fill_parts executions (unless it is
// cancelled first, or its symbol is in no_fill), cancels on request, and
// rejects symbols in reject_symbols. Reports come from a TimedRunner thread,
// as a real session's would.
//
// The venue state (orders, executions) lives in SimKraken::Venue, shared by
// every session the factory makes, so a reconnecting OM sees what happened
// while it was away: on logon the session sends the open orders and the last
// 50 executions as a snapshot, as Kraken's executions channel does.

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "frame/som/TimedRunner.hpp"
#include "kraken_oe/KrakenSession.hpp"

namespace kraken::oe
{
  class SimKraken
  {
  public:
    struct Config
    {
      int ack_ms = 0;
      int fill_ms = 50;
      int fill_parts = 1;
      std::set<std::string> reject_symbols;
      std::set<std::string> no_fill; // symbols whose orders rest until cancelled
    };

    struct Venue; // shared state

    // A factory whose sessions all trade on one simulated venue.
    static SessionFactory make_factory(const Config &cfg);
    // The same, also giving the venue (for tests: drop the link, count orders).
    static SessionFactory make_factory(const Config &cfg, std::shared_ptr<Venue> &venue);

    // Test hooks on a venue.
    static void drop_connection(Venue &v);         // the current session disconnects
    static size_t orders(Venue &v);                // orders ever entered
    static size_t executions(Venue &v);
  };
}
