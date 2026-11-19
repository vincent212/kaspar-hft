#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimCbx: an in-process Coinbase Exchange FIX order-entry session, for --sim
// runs and demos. It plays the venue side of CbxSession: logs a client on,
// answers NewOrderSingle with ExecutionReport New (or Rejected), fills after a
// delay in one or several Trade reports, cancels on OrderCancelRequest
// (OrderCancelReject for an order it does not know or that is done), answers
// OrderStatusRequest, and expires IOC/FOK orders it does not fill. Reports
// carry the fields order-entry-messages5 lists for ExecutionReport, with
// UUID OrderIDs and ExecIDs. Its thread is a frame::som::TimedRunner.
//
// Not modelled: the book (fills are at the limit price, or market_price for
// a market order), partial cancels, replaces, self-trade prevention, rate
// limits, cancel-on-disconnect, resends.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <unordered_map>

#include "coinbase_exchange_oe/CbxSession.hpp"
#include "frame/som/TimedRunner.hpp"

namespace coinbase_exchange::oe
{
  class SimCbx
  {
  public:
    struct Config
    {
      int ack_delay_ms = 0;
      int fill_delay_ms = 50;     // after the ack
      int fill_parts = 1;
      bool fill = true;           // false: orders rest until canceled (IOC/FOK expire)
      std::string market_price = "100.00";
      std::set<std::string> reject_symbols;
    };

    explicit SimCbx(Config c);
    ~SimCbx();

    SessionFactory factory();
    static SessionFactory make_factory(Config c);
    uint64_t reports() const;

  private:
    class Session;
    struct SimOrder
    {
      std::string clordid, order_id, symbol, price, qty;
      char side = '1', ord_type = '2', tif = '1';
      int64_t qty_e8 = 0, filled_e8 = 0; // in 1e-8
      bool live = false;
    };

    void do_new(uint64_t id, fix::FixMsg m);
    void do_cancel(uint64_t id, fix::FixMsg m);
    void do_status(uint64_t id, fix::FixMsg m);
    void do_fill(std::string order_id, int64_t qty_e8);
    void emit(fix::FixMsg m);
    fix::FixMsg report(const SimOrder &o, char exec_type, char ord_status);
    std::string uuid();
    CallStatus submit(uint64_t id, int delay_ms, std::function<void()> f);
    void detach(uint64_t id);

    Config cfg_;
    mutable std::mutex mu_;
    SessionEvents *live_ = nullptr;
    uint64_t live_id_ = 0, next_id_ = 1, reports_ = 0;
    std::set<uint64_t> alive_;
    std::unordered_map<std::string, SimOrder> orders_;          // by OrderID
    std::unordered_map<std::string, std::string> by_clordid_;  // ClOrdID -> OrderID
    std::mt19937_64 rng_{12345};
    frame::som::TimedRunner runner_; // last
  };
}
