#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxSession: the connection to OKX's private WebSocket, as OkxOM sees it.
//
//   logon()   connect wss://<ws_host>/ws/v5/private, send the login op, and
//             on success subscribe to the `orders` channel (instType ANY)
//   place()   op "order" with one argument (WS / Place order)
//   cancel()  op "cancel-order" with one argument (WS / Cancel order)
//   recover() REST: the fills of one order (GET /api/v5/trade/fills?ordId=)
//             and its state (GET /api/v5/trade/order), delivered as
//             order_update events, fills first; used after a reconnect,
//             because the orders channel pushes nothing on subscribe
//
// Three implementations:
//   OkxWsSession  the real one (OkxWsSession.cpp)
//   SimOkx        an in-process OKX for --sim runs (SimOkx.hpp)
//   FakeOkx       the test double (tests/FakeOkx.hpp)
//
// Rules every implementation keeps:
//   - events are delivered on the implementation's own thread (the test
//     double may also deliver from inside a call), never after the session's
//     destructor has returned; the receiver only queues them
//   - order_update carries one element of the orders channel's `data` array
//     as received (or, from recover(), a fill or state in the same shape)
//   - nothing is queued across a disconnect: a call made while not logged in
//     and subscribed returns Disconnected

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace okx::oe
{
  struct OkxOMConfig;

  enum class CallStatus
  {
    Success,      // handed to the connection
    Busy,         // the outbound queue is full; wait for available()
    Disconnected, // not logged in; nothing was sent
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_in() = 0;                                  // login ok, orders channel subscribed
    virtual void login_rejected(const std::string &code, const std::string &msg) = 0;
    virtual void disconnected(const std::string &reason) = 0;
    virtual void busy() = 0;
    virtual void available() = 0;
    // A response to place() or cancel(): {"id","op","code","msg","data":[...]}.
    virtual void op_result(const nlohmann::json &r) = 0;
    // One orders-channel element, or a recovered fill or state.
    virtual void order_update(const nlohmann::json &d) = 0;
  };

  class OkxSession
  {
  public:
    virtual ~OkxSession() = default;
    virtual void logon() = 0;
    virtual CallStatus place(const std::string &req_id, const nlohmann::json &arg) = 0;
    virtual CallStatus cancel(const std::string &req_id, const nlohmann::json &arg) = 0;
    virtual CallStatus recover(const std::string &inst_id, const std::string &ord_id,
                               const std::string &cl_ord_id) = 0;
    virtual void logout() = 0;
  };

  using SessionFactory = std::function<std::unique_ptr<OkxSession>(const OkxOMConfig &, SessionEvents &)>;

  // The real session (OkxWsSession.cpp). It refuses to connect (disconnected
  // event, nothing sent) when the configuration fails OkxOMConfig::valid().
  SessionFactory ws_session_factory();
}
