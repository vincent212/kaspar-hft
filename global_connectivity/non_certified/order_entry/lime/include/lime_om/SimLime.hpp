/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SimLime: an in-process Lime trading platform behind LimeSession, for
// --sim runs and demos.
//
// It plays the Lime side of the Trading API: accepts the login, accepts or
// rejects each order, fills it after a delay (in one or several executions,
// at the limit price, or at sim_market_price for a market order), cancels on
// request and, when asked at login, on disconnect. It numbers every event
// with an eventId for the day, keeps them, and replays from the eventId a
// new session asks for (TO 3). Its thread is a frame::som::TimedRunner.
//
// Not modelled: a book, routes and their rules, algos (accepted and filled
// like any order), replaces, busts, busy connections.

#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "frame/som/TimedRunner.hpp"
#include "lime_om/LimeApi.hpp"

namespace lime
{
  class SimLime
  {
  public:
    struct Config
    {
      int ack_delay_ms = 0;
      int fill_delay_ms = 50;   // after the ack
      int fill_parts = 1;       // executions an order is filled in
      bool fill = true;         // false: orders rest until cancelled (IOC orders are cancelled)
      ScaledPrice market_price = 100 * PRICE_SCALE; // fill price of market orders
      std::string reject_route = "REJECT"; // orders to this route are rejected
    };

    explicit SimLime(Config c);
    ~SimLime();

    // Sessions on this simulated platform. SimLime must outlive them.
    SessionFactory factory();
    // A factory that owns its own SimLime (for create_LimeOM).
    static SessionFactory make_factory(Config c);

    uint64_t last_event_id() const;

  private:
    class Session;
    struct SimOrder
    {
      OrderId id = 0, lime_id = 0;
      std::string symbol;
      Side side = Side::Buy;
      int qty = 0, filled = 0;
      ScaledPrice price = 0;
      Tif tif = Tif::Day;
      bool live = false;
      int open() const { return qty - filled; }
    };
    struct Logged
    {
      std::function<void(SessionEvents &, uint64_t)> deliver;
    };

    // Runner thread only, under mu_.
    void do_login(uint64_t sid, LoginParams p);
    void do_place(NewOrder o);
    void do_cancel(OrderId id);
    void do_cancel_all();
    void do_fill(OrderId id, int qty);
    void emit(std::function<void(SessionEvents &, uint64_t)> f); // number, log, deliver to the live session
    void cancel_open(const char *why);

    // Any thread.
    CallStatus submit(uint64_t sid, int delay_ms, std::function<void()> f);
    void detach(uint64_t sid);

    Config cfg_;
    mutable std::mutex mu_;
    SessionEvents *live_ = nullptr;
    uint64_t live_sid_ = 0;
    bool cancel_on_disconnect_ = true;
    uint64_t next_sid_ = 1;
    std::set<uint64_t> alive_;
    std::unordered_map<uint64_t, SessionEvents *> events_of_;
    std::vector<Logged> log_; // eventId = index + 1
    std::unordered_map<OrderId, SimOrder> orders_;
    OrderId next_lime_id_ = 7'000'000'001ULL;
    uint64_t next_exec_ = 1;
    frame::som::TimedRunner runner_; // last: its thread stops before the state above goes
  };
}
