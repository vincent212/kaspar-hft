/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SimOuch: an in-process Nasdaq OUCH port, for --sim runs and demos.
//
// It plays the venue side of OuchSession: logs a client in, accepts or
// rejects each Enter Order, fills it after a delay (in one or several
// executions), applies Cancel Order requests, and answers Account Query.
// Every report is an OUCH 5.0 message in bytes, numbered as SoupBinTCP
// numbers Sequenced Data, and kept in a log that a later login replays from
// the sequence number it asks for. Its thread is a frame::som::TimedRunner.
//
// What it does not model: the book (fills are at the order's limit price,
// or at market_price for a market order, whatever the market), crosses,
// partial cancels, replaces, self-match prevention, rate limits.

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "frame/som/TimedRunner.hpp"
#include "nasdaq_ouch/OuchSession.hpp"

namespace nasdaq::ouch
{
  class SimOuch
  {
  public:
    struct Config
    {
      int ack_delay_ms = 0;
      int fill_delay_ms = 50;   // after the ack
      int fill_parts = 1;       // executions an order is filled in
      bool fill = true;         // false: orders rest until canceled (IOC orders are canceled)
      uint64_t market_price = 100 * 10'000; // fill price of a market order, OUCH units ($100.00)
      std::set<std::string> reject_symbols; // Enter Orders for these are rejected (Invalid Symbol)
    };

    explicit SimOuch(Config c);
    ~SimOuch();

    // Sessions that log in to this simulated port. SimOuch must outlive them.
    SessionFactory factory();

    // A SessionFactory that owns its own SimOuch (for create_OuchOM).
    static SessionFactory make_factory(Config c);

    uint64_t messages() const; // sequenced messages sent so far

  private:
    class Session;
    struct SimOrder
    {
      std::string symbol;
      char side = 'B';
      char tif = '0';
      uint64_t price = 0;
      int shares = 0;
      int executed = 0;
      int decremented = 0;
      bool live = false;
      int open() const { return shares - executed - decremented; }
    };

    // Runner thread only, under mu_.
    void do_logon(uint64_t id, LogonParams p);
    void do_enter(uint64_t id, to_nasdaq::EnterOrder m);
    void do_cancel(uint64_t id, to_nasdaq::CancelOrderRequest m);
    void do_account_query(uint64_t id);
    void do_fill(uint32_t urn, int qty);
    void emit(std::string bytes); // log, and deliver if a session is live
    uint64_t now_ns() const;

    // Any thread.
    CallStatus submit(uint64_t id, int delay_ms, std::function<void()> f);
    void detach(uint64_t id);

    Config cfg_;
    mutable std::mutex mu_;
    SessionEvents *live_ = nullptr; // the logged-in session's events
    uint64_t live_id_ = 0;
    uint64_t next_id_ = 1;
    std::set<uint64_t> alive_;      // sessions not yet destroyed
    std::vector<std::string> log_; // sequence number = index + 1
    std::unordered_map<uint32_t, SimOrder> orders_;
    uint32_t last_urn_ = 0;
    uint64_t next_ref_ = 1000001;
    uint64_t next_match_ = 5000001;
    frame::som::TimedRunner runner_; // last: its thread stops before the state above goes
  };
}
