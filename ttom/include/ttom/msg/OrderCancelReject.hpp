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
  struct OrderCancelReject : public chutil::PooledObject<OrderCancelReject, actors::MessageT<OrderCancelReject>>
  {
    OrdStatus s;
    uint64_t clOrdId;
    uint64_t origClOrdId;
    std::string TTOrdId;
    uint64_t cmeOrdId;
    int cxlRejResponseTo;
    int cxlRejReason;
    std::string complianceText;
    std::string tranactionTime;
    std::string TTreceiveTime;
    std::string text;
    OrderCancelReject(
        OrdStatus _s,
        uint64_t _clOrdId,
        uint64_t _origClOrdId,
        const std::string &_TTOrdId,
        uint64_t _cmeOrdId,
        int _cxlRejResponseTo,
        int _cxlRejReason,
        const std::string &_complianceText,
        const std::string &_tranactionTime,
        const std::string &_TTreceiveTime,
        const std::string &_text)
        : s(_s),
          clOrdId(_clOrdId),
          origClOrdId(_origClOrdId),
          TTOrdId(_TTOrdId),
          cmeOrdId(_cmeOrdId),
          cxlRejResponseTo(_cxlRejResponseTo),
          cxlRejReason(_cxlRejReason),
          complianceText(_complianceText),
          tranactionTime(_tranactionTime),
          TTreceiveTime(_TTreceiveTime),
          text(_text)
    {
    }
  };
}