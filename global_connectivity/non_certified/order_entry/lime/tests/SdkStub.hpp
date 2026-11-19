/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SdkStub: an implementation of the TradingApi declared in
// sdk_doc/LimeTradingApiDoc.hh, for testing SdkSession. It records every
// call and lets the test play the API's callback thread by calling the
// Listener the latest TradingApi was built with.

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "LimeTradingApiDoc.hh"

namespace sdkstub
{
  struct Call
  {
    std::string what; // placeOrder, placeAlgoOrder, cancelOrder, cancelAllOpenOrders
    LimeBrokerage::OrderId id = 0;
    std::string route, symbol, strategy;
    LimeBrokerage::Side side = LimeBrokerage::sideBuy;
    int quantity = 0;
    LimeBrokerage::ScaledPrice price = 0;
    int tif = -1;
    bool sale_affirm = false, regular_session_only = false;
  };

  struct Ctor
  {
    std::string account, user, password, host;
    uint64_t event_id = 0;
    bool cancel_all = false;
    LimeBrokerage::TransportType transport = LimeBrokerage::transportTypeTcp;
    std::thread::id thread;
  };

  struct State
  {
    std::mutex mu;
    std::vector<Call> calls;
    std::vector<Ctor> ctors;
    std::vector<std::thread::id> dtors;
    LimeBrokerage::Listener *listener = nullptr; // of the live TradingApi
    LimeBrokerage::CallStatus status = LimeBrokerage::statusSuccess;
    bool throw_in_ctor = false;
    bool throw_in_call = false;
  };

  State &state();
  void reset();
}
