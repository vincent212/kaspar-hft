/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "SdkStub.hpp"

namespace LimeBrokerage
{
  const CpuSet CpuSet::null{};

  TradingApi::TradingApi(Listener &listener, const std::string &account, const std::string &user,
                         const std::string &password, uint64_t eventId, bool cancelAllOnDisconnect,
                         const std::string &hostname, TransportType transportType, const CpuSet &)
  {
    auto &s = sdkstub::state();
    std::lock_guard<std::mutex> l(s.mu);
    if (s.throw_in_ctor)
      throw TradingApiException("stub: version mismatch");
    s.ctors.push_back({account, user, password, hostname, eventId, cancelAllOnDisconnect, transportType,
                       std::this_thread::get_id()});
    s.listener = &listener;
  }

  TradingApi::~TradingApi()
  {
    auto &s = sdkstub::state();
    std::lock_guard<std::mutex> l(s.mu);
    s.dtors.push_back(std::this_thread::get_id());
    s.listener = nullptr;
  }

  namespace
  {
    CallStatus record(sdkstub::Call c)
    {
      auto &s = sdkstub::state();
      std::lock_guard<std::mutex> l(s.mu);
      if (s.throw_in_call)
        throw TradingApiException("stub: internal error");
      if (s.status == statusSuccess)
        s.calls.push_back(std::move(c));
      return s.status;
    }
  }

  CallStatus TradingApi::placeOrder(OrderId orderId, const std::string &route, const std::string &symbol, Side side,
                                    int quantity, ScaledPrice price, const OrderProperties &p)
  {
    sdkstub::Call c;
    c.what = "placeOrder";
    c.id = orderId;
    c.route = route;
    c.symbol = symbol;
    c.side = side;
    c.quantity = quantity;
    c.price = price;
    c.tif = p.getTimeInForce();
    c.sale_affirm = p.isSaleAffirm();
    c.regular_session_only = p.isRegularSessionOnly();
    return record(c);
  }

  CallStatus TradingApi::placeAlgoOrder(OrderId orderId, const std::string &route, const std::string &symbol,
                                        Side side, int quantity, const std::string &strategy, ScaledPrice price,
                                        const AlgoOrderProperties &p)
  {
    sdkstub::Call c;
    c.what = "placeAlgoOrder";
    c.id = orderId;
    c.route = route;
    c.symbol = symbol;
    c.side = side;
    c.quantity = quantity;
    c.strategy = strategy;
    c.price = price;
    c.tif = p.getTimeInForce();
    c.sale_affirm = p.isSaleAffirm();
    c.regular_session_only = p.isRegularSessionOnly();
    return record(c);
  }

  CallStatus TradingApi::cancelOrder(OrderId orderId)
  {
    sdkstub::Call c;
    c.what = "cancelOrder";
    c.id = orderId;
    return record(c);
  }

  CallStatus TradingApi::cancelAllOpenOrders()
  {
    sdkstub::Call c;
    c.what = "cancelAllOpenOrders";
    return record(c);
  }
}

namespace sdkstub
{
  State &state()
  {
    static State s;
    return s;
  }

  void reset()
  {
    auto &s = state();
    std::lock_guard<std::mutex> l(s.mu);
    s.calls.clear();
    s.ctors.clear();
    s.dtors.clear();
    s.listener = nullptr;
    s.status = LimeBrokerage::statusSuccess;
    s.throw_in_ctor = false;
    s.throw_in_call = false;
  }
}
