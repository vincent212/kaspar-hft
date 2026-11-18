/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SimClearStreet: an in-process simulated Clear Street FIX session for --sim
// runs, demos and tests. It implements CsSession directly (no sockets) and
// answers on its own thread (frame::som::TimedRunner) as the public FIX API
// guide describes: Logon with the account list in Text, the order-type, time
// in force, strategy and extended-hours matrices, ExecutionReports with
// ExecType A/0/1/2/4/5/8, OrderCancelReject for unknown, terminal and pending
// orders, and a session Reject (35=3) for an unauthorized account.
//
// It is the guide as read for this OM (feed.md), not Clear Street's
// behaviour: anything the guide leaves open, the simulator does one way.

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "clearstreet_om/CsSession.hpp"
#include "frame/som/TimedRunner.hpp"

namespace clearstreet::om
{
  struct SimConfig
  {
    std::set<std::string> accounts{"SIM1"}; // authorized accounts, listed in the Logon's Text
    int ack_ms = 1;                         // delay of every first report
    int fill_ms = 20;                       // delay between fills; < 0: never fill
    int fills = 2;                          // an order fills in this many executions
    double market_px = 100.0;               // fill price of market orders
    bool queue_orders = false;              // first report 150=A (Pending New) instead of 150=0
  };

  class SimClearStreet : public CsSession
  {
  public:
    SimClearStreet(SimConfig cfg, SessionEvents &ev);
    ~SimClearStreet() override;

    void start() override;
    void stop() override;
    CallStatus send_new(const NewOrderReq &r) override;
    CallStatus send_cancel(const CancelReq &r) override;
    CallStatus send_replace(const ReplaceReq &r) override;

    // Clear Street's side, for demos and tests.
    void cancel_all(const std::string &text); // unsolicited cancels (no OrigClOrdID)
    void drop();                              // connection lost: logged_out, requests refused
    void restore();                           // logged on again

    // Why the guide's matrices refuse a request; empty if they accept it.
    static std::string check(const NewOrderReq &r);

  private:
    struct Ord
    {
      NewOrderReq req;
      std::string order_id;
      std::string current; // current ClOrdID
      double qty = 0, cum = 0, notional = 0;
      char status = '0';
      bool pending = false; // cancel or replace in flight (never true: the sim answers at once)
    };

    void emit_exec(Ord &o, char exec_type, const std::string &orig, double last_qty, double last_px,
                   const std::string &text = "");
    void schedule_fill(const std::string &order_id, int n);
    ExecReport base(const Ord &o, char exec_type) const;
    bool terminal(const Ord &o) const { return o.status == '2' || o.status == '4' || o.status == '8'; }
    double px_of(const Ord &o) const;

    SimConfig cfg_;
    SessionEvents &ev_;
    std::unique_ptr<frame::som::TimedRunner> run_;
    std::mutex mu_;
    bool up_ = false, stopped_ = false;
    uint64_t seq_ = 0, ids_ = 0;
    std::set<std::string> clordids_;               // every ClOrdID seen (uniqueness)
    std::map<std::string, Ord> orders_;            // OrderID -> order
    std::map<std::string, std::string> by_clordid_; // current ClOrdID -> OrderID
  };

  SessionFactory sim_session_factory(SimConfig cfg);
}
