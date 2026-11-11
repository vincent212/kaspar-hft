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

namespace ttfixom
{
  /**
   * @brief call back interface
   *
   */
  struct CallBackIF
  {

    virtual ~CallBackIF() = default;

    /**
     * @brief called when client is logged on
     *
     */
    virtual void onLogon() noexcept = 0;

    /**
     * @brief called when client is logged out
     *
     */
    virtual void onLogout() noexcept = 0;

    /**
     * @brief called when sequence id synchronization is complete and reset to 1
     * Client will not send messages to TT gateway until the news message is received
     * @see https://library.tradingtechnologies.com/tt-fix/general/Msg_News_B.html
     *
     */
    virtual void onNews(const std::string &) noexcept = 0;

    /**
     * @brief reply from the exchange indicating reason for report
     * and status of order. This can include acks, fills, and rejects
     * @see https://library.tradingtechnologies.com/tt-fix/gateway/Msg_ExecutionReport_8.html
     *
     * @param s
     * @param x
     * @param clOrdId
     * @param TTOrdId
     * @param cmeOrdId
     * @param qty = cumQty + leavesQty
     * @param cumQty how much traded
     * @param leavesQty available to trade
     * @param has_px if price was set
     * @param px fill price
     * @param tranactionTime time of transaction at CME
     * @param TTreceiveTime when TT received the message
     * @param sendingTime (ms)
     */
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
        const std::string &text) noexcept = 0;

    /**
     * @brief Order cancel request rejected
     * @see https://library.tradingtechnologies.com/tt-fix/gateway/Msg_OrderCancelReject_9.html
     *
     * @param s status
     * @param clOrdId clientorder id of the transaction to cancel
     * @param origClOrdId client id of order being cancelled
     * @param TTOrdId
     * @param cmeOrdId
     * @param cxlRejResponseTo
     * @param cxlRejReason
     * @param complianceText
     * @param tranactionTime
     * @param TTreceiveTime
     */
    virtual void onOrderCancelReject(
        OrdStatus s,
        uint64_t clOrdId,
        uint64_t origClOrdId,
        const std::string &ttOid,
        uint64_t cmeOrdId,
        int cxlRejResponseTo,
        int cxlRejReason,
        const std::string &complianceText,
        const std::string &tranactionTime,
        const std::string &TTreceiveTime,
        const std::string &text) noexcept = 0;

    /**
     * @brief Message reject due to likely client misconfiguration.
     * Not likely to happen in normal trading. One possible scenario as far
     * as the CME is concerned is a situation when order rate is higher than
     * what CME allows. High order rates will trigger a business reject.
     * @see https://library.tradingtechnologies.com/tt-fix/general/Msg_BusinessMessageReject_j.html
     *
     * @param rejReason
     * @param rejMsgTyp
     * @param rejText
     * @param refSeqNum
     */
    virtual void onBusinessReject(
        int rejReason,
        const std::string rejMsgTyp,
        const std::string rejText,
        int refSeqNum)
    {
      std::cout << "onBusinessReject:"
                << " rejReason=" << rejReason
                << " rejMsgTyp=" << rejMsgTyp
                << " rejText=" << rejText
                << " refSeqNum=" << refSeqNum
                << std::endl;
      std::cout.flush();
    }

    /**
     * @brief Message reject due to likely client misonfiguration. This functionality
     * was not tested.
     * @see https://library.tradingtechnologies.com/tt-fix/general/Msg_SessionLevelReject_3.html
     *
     * @param rejReason
     * @param rejMsgTyp
     * @param rejText
     * @param refSeqNum
     */
    virtual void onSessionReject(
        int rejReason,
        const std::string rejMsgTyp,
        const std::string rejText,
        int refSeqNum)
    {
      std::cout << "onSessionReject:"
                << " rejReason=" << rejReason
                << " rejMsgTyp=" << rejMsgTyp
                << " rejText=" << rejText
                << " refSeqNum=" << refSeqNum
                << std::endl;
      std::cout.flush();
    }
  };
}