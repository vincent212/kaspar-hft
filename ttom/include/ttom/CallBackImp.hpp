/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

#include <iostream>

#include "ttom/OrdStatus.hpp"
#include "ttom/ExecType.hpp"
#include "ttom/CallBackIF.hpp"

#include "logger/act/Logger.hpp"
#include "actors/Actor.hpp"
#include "ttom/msg/ExecutionReport.hpp"
#include "ttom/msg/OrderCancelReject.hpp"

namespace ttfixom
{
  /**
   * @brief call back implementation
   *
   */
  struct CallBackImp : public CallBackIF
  {

    cfsmp fixom;

    CallBackImp(cfsmp _fixom) : fixom(_fixom) {}

    const char *get_name() const { return "TTOMCB"; }

    virtual void onLogon() noexcept override
    {
      log_omg("logged on");
    }

    virtual void onLogout() noexcept override
    {
      log_omg("logged out");
    }

    virtual void onNews(const std::string &text) noexcept override
    {
      log_omg("news: %s", text);
    }

    virtual void onExecutionReport(
        OrdStatus s,
        ExecType x,
        uint64_t clOrdId,
        const std::string &TTOrdId,
        uint64_t cmeOrdId,
        uint qty,
        uint cumQty,
        uint leavesQty,
        bool has_px,
        double px,
        const std::string &tranactionTime,
        const std::string &TTreceiveTime,
        const std::string &sendingTime,
        const std::string &text) noexcept override
    {
      log_omg("ExecutionReport s: %s, x: %s, clOrdId: %d, TTOrdId: %s, cmeOrdId: %d, qty: %d, cumQty: %d, leavesQty: %d, has_px: %d, px: %f, tranactionTime: %s, TTreceiveTime: %s, sendingTime: %s, txt: %s",
              to_string(s),
              to_string(x),
              clOrdId,
              TTOrdId,
              cmeOrdId,
              qty,
              cumQty,
              leavesQty,
              has_px,
              px,
              tranactionTime,
              TTreceiveTime,
              sendingTime,
              text);
      auto m = new ttfixom::msg::ExecutionReport(
          s,
          x,
          clOrdId,
          TTOrdId,
          cmeOrdId,
          qty,
          cumQty,
          leavesQty,
          has_px,
          px,
          tranactionTime,
          TTreceiveTime,
          sendingTime,
          text);
      fixom->fast_send(m, nullptr);
    }

    virtual void onOrderCancelReject(
        OrdStatus s,
        uint64_t clOrdId,
        uint64_t origClOrdId,
        const std::string &TTOrdId,
        uint64_t cmeOrdId,
        int cxlRejResponseTo,
        int cxlRejReason,
        const std::string &complianceText,
        const std::string &tranactionTime,
        const std::string &TTreceiveTime,
        const std::string &text) noexcept override
    {
      log_omg("OrderCancelReject s: %s, clOrdId: %d, origClOrdId: %d, TTOrdId: %s, cmeOrdId: %d, cxlRejResponseTo: %d, cxlRejReason: %d, complianceText: %s, tranactionTime: %s, TTreceiveTime: %s, txt: %s",
              to_string(s),
              clOrdId,
              origClOrdId,
              TTOrdId,
              cmeOrdId,
              cxlRejResponseTo,
              cxlRejReason,
              complianceText,
              tranactionTime,
              TTreceiveTime,
              text);
      auto m = new ttfixom::msg::OrderCancelReject(
          s,
          clOrdId,
          origClOrdId,
          TTOrdId,
          cmeOrdId,
          cxlRejResponseTo,
          cxlRejReason,
          complianceText,
          tranactionTime,
          TTreceiveTime,
          text);
      fixom->fast_send(m, nullptr);
    }
  };
}