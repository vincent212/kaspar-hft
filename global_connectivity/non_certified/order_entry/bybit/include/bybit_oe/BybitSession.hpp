#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitSession: the connection to Bybit V5 as BybitOM sees it. NOT CERTIFIED.
//
// Requests go over signed REST (POST /v5/order/create, /v5/order/cancel; GET
// /v5/order/realtime and /v5/execution/list for reconciliation), which both
// the testnet and demo trading accept (demo has no WebSocket order entry).
// Reports come from the private WebSocket topics `order` and `execution`.
// Every REST answer is reported too: Bybit's create and cancel answers only
// say the request was accepted, so order state comes from the stream.
//
// Three implementations:
//   RestWsSession  the real one (RestWsSession.cpp): a private-stream thread
//                  and a REST worker thread
//   SimBybit       an in-process venue for --sim runs (SimBybit.hpp)
//   FakeBybit      the test double (tests/FakeBybit.hpp)
//
// Rules every implementation keeps:
//   - events are delivered on the implementation's own thread (the test
//     double may deliver from inside a call), never after the destructor
//     returns; the receiver only queues them
//   - order() and execution() carry one element of the topic's data array
//     (or of a REST list) as JSON text, unchanged
//   - a call made while not logged in returns Disconnected; nothing is queued
//     across a disconnect

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "bybit_oe/BybitAuth.hpp"

namespace bybit::oe
{
  enum class CallStatus
  {
    Success,      // queued for sending
    Busy,         // the request queue is full
    Disconnected, // not logged in; nothing was sent
  };

  enum class RequestKind
  {
    Place,
    Cancel,
    QueryOrder,
    QueryExecutions,
  };

  struct PlaceRequest
  {
    std::string category;      // spot | linear
    std::string symbol;
    std::string side;          // Buy | Sell
    std::string order_type;    // Limit | Market
    std::string qty;           // decimal string
    std::string price;         // decimal string; empty for Market
    std::string time_in_force; // GTC | IOC | FOK | PostOnly
    std::string order_link_id;
    int position_idx = 0;      // linear: 0 one-way mode
    bool reduce_only = false;

    std::string body() const; // the JSON body sent and signed (RestWsSession.cpp)
  };

  struct CancelRequest
  {
    std::string category;
    std::string symbol;
    std::string order_link_id;
    std::string body() const;
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_in() = 0;
    virtual void login_rejected(const std::string &why) = 0;
    virtual void disconnected(const std::string &why) = 0;
    virtual void busy() = 0;
    virtual void available() = 0;
    virtual void order(const std::string &json) = 0;     // one order update
    virtual void execution(const std::string &json) = 0; // one execution
    // The REST answer to a request: retCode 0 = accepted for processing.
    // ret_code -1: no answer (transport error); the outcome is unknown.
    virtual void response(RequestKind kind, const std::string &order_link_id, int ret_code,
                          const std::string &ret_msg) = 0;
  };

  class BybitSession
  {
  public:
    virtual ~BybitSession() = default;
    // Connect the private stream, authenticate and subscribe; the answer
    // comes as logged_in, login_rejected or disconnected.
    virtual void logon() = 0;
    virtual CallStatus place(const PlaceRequest &r) = 0;
    virtual CallStatus cancel(const CancelRequest &r) = 0;
    // Reconciliation: the answers come as order() and execution() events.
    virtual CallStatus query_order(const std::string &category, const std::string &order_link_id) = 0;
    virtual CallStatus query_executions(const std::string &category, const std::string &order_link_id) = 0;
    virtual void logout() = 0;
  };

  struct SessionConfig
  {
    std::string rest_host = "api-testnet.bybit.com";
    std::string ws_host = "stream-testnet.bybit.com";
    bool testnet = true;
    auth::Credentials credentials;
    int recv_window_ms = 5000;
    size_t max_queue = 64; // REST requests waiting; above it calls return Busy
  };

  using SessionFactory =
      std::function<std::unique_ptr<BybitSession>(const SessionConfig &, SessionEvents &)>;

  // The real session. Throws if testnet is set and a host is not a Bybit
  // test host.
  SessionFactory rest_ws_session_factory();
}
