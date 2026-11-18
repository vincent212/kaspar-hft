#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitSession: the connection to Deribit's JSON-RPC WebSocket, as
// DeribitOM sees it (feed.md 2 and 4).
//
//   logon()    connect wss://<host>/ws/api/v2; public/auth (client_signature);
//              private/subscribe user.orders.any.any.raw and
//              user.trades.any.any.raw; public/set_heartbeat; optionally
//              private/enable_cancel_on_disconnect (scope connection)
//   call()     one JSON-RPC request with the OM's id: private/buy,
//              private/sell, private/cancel or private/cancel_by_label (any
//              other method is refused with Disconnected and nothing sent)
//   recover()  the order's trades (private/get_user_trades_by_order) then its
//              state (private/get_order_state), or, with no order_id yet,
//              private/get_order_state_by_label first; delivered as trade and
//              order_update events, trades first. Used after a reconnect,
//              because the user channels resend nothing on subscribe.
//
// Implementations: DeribitWsSession (the real one), SimDeribit (an
// in-process venue for --sim runs) and FakeDeribit (tests).
//
// Rules every implementation keeps:
//   - events come on the implementation's own thread (the fake may also call
//     them from inside a call), never after its destructor has returned;
//   - rpc_result carries the response as received: {"id", "result"} or
//     {"id", "error": {"code", "message"}};
//   - order_update carries one order object (user.orders data, a response's
//     order, or a recovered state); trade one trade object;
//   - nothing is queued across a disconnect: a call while not logged in
//     returns Disconnected.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace deribit::oe
{
  struct DeribitOMConfig;

  enum class CallStatus
  {
    Success,
    Busy,
    Disconnected,
  };

  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void logged_in() = 0;
    virtual void login_rejected(const std::string &code, const std::string &msg) = 0;
    virtual void disconnected(const std::string &reason) = 0;
    virtual void busy() = 0;
    virtual void available() = 0;
    virtual void rpc_result(const nlohmann::json &r) = 0;
    virtual void order_update(const nlohmann::json &order) = 0;
    virtual void trade(const nlohmann::json &trade) = 0;
  };

  class DeribitSession
  {
  public:
    virtual ~DeribitSession() = default;
    virtual void logon() = 0;
    virtual CallStatus call(int64_t id, const std::string &method, const nlohmann::json &params) = 0;
    virtual CallStatus recover(const std::string &order_id, const std::string &label, const std::string &currency) = 0;
    virtual void logout() = 0;
  };

  // The methods call() accepts.
  bool order_method(const std::string &m);

  using SessionFactory =
      std::function<std::unique_ptr<DeribitSession>(const DeribitOMConfig &, SessionEvents &)>;

  // The real session. It refuses to connect (login_rejected, nothing sent)
  // when the endpoint fails Endpoint::check() or the key file cannot be read.
  SessionFactory ws_session_factory();
}
