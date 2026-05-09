#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <stdint.h>
#include "ilink_v8/FTI.h"

namespace m2::ilink
{
    /**
     * @brief Callback Interface for ILink
     * 
     * Member functions must be implemented for each message.
     * These are called from process_message_from_msgw().
     * 
    */
    struct CBIF
    {

        //
        // SESSION LAYER
        //

        virtual void sequence(
            uint32_t NextSeqNo,
            sbe::FTI::Value FaultToleranceIndicator,
            sbe::KeepAliveLapsed::Value KeepAliveLapsed
            ) = 0;


        /**
         * @brief negotiationResponse
         * @@see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Negotiation+Response
         *
         */
        virtual void negotiationResponse(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint32_t PreviousSeqNo,
            uint64_t PreviousUUID) = 0;

        /**
         * @brief negotiationReject
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Negotiation+Reject
         *
         */
        virtual void negotiationReject(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint16_t errorCodes,
            const std::string &Reason) = 0;

        /**
         * @brief establishementAck
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Establishment+Acknowledgment
         *
         */
        virtual void establishementAck(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint32_t PreviousSeqNo,
            uint64_t PreviousUUID,
            uint32_t NextSeqNo,
            uint16_t KeepAliveInterval) = 0;

        /**
         * @brief establishmentReject
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Establishment+Reject
         *
         */
        virtual void establishmentReject(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint32_t NextSeqNo,
            uint16_t errorCodes,
            const std::string &Reason) = 0;

        /**
         * @brief not applied
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Not+Applied
         *
         * @param UUID
         * @param FromSeqNo
         * @param MsgCount
         */

        virtual void notApplied(
            uint64_t UUID,
            uint32_t FromSeqNo,
            uint32_t MsgCount) = 0;

        /**
         * @brief Retransmission Request
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Retransmission
         *
         */
        virtual void retransmission(
            uint64_t UUID,
            uint64_t RequestTimestamp,
            uint64_t LastUUID,
            uint32_t FromSeqNo,
            uint32_t MsgCount) = 0;

        /**
         * @brief Retransmission reject
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Retransmit+Reject
         *
         */
        virtual void
        retransmitReject(
            uint64_t UUID,
            uint64_t LastUUID,
            uint64_t RequestTimestamp,
            uint16_t ErrorCodes,
            const std::string &Reason) = 0;

        //
        // APPLICATION LAYER
        //

        /**
         * @brief businessReject
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Business+Reject
         *
         */
        virtual void businessReject(
            uint64_t UUID,
            uint32_t SeqNum,
            const std::string &Text,
            uint64_t SendingTime,
            uint16_t BusinessRejectRefID,
            uint32_t RefSeqNum,
            uint16_t TagId,
            uint16_t BusinessRejectReason,
            const std::string &RefMsgType,
            bool PossRetransFlag) = 0;

        struct exec_report_param_t
        {
            uint16_t templateId;
            uint64_t UUID;
            uint32_t SeqNum;
            std::string ExecID;
            std::string SenderID;
            std::string ClOrdID;
            uint64_t PartyDetailsListReqID;
            uint64_t OrderID;
            int64_t Price_mantissa;
            int8_t Price_exponent;
            int64_t StopPx_mantissa;
            int8_t StopPx_exponent;
            uint64_t lastPx_mantissa;
            int8_t lastPx_exponent;
            uint64_t TransactTime;
            uint64_t SendingTime;
            uint64_t OrderRequestID;
            std::string Location;
            int32_t SecurityID;
            uint32_t OrderQty;
            uint32_t DispQty;
            uint32_t LastQty;
            uint32_t LeavesQty;
            uint32_t CumQty;
            sbe::OrderType::Value OrdType;
            sbe::SideReq::Value Side;
            sbe::TimeInForce::Value TimeInForce;
            sbe::ManualOrdIndReq::Value ManualOrderIndicator;
            bool PossRetransFlag;
            std::string OrdStatus;
            // for trades
            uint64_t SideTradeID;
            std::string ExecType;
            std::string TradeID;
            // for spreads
            u_int64_t SecExecID;
            bool AggressorIndicator;
            // for addendum
            uint64_t OrigSideTradeID;
        };

        /**
         * @brief execution report for
         * New
         * Modify
         * Cancel
         * Status
         * Elimination
         * TradeOutright
         * TradeSpread
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/From+CME+Globex+to+Client+System
         */
        virtual void executionReport(
            const exec_report_param_t &param) = 0;

        struct canc_rej_param_t
        {
            uint16_t templateId;
            uint64_t UUID;
            uint32_t SeqNum;
            std::string ExecID;
            std::string SenderID;
            std::string ClOrdID;
            uint64_t PartyDetailsListReqID;
            uint64_t OrderID;
            uint64_t TransactTime;
            uint64_t SendingTime;
            uint64_t OrderRequestID;
            std::string Location;
            sbe::ManualOrdIndReq::Value ManualOrderIndicator;
            bool PossRetransFlag;
            std::string OrdStatus;
            uint32_t CxlRejReason;
            std::string CxlRejResponseTo;
        };

        /**
         * @brief cancel reject for
         * OrderCancelRejec
         * OrderCancelReplaceReject
         * @see @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/From+CME+Globex+to+Client+System
         */
        virtual void cancelReject(
            const canc_rej_param_t &param) = 0;

        /**
         * @brief partyDetailAck
         *
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Party+Details+Definition+Request+Acknowledgment
         */
        virtual void partyDetailAck(
            uint64_t UUID,
            uint32_t SeqNum,
            uint64_t PartyDetailsListReqID,
            uint64_t SendingTime,
            uint8_t PartyRequestStatus,
            bool PossRetransFlag,
            const std::vector<std::string> &partyDetailID,
            const std::vector<std::string> &partyDetailSource,
            const std::vector<sbe::PartyDetailRole::Value> &partyDetailRole) = 0;

        /**
         * @brief partyDetailReport
         * 
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Party+Details+List+Report
         */
        virtual void partyDetailReport(
            uint64_t UUID,
            uint32_t SeqNum,
            uint64_t PartyDetailsListReqID,
            uint64_t SendingTime,
            const std::vector<std::string> &partyDetailID,
            const std::vector<std::string> &partyDetailSource) = 0;

        /**
         * @brief terminate
         * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Terminate
         */
        virtual void terminate(
            uint64_t UUID,
            uint16_t ErrorCodes,
            const std::string &Reason) = 0;
        
    };
}
