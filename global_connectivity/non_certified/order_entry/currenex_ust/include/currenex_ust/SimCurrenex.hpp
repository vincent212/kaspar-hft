/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SimCurrenex: an in-process Currenex US Treasury OUCH session, for --sim
// runs and demos.
//
// It plays the Currenex side of CxSession: answers the Logon, lists its
// instruments (Instrument Info) and routed users, acknowledges or rejects
// each NewOrder, fills it after a delay (in one or several trades, with or
// without the pending fill workflow), cancels on request and on disconnect,
// and replays on ResendRequest with GapFills in place of Logon and Heartbeat
// messages. Every message is a Currenex message in bytes, numbered as
// Currenex numbers its outbound messages: across connections when
// persistence is on, from 1 on each connection when it is off. Its thread is
// a frame::som::TimedRunner.
//
// What it does not model: a book (fills are at the order's limit rate),
// replaces, minimum and show amounts, spreads, credit, rate limits.

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "currenex_ust/CxSession.hpp"
#include "frame/som/TimedRunner.hpp"

namespace currenex::ouch
{
  class SimCurrenex
  {
  public:
    struct Instrument
    {
      std::string cusip;         // SecurityID
      std::string instrument_id; // e.g. OTR-10Y
      std::string state = "OTR";
      std::string tenor;
    };

    struct Config
    {
      int ack_delay_ms = 0;
      int fill_delay_ms = 50;     // after the ack
      int fill_parts = 1;         // trades an order is filled in
      bool fill = true;           // false: orders rest until cancelled (IOC orders are cancelled)
      bool pending_fill = false;  // the pending fill workflow: U, then t (UST OUCH 8.5)
      bool persistence = true;    // outbound numbering continues across connections (5.5)
      bool cancel_on_disconnect = true; // (8.7)
      int heartbeat_ms = 3000;    // 0: no heartbeats
      std::vector<Instrument> instruments; // empty: a default list
      std::vector<std::pair<std::string, int32_t>> routed_users; // RoutedUserId, RoutedUserKey
    };

    explicit SimCurrenex(Config c);
    ~SimCurrenex();

    // Sessions that log in to this simulated Currenex. SimCurrenex must
    // outlive them.
    SessionFactory factory();

    // A SessionFactory that owns its own SimCurrenex (for create_CurrenexOM).
    static SessionFactory make_factory(Config c);

    uint32_t last_sequence() const; // sequence number of the last message sent

  private:
    class Session;
    struct SimOrder
    {
      int32_t clid = 0;
      int64_t order_id = 0;
      int16_t index = 0;
      char side = 'B';
      char expire = 'G';
      int64_t amt = 0;  // cents
      int64_t rate = 0; // 9 decimals
      int64_t filled = 0;
      int32_t routed = NOT_ROUTED;
      bool live = false;
      int64_t open() const { return amt - filled; }
    };
    struct Logged
    {
      std::string bytes; // from the type code
      bool admin;        // Logon, Logout, Heartbeat: replaced by GapFill on resend
    };

    // Runner thread only, under mu_.
    void do_logon(uint64_t id, LogonParams p);
    void do_new_order(uint64_t id, to::NewOrder m);
    void do_cancel(uint64_t id, to::OrderCancelRequest m);
    void do_instruments(uint64_t id);
    void do_routing(uint64_t id);
    void do_resend(uint64_t id, uint32_t begin);
    void do_fill(int32_t clid, int64_t amt);
    void do_heartbeat(uint64_t id);
    void cancel_all(int16_t type);
    void emit(std::string bytes, bool admin = false); // number, log, deliver if a session is live
    void deliver(uint32_t seq, const std::string &bytes);
    const Instrument *instrument(int16_t index) const;

    // Any thread.
    CallStatus submit(uint64_t id, int delay_ms, std::function<void()> f);
    void detach(uint64_t id, bool cancel);

    Config cfg_;
    mutable std::mutex mu_;
    SessionEvents *live_ = nullptr; // the logged-in session's events
    uint64_t live_id_ = 0;
    uint64_t next_id_ = 1;
    int32_t session_id_ = 4242;
    std::set<uint64_t> alive_;     // sessions not yet destroyed
    std::vector<Logged> log_;      // sequence number = index + 1
    std::unordered_map<int32_t, SimOrder> orders_; // by ClOrderID
    std::set<int32_t> used_ids_;   // every ClOrderID seen
    int64_t next_order_id_ = 10'000'001;
    uint64_t next_exec_ = 1;
    frame::som::TimedRunner runner_; // last: its thread stops before the state above goes
  };
}
