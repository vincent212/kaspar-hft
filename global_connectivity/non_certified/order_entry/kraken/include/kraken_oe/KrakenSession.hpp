#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenSession: one connection to Kraken's order API, as KrakenOM sees it.
//
// Implementations:
//   spot_session_factory     spot, WebSocket v2 (wss://ws-auth.kraken.com/v2):
//                            add_order / cancel_order and the `executions`
//                            channel. Production only: Kraken has no spot
//                            testnet. Never run by the agent.
//   futures_session_factory  Kraken Futures REST (sendorder, cancelorder;
//                            fills and orders/status polled); refuses any
//                            host but demo-futures.kraken.com while
//                            testnet = true. (KrakenSessions.hpp)
//   SimKraken             in-process venue for --sim runs and tests
//   FakeKraken            the test double (tests/FakeKraken.hpp)
//
// Every implementation turns what the venue says into Reports (below), on its
// own thread (a test double may also report from inside a call). The OM only
// queues them. Requests are never queued across a disconnect: a call made
// while the session is not logged in returns Disconnected.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace kraken::oe
{
  enum class CallStatus
  {
    Success,      // handed to the connection
    Busy,         // the outbound queue is full; wait for available()
    Disconnected, // not logged in; nothing was sent
  };

  enum class Tif
  {
    GTC,
    IOC,
    FOK,
  };

  struct NewOrder
  {
    std::string cl_ord_id; // KrakenOM::client_order_id(cid)
    std::string symbol;    // venue symbol: BTC/USD (spot), PF_XBTUSD (futures)
    bool buy = true;
    std::string qty;       // decimal text, in the venue's quantity unit
    std::string price;     // decimal text; empty for a market order
    bool market = false;
    Tif tif = Tif::GTC;
    bool post_only = false;
  };

  // One thing the venue said about one order, normalized.
  struct Report
  {
    enum Kind
    {
      Accepted,       // live at the venue (or acknowledged by the request's response)
      Rejected,       // the new order was refused
      Trade,          // one execution; exec_id, last_qty, last_price set
      Canceled,       // the order ended unfilled or partly filled (by request, expiry, the venue)
      Filled,         // the order is fully filled (status only; the Trades carry the quantities)
      CancelRejected, // a cancel was refused; the order may still be live
      Restated,       // the venue changed the order (amend, restatement); logged
      Other,          // anything else, logged
    };
    Kind kind = Other;
    std::string cl_ord_id, order_id; // either may be empty; at least one is set
    std::string exec_id, trade_id;
    std::string last_qty, last_price; // decimal text
    std::string symbol, reason, status;
    bool buy = true;
    bool maker = false;
    uint64_t time_ns = 0;
    bool snapshot = false; // part of a snapshot (replay after (re)connect)
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_in() = 0;
    virtual void login_failed(const std::string &reason) = 0; // a retry will not help (keys, permissions, host)
    virtual void disconnected(const std::string &reason) = 0;
    virtual void busy() = 0;
    virtual void available() = 0;
    virtual void report(const Report &r) = 0;
    // A gap in the venue's own sequence (spot executions `sequence`).
    virtual void gap(uint64_t expected, uint64_t got) = 0;
  };

  class KrakenSession
  {
  public:
    virtual ~KrakenSession() = default;
    // Connect, authenticate, subscribe; then logged_in, login_failed or
    // disconnected. Called once per session object.
    virtual void logon() = 0;
    virtual CallStatus add_order(const NewOrder &o) = 0;
    // Cancel one order by the venue's id, or by the client id when the venue
    // id is not known yet.
    virtual CallStatus cancel_order(const std::string &order_id, const std::string &cl_ord_id) = 0;
    virtual void logout() = 0;
  };

  using SessionFactory = std::function<std::unique_ptr<KrakenSession>(SessionEvents &)>;
}
