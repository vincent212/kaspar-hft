#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimBybit: an in-process Bybit V5 order API, for --sim runs, demos and the
// OM's simulator tests.
//
// It plays the venue side of BybitSession: answers each create and cancel as
// Bybit's REST does (retCode 0, or 110001 for a cancel of an order it does
// not have open), then publishes `order` updates (New, PartiallyFilled,
// Filled, Cancelled) and `execution` entries (execType Trade, a fresh execId
// each) in Bybit's JSON field names, and answers the reconciliation queries
// from its own records. Its thread is a frame::som::TimedRunner.
//
// What it does not model: the book (a limit order fills at its own price, a
// market order at market_price), PostOnly crossing, amend, self-match
// prevention, rate limits, the ordering between the `order` and `execution`
// topics (it publishes the execution first, then the order update).

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "bybit_oe/BybitSession.hpp"

namespace frame::som
{
  class TimedRunner;
}

namespace bybit::oe
{
  class SimBybit
  {
  public:
    struct Config
    {
      int ack_delay_ms = 0;
      int fill_delay_ms = 50;      // after the ack
      int fill_parts = 1;          // executions an order is filled in
      bool fill = true;            // false: limit orders rest until canceled (IOC/FOK cancel)
      std::string market_price = "100";
      bool numeric_order_ids = false; // spot style; false: UUID style (linear)
      std::set<std::string> reject_symbols;
    };

    explicit SimBybit(Config c);
    ~SimBybit();

    SessionFactory factory();
    static SessionFactory make_factory(Config c);

    uint64_t executions() const;

  private:
    class Session;
    struct SimOrder
    {
      std::string category, symbol, side, type, tif, link, order_id;
      double price = 0, qty = 0, cum = 0;
      std::string status;
      uint64_t session = 0;
    };

    void do_place(uint64_t sid, PlaceRequest r);
    void do_cancel(uint64_t sid, CancelRequest r);
    void do_fill(const std::string &link, double qty);
    void publish_order(const SimOrder &o, const std::string &reject = "EC_NoError",
                       const std::string &cancel_type = "UNKNOWN");
    std::string next_order_id();

    Config cfg_;
    mutable std::mutex mu_;
    std::map<uint64_t, SessionEvents *> sessions_;
    uint64_t next_session_ = 1;
    std::map<std::string, SimOrder> orders_; // by orderLinkId
    std::map<std::string, std::vector<std::string>> execs_; // link -> execution JSON
    uint64_t next_id_ = 1000000000000000000ull;
    uint64_t exec_n_ = 0;
    std::unique_ptr<frame::som::TimedRunner> runner_;
  };
}
