/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LimeApi: the connection to one Lime account through the Lime C++ Trading
// API, as LimeOM sees it.
//
// Source: "Lime C++ Trading Api Overview", version 1.78.0, 3/8/2022
// (docs.lime.co/cpp/LimeTradingApiOverview.pdf; "TO" below), with the
// Lime Direct Python SDK and Java javadoc pages on docs.lime.co for what the
// C++ overview leaves out (ExecType values, the bust and correction
// semantics). The vendor's headers and library are not public; this
// interface is written from the documents.
//
// The vendor API (TO 2, 3): one TradingApi object per account; constructing
// it connects and logs in; callbacks arrive on the API's single callback
// thread; calling into the API from a callback is prohibited, and the
// object must be destroyed on the thread that constructed it. After a
// connection failure the object is dead and a new one is constructed, with
// the eventId to resume from.
//
// Three implementations:
//   SdkSession  the real one over the vendor SDK (src/SdkSession.cpp, built
//               only with LIME_SDK_HEADER set; see the Makefile)
//   SimLime     an in-process Lime for --sim runs (SimLime.hpp)
//   FakeLime    the test double (tests/FakeLime.hpp)
//
// Rules every implementation keeps:
//   - the factory call is the TradingApi constructor: it starts the
//     connection and login; the answer comes as an event
//   - events are delivered on the implementation's own thread (the test
//     double may deliver from the test thread), never from inside a call,
//     and never after the session's destructor has returned
//   - a call made while the session is not logged in returns
//     ConnectionError and sends nothing (TO 4)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace lime
{
  using OrderId = uint64_t;     // client order id; unique per account per day (TO 4.1)
  using ScaledPrice = int64_t;  // dollars * PRICE_SCALE (TO 4.1)
  constexpr int64_t PRICE_SCALE = 10000; // priceScalingFactor
  constexpr ScaledPrice MARKET_PRICE = 0; // "Setting price to zero has the same effect" as marketPrice (TO 4.1)

  // TO 4.1 enum Side, in its order.
  enum class Side : uint8_t
  {
    Buy,
    Sell,
    SellShort,
    SellShortExempt,
    BuyToCover,
  };

  // The subset of OrderProperties::TimeInForce (TO 4.1) and
  // AlgoOrderProperties::TimeInForce (TO 4.3) this OM sends.
  enum class Tif : uint8_t
  {
    Day,
    Opg,         // on the open
    Ioc,
    ExtendedDay,
    AtTheClose,
  };

  // TO 4 enum CallStatus.
  enum class CallStatus : uint8_t
  {
    Success,
    ConnectionBusy,  // buffers full: wait for available()
    ConnectionError, // down, or not logged in; nothing was sent
  };

  // FillInfo::execType. The C++ overview names Fill, Bust and Correction
  // (TO 5) but not the values; 0, 1, 2 are from the Python SDK page.
  enum class ExecType : uint8_t
  {
    Fill,
    Bust,
    Correction,
  };

  // The TradingApi constructor's arguments (TO 3).
  struct LoginParams
  {
    std::string account;
    std::string user;
    std::string password;
    // Next eventId wanted: 0 continues without replay, 1 replays the day,
    // n replays from n (TO 3).
    uint64_t event_id = 0;
    bool cancel_all_on_disconnect = true;
    std::string host;          // empty: the Lime platform on this host (nullHostname)
    bool infiniband = false;   // transportTypeInfiniband instead of TCP
  };

  // placeOrder (TO 4.1) or, with a strategy, placeAlgoOrder (TO 4.3).
  struct NewOrder
  {
    OrderId id = 0;
    std::string route;    // ExDestination (FIX manual appendix C), or the algo venue
    std::string symbol;   // CMS, CQS or Nasdaq symbology (FIX manual appendix A)
    Side side = Side::Buy;
    int quantity = 0;
    ScaledPrice price = MARKET_PRICE; // > 0: limit
    Tif tif = Tif::Day;
    std::string strategy;      // non-empty: placeAlgoOrder
    bool sale_affirm = false;  // OrderProperties::saleAffirm (DVP accounts)
    bool regular_session_only = false;
    std::string client_data1;  // echoed on fills (FIX 9050)
  };

  // FillInfo (TO 5).
  struct FillInfo
  {
    std::string symbol;
    Side side = Side::Buy;
    int last_shares = 0;
    ScaledPrice last_price = 0;
    int left_qty = 0;
    int liquidity = 0;
    uint64_t transact_time_ms = 0;  // ms since the Unix epoch
    uint64_t transact_time_ns = 0;  // the same, ns
    ExecType exec_type = ExecType::Fill;
    std::string exec_id;            // for a bust or correction: the original execution's
    std::string last_market;
  };

  // The Listener callbacks (TO 3, 5). event_id is 0 on the callbacks the API
  // generates itself (TO 3 footnote 2), which are not replayed.
  struct SessionEvents
  {
    virtual ~SessionEvents() = default;
    virtual void login_accepted(uint64_t event_id) = 0;
    virtual void login_failed(const std::string &reason) = 0;
    virtual void connection_failed(const std::string &reason) = 0; // the session is dead
    virtual void busy() = 0;
    virtual void available() = 0;

    virtual void accepted(OrderId id, OrderId lime_id, uint64_t event_id) = 0;
    virtual void filled(OrderId id, const FillInfo &f, uint64_t event_id) = 0;
    virtual void canceled(OrderId id, const std::string &reason, uint64_t event_id) = 0;
    virtual void partial_canceled(OrderId id, int left_qty, uint64_t event_id) = 0;
    virtual void replaced(OrderId id, OrderId replace_id, OrderId lime_id, uint64_t event_id) = 0;
    virtual void rejected(OrderId id, const std::string &reason, uint64_t event_id) = 0;
    virtual void cancel_rejected(OrderId id, const std::string &reason, uint64_t event_id) = 0;
    virtual void replace_rejected(OrderId id, OrderId replace_id, const std::string &reason,
                                  uint64_t event_id) = 0;
    // Orders entered outside the API (Lime Portal), if the account is
    // configured for it (TO 5).
    virtual void manual_order(OrderId id, const std::string &symbol, const std::string &route, int quantity,
                              ScaledPrice price, Side side, uint64_t event_id) = 0;
    virtual void manual_replace(OrderId id, OrderId replace_id, int quantity, ScaledPrice price,
                                uint64_t event_id) = 0;
  };

  class LimeSession
  {
  public:
    virtual ~LimeSession() = default; // stops the callback thread: no events after it returns
    virtual CallStatus place(const NewOrder &o) = 0;
    // cancelOrder (TO 4.5): by the order's own id; Lime assigns the cancel's id.
    virtual CallStatus cancel(OrderId id) = 0;
    virtual CallStatus cancel_all() = 0; // cancelAllOpenOrders (TO 4.11)
  };

  // Constructs the session, which connects and logs in (the TradingApi
  // constructor). May throw (TO 6).
  using SessionFactory = std::function<std::unique_ptr<LimeSession>(const LoginParams &, SessionEvents &)>;

  // The real session over the vendor SDK. Throws std::runtime_error when the
  // binary was built without it.
  SessionFactory sdk_session_factory();
  bool sdk_available();

  const char *to_string(Side s);
  const char *to_string(Tif t);
  const char *to_string(ExecType t);
}
