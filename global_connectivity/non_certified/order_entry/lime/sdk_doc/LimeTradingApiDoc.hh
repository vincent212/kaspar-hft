/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// NOT THE VENDOR HEADER. The Lime C++ Trading API's headers are not public.
// This file transcribes, from "Lime C++ Trading Api Overview" v1.78.0
// (3/8/2022), sections 3 to 6, the declarations SdkSession.cpp uses, so that
// SdkSession compiles and can be tested against an implementation written
// for the tests (tests/SdkStub.cpp). Building against the real SDK means
// pointing LIME_SDK_HEADER at the vendor header instead; whatever differs is
// then a compile error to fix in SdkSession.cpp.
//
// Not in the document, so guessed here (marked GUESS):
//   - the header file names (the revision history names limeTypes.hh)
//   - the ExecType enumerator names (the document says "Fill, Bust, or
//     Correction"; the Python SDK gives the values 0, 1, 2)
//   - the values of marketPrice and samePrice (the document says a price of
//     zero means market, like marketPrice)
//   - CpuSet, AllowRouting, PositionEffect and the options classes are
//     reduced to what SdkSession needs

#include <cstdint>
#include <string>
#include <utility>

namespace LimeBrokerage
{
  typedef uint64_t OrderId;   // TO 4.1
  typedef int64_t ScaledPrice; // TO 4.1
  static const int priceScalingFactor = 10000;
  static const ScaledPrice marketPrice = 0; // GUESS (TO 4.1: zero has the same effect)

  enum Side // TO 4.1
  {
    sideBuy,
    sideSell,
    sideSellShort,
    sideSellShortExempt,
    sideBuyToCover
  };

  enum CallStatus // TO 4
  {
    statusSuccess,
    statusConnectionBusy,
    statusConnectionError
  };

  enum TransportType // TO 3
  {
    transportTypeTcp,
    transportTypeInfiniband
  };

  enum ExecType // GUESS: names; values from the Python SDK
  {
    execTypeFill = 0,
    execTypeBust = 1,
    execTypeCorrection = 2
  };

  enum PositionEffect // reduced
  {
    positionEffectNone
  };

  struct CpuSet // reduced
  {
    static const CpuSet null;
  };

  class OrderProperties // TO 4.1, reduced to the setters SdkSession calls
  {
  public:
    enum TimeInForce
    {
      timeInForceDay,
      timeInForceOpg,
      timeInForceIoc,
      timeInForceExtendedDay,
      timeInForceGoodTillDate,
      timeInForceAtTheClose,
      timeInForceTimeInMarket,
      timeInForceIntradayCross,
      timeInForceAtOpenThenDay = 9,
      timeInForceLateLimitOnOpen,
      timeInForceLateLimitOnClose,
      timeInForcePreOpenSession,
      timeInForcePostCloseSession,
      timeInForceExtendedTradingClose
    };
    void setTimeInForce(TimeInForce value) { timeInForce = value; }
    void setSaleAffirm(bool value) { saleAffirm = value; }
    void setRegularSessionOnly(bool value) { regularSessionOnly = value; }
    void setClientData1(const std::string &value) { clientData1 = value; }
    TimeInForce getTimeInForce() const { return timeInForce; }
    bool isSaleAffirm() const { return saleAffirm; }
    bool isRegularSessionOnly() const { return regularSessionOnly; }
    const std::string &getClientData1() const { return clientData1; }

  private:
    TimeInForce timeInForce = timeInForceDay;
    bool saleAffirm = false;
    bool regularSessionOnly = false;
    std::string clientData1;
  };

  class AlgoOrderProperties // TO 4.3, reduced
  {
  public:
    enum TimeInForce
    {
      timeInForceDay,
      timeInForceOpg,
      timeInForceIoc,
      timeInForceExtendedDay,
      timeInForceGoodTillDate,
      timeinForceAtTheClose, // sic (TO 4.3)
      timeInForceAtOpenThenDay = 9
    };
    void setTimeInForce(TimeInForce value) { timeInForce = value; }
    void setSaleAffirm(bool value) { saleAffirm = value; }
    void setRegularSessionOnly(bool value) { regularSessionOnly = value; }
    void setClientData1(const std::string &value) { clientData1 = value; }
    TimeInForce getTimeInForce() const { return timeInForce; }
    bool isSaleAffirm() const { return saleAffirm; }
    bool isRegularSessionOnly() const { return regularSessionOnly; }

  private:
    TimeInForce timeInForce = timeInForceDay;
    bool saleAffirm = false;
    bool regularSessionOnly = false;
    std::string clientData1;
  };

  struct OrderAckAttributes // TO 5
  {
    ScaledPrice adjustedPrice = 0;
    ScaledPrice displayPrice = 0;
    bool displayPriceAdjusted = false;
    std::string marketClOrdId;
    std::string marketOrderId;
    PositionEffect positionEffect = positionEffectNone;
    Side side = sideBuy;
  };

  struct FillInfo // TO 5
  {
    std::string symbol;
    Side side = sideBuy;
    int lastShares = 0;
    ScaledPrice lastPrice = 0;
    int leftQty = 0;
    int liquidity = 0;
    uint64_t transactTime = 0;
    ExecType execType = execTypeFill;
    std::string execId;
    std::string contraBroker;
    std::string lastMarket;
    std::string clientData1;
    std::string clientData2;
    std::string clientData3;
    std::string marketLiquidity;
    uint64_t transactTimeNanos = 0;
  };

  struct USOptionsFillInfo // reduced
  {
    int lastShares = 0;
  };

  struct ManualOrderInfo // TO 5
  {
    std::string symbol;
    std::string route;
    int quantity = 0;
    ScaledPrice price = 0;
    Side side = sideBuy;
  };

  struct ManualUSOptionsOrderInfo // reduced
  {
    std::string route;
    int quantity = 0;
  };

  class Listener // TO 3, 5
  {
  public:
    virtual ~Listener() {}
    // Required trading callbacks
    virtual void onOrderAccept(OrderId orderId, OrderId limeOrderId, const OrderAckAttributes &attributes,
                               uint64_t eventId) = 0;
    virtual void onOrderFill(OrderId orderId, const FillInfo &fillInfo, uint64_t eventId) = 0;
    virtual void onUSOptionsOrderFill(OrderId orderId, const USOptionsFillInfo &fillInfo, uint64_t eventId) = 0;
    virtual void onOrderCancel(OrderId orderId, uint64_t eventId) = 0;
    virtual void onOrderCancel(OrderId orderId, const std::string &reason, uint64_t eventId) = 0;
    virtual void onOrderPartialCancel(OrderId orderId, int leftQty, uint64_t eventId) = 0;
    virtual void onOrderReplace(OrderId orderId, OrderId replaceOrderId, OrderId limeReplaceOrderId,
                                const OrderAckAttributes &attributes, uint64_t eventId) = 0;
    virtual void onOrderReject(OrderId orderId, const std::string &reason, uint64_t eventId) = 0;
    // Optional trading callbacks
    virtual void onOrderCancelReject(OrderId, const std::string &, uint64_t) {}
    virtual void onOrderCancelReplaceReject(OrderId, OrderId, const std::string &, uint64_t) {}
    virtual void onManualOrder(OrderId, const ManualOrderInfo &, uint64_t) {}
    virtual void onManualUSOptionsOrder(OrderId, const ManualUSOptionsOrderInfo &, uint64_t) {}
    virtual void onManualOrderReplace(OrderId, OrderId, int, ScaledPrice, uint64_t) {}
    // Session callbacks
    virtual void onLoginAccepted() {}
    virtual void onLoginAccepted(uint64_t) {}
    virtual void onLoginFailed(const std::string &reason) = 0;
    virtual void onConnectionFailed(const std::string &reason) = 0;
    virtual void onConnectionBusy() {}
    virtual void onConnectionAvailable() {}
  };

  class TradingApiException // TO 6
  {
  public:
    explicit TradingApiException(const std::string &message) : message(message) {}
    const char *what() const { return message.c_str(); }

  private:
    const std::string message;
  };

  static const std::string nullHostname;

  class TradingApi // TO 3, 4
  {
  public:
    TradingApi(Listener &listener, const std::string &account, const std::string &user,
               const std::string &password, uint64_t eventId, bool cancelAllOnDisconnect,
               const std::string &hostname = nullHostname, TransportType transportType = transportTypeTcp,
               const CpuSet &callbackCpuset = CpuSet::null);
    ~TradingApi();

    CallStatus placeOrder(OrderId orderId, const std::string &route, const std::string &symbol, Side side,
                          int quantity, ScaledPrice price, const OrderProperties &properties);
    CallStatus placeAlgoOrder(OrderId orderId, const std::string &route, const std::string &symbol, Side side,
                              int quantity, const std::string &strategy, ScaledPrice price,
                              const AlgoOrderProperties &properties);
    CallStatus cancelOrder(OrderId orderId);
    CallStatus cancelAllOpenOrders();
  };
}
