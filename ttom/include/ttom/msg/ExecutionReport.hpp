#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "chutil/PooledObject.hpp"
#include "ttom/ExecType.hpp"
#include "ttom/OrdStatus.hpp"

namespace ttfixom::msg
{
  struct ExecutionReport : public chutil::PooledObject<ExecutionReport, actors::MessageT<ExecutionReport>>
  {
    OrdStatus s;
    ExecType x;
    uint64_t clOrdId;
    std::string TTOrdId;
    uint64_t cmeOrdId;
    uint qty;
    uint cumQty;
    uint leavesQty;
    bool has_px;
    double px;
    std::string tranactionTime;
    std::string TTreceiveTime;
    std::string sendingTime;
    std::string text;
    ExecutionReport(
        OrdStatus _s,
        ExecType _x,
        uint64_t _clOrdId,
        const std::string &_TTOrdId,
        uint64_t _cmeOrdId,
        uint _qty,
        uint _cumQty,
        uint _leavesQty,
        bool _has_px,
        double _px,
        const std::string &_tranactionTime,
        const std::string &_TTreceiveTime,
        const std::string &_sendingTime,
        const std::string &_text)
        : s(_s),
          x(_x),
          clOrdId(_clOrdId),
          TTOrdId(_TTOrdId),
          cmeOrdId(_cmeOrdId),
          qty(_qty),
          cumQty(_cumQty),
          leavesQty(_leavesQty),
          has_px(_has_px),
          px(_px),
          tranactionTime(_tranactionTime),
          TTreceiveTime(_TTreceiveTime),
          sendingTime(_sendingTime),
          text(_text)
    {
    }
  };
}
