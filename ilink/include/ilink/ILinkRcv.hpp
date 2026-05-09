#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <assert.h>

#include "ilink_v8/NegotiationResponse501.h"
#include "ilink_v8/NegotiationReject502.h"
#include "ilink_v8/EstablishmentAck504.h"
#include "ilink_v8/EstablishmentReject505.h"

#include "ilink_v8/NotApplied513.h"
#include "ilink_v8/RetransmitRequest508.h"
#include "ilink_v8/Retransmission509.h"
#include "ilink_v8/RetransmitReject510.h"

#include "ilink_v8/BusinessReject521.h"
#include "ilink_v8/ExecutionReportNew522.h"
#include "ilink_v8/ExecutionReportCancel534.h"
#include "ilink_v8/ExecutionReportModify531.h"
#include "ilink_v8/ExecutionReportStatus532.h"
#include "ilink_v8/ExecutionReportTradeOutright525.h"
#include "ilink_v8/ExecutionReportTradeSpread526.h"
#include "ilink_v8/ExecutionReportTradeSpreadLeg527.h"
#include "ilink_v8/ExecutionReportElimination524.h"
#include "ilink_v8/ExecutionReportTradeAddendumOutright548.h"
#include "ilink_v8/ExecutionReportTradeAddendumSpread549.h"
#include "ilink_v8/OrderCancelReject535.h"
#include "ilink_v8/OrderCancelReplaceReject536.h"
#include "ilink_v8/ExecutionReportReject523.h"
#include "ilink_v8/Terminate507.h"
#include "ilink_v8/PartyDetailsDefinitionRequestAck519.h"
#include "ilink_v8/PartyDetailsListReport538.h"
#include "ilink_v8/Sequence506.h"

#include "logger/act/Logger.hpp"

#include "ILinkCBIF.hpp"

#include "sock_help.hpp"

namespace m2::ilink::receiver
{

    // get name for logger
    static const char* get_name() noexcept
    {
        return "ILinkRcv";
    }

    /**
     * @brief process message from msgw if available
     * call message handler
     *
     */
    static bool process_message_from_msgw(int sock, char *msg_buf, CBIF *cbif, bool block = true, bool debug = false) noexcept
    {
        auto header = sockhelp::recv_message(sock, msg_buf, block);
        if (!header)
        {
            return false;
        }

        if (debug)
        {
            std::cerr << "Received message: " << header->TemplateID << std::endl;
        }

        log_inf("Received message: %d", int(header->TemplateID));

        switch (header->TemplateID)
        {

            //
            // SESSION LAYER
            //

        case sbe::Sequence506::sbeTemplateId():
        {
            sbe::Sequence506 sequence;
            auto msg = sequence.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            auto NextSeqNo = msg.nextSeqNo();
            auto FaultToleranceIndicator = msg.faultToleranceIndicator();
            auto KeepAliveIntervalLapsed = msg.keepAliveIntervalLapsed();
            cbif->sequence(NextSeqNo, FaultToleranceIndicator, KeepAliveIntervalLapsed);
            break;
        }

        case sbe::NegotiationResponse501::sbeTemplateId():
        {
            sbe::NegotiationResponse501 negotiateResponse;
            auto msg = negotiateResponse.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());
            
            auto RequestTimeStamp = msg.requestTimestamp();
            auto UUID = msg.uUID();
            auto FaultToleranceIndicator = msg.faultToleranceIndicator();
            auto PreviousSeqNo = msg.previousSeqNo();
            auto PreviousUUID = msg.previousUUID();
            cbif->negotiationResponse(RequestTimeStamp, UUID, FaultToleranceIndicator, PreviousSeqNo, PreviousUUID);
            break;
        }

        case sbe::NegotiationReject502::sbeTemplateId():
        {
            sbe::NegotiationReject502 negotiateReject;
            auto msg = negotiateReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_err("msg: %s", ss.str());

            auto Reason = msg.getReasonAsString();
            auto RequestTimeStamp = msg.requestTimestamp();
            auto UUID = msg.uUID();
            auto errorCodes = msg.errorCodes();
            auto FaultToleranceIndicator = msg.faultToleranceIndicator();
            cbif->negotiationReject(RequestTimeStamp, UUID, FaultToleranceIndicator, errorCodes, Reason);
            break;
        }

        case sbe::EstablishmentAck504::sbeTemplateId():
        {
            sbe::EstablishmentAck504 establishmentAck;
            auto msg = establishmentAck.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            auto RequestTimeStamp = msg.requestTimestamp();
            auto UUID = msg.uUID();
            auto NextSeqNo = msg.nextSeqNo();
            auto KeepAliveInterval = msg.keepAliveInterval();
            auto FaultToleranceIndicator = msg.faultToleranceIndicator();
            auto PreviousSeqNo = msg.previousSeqNo();
            auto PreviousUUID = msg.previousUUID();
            cbif->establishementAck(RequestTimeStamp, UUID, FaultToleranceIndicator, PreviousSeqNo, PreviousUUID, NextSeqNo, KeepAliveInterval);
            break;
        }

        case sbe::EstablishmentReject505::sbeTemplateId():
        {
            sbe::EstablishmentReject505 establishmentReject;
            auto msg = establishmentReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_err("msg: %s", ss.str());

            auto Reason = msg.getReasonAsString();
            auto UUID = msg.uUID();
            auto RequestTimeStamp = msg.requestTimestamp();
            auto NextSeqNo = msg.nextSeqNo();
            auto errorCodes = msg.errorCodes();
            auto FaultToleranceIndicator = msg.faultToleranceIndicator();
            cbif->establishmentReject(
                RequestTimeStamp,
                UUID,
                FaultToleranceIndicator,
                NextSeqNo,
                errorCodes,
                Reason);
            break;
        }

        case sbe::NotApplied513::sbeTemplateId():
        {
            sbe::NotApplied513 notApplied;
            auto msg = notApplied.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            auto UUID = msg.uUID();
            auto FromSeqNo = msg.fromSeqNo();
            auto MsgCount = msg.msgCount();
            cbif->notApplied(UUID, FromSeqNo, MsgCount);
            break;
        }

        case sbe::Retransmission509::sbeTemplateId():
        {
            sbe::Retransmission509 retransmission;
            auto msg = retransmission.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            auto UUID = msg.uUID();
            auto LastUUID = msg.lastUUID();
            auto RequestTimestamp = msg.requestTimestamp();
            auto FromSeqNo = msg.fromSeqNo();
            auto MsgCount = msg.msgCount();
            cbif->retransmission(UUID, LastUUID, RequestTimestamp, FromSeqNo, MsgCount);
            break;
        }

        case sbe::RetransmitReject510::sbeTemplateId():
        {
            sbe::RetransmitReject510 retransmitReject;
            auto msg = retransmitReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_err("msg: %s", ss.str());

            auto Reason = msg.getReasonAsString();
            auto UUID = msg.uUID();
            auto LastUUID = msg.lastUUID();
            auto RequestTimestamp = msg.requestTimestamp();
            auto ErrorCodes = msg.errorCodes();
            cbif->retransmitReject(UUID, LastUUID, RequestTimestamp, ErrorCodes, Reason);
            break;
        }

            //
            // APPLICATION LAYER
            //

        case sbe::BusinessReject521::sbeTemplateId():
        {
            sbe::BusinessReject521 businessReject;
            auto msg = businessReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_err("msg: %s", ss.str());

            auto UUID = msg.uUID();
            auto SeqNum = msg.seqNum();
            std::string Text = msg.getTextAsString();
            auto SendingTime = msg.sendingTimeEpoch();
            auto BusinessRejectRefID = msg.businessRejectRefID();
            auto RefSeqNum = msg.refSeqNum();
            auto TagId = msg.refTagID();
            auto BusinessRejectReason = msg.businessRejectReason();
            auto RefMsgType = msg.getRefMsgTypeAsString();
            auto PossRetransFlag = msg.possRetransFlag();
            cbif->businessReject(UUID, SeqNum, Text, SendingTime, BusinessRejectRefID, RefSeqNum, TagId, BusinessRejectReason, RefMsgType, PossRetransFlag);
            break;
        }

        case sbe::ExecutionReportNew522::sbeTemplateId():
        {
            sbe::ExecutionReportNew522 executionReportNew;
            CBIF::exec_report_param_t param;
            auto msg = executionReportNew.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }


            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportNew522::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.DispQty = msg.displayQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            param.ExecType = msg.getExecTypeAsString();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportModify531::sbeTemplateId():
        {
            sbe::ExecutionReportModify531 executionReportModify;
            CBIF::exec_report_param_t param;
            auto msg = executionReportModify.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportModify531::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.DispQty = msg.displayQty();
            param.CumQty = msg.cumQty();
            param.LeavesQty = msg.leavesQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            param.ExecType = msg.getExecTypeAsString();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportCancel534::sbeTemplateId():
        {
            sbe::ExecutionReportCancel534 executionReportCancel;
            CBIF::exec_report_param_t param;
            auto msg = executionReportCancel.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportCancel534::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.DispQty = msg.displayQty();
            param.CumQty = msg.cumQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            param.ExecType = msg.getExecTypeAsString();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportStatus532::sbeTemplateId():
        {
            sbe::ExecutionReportStatus532 executionReportStatus;
            CBIF::exec_report_param_t param;
            auto msg = executionReportStatus.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportStatus532::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.DispQty = msg.displayQty();
            param.CumQty = msg.cumQty();
            param.LeavesQty = msg.leavesQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.ExecType = msg.getExecTypeAsString();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportTradeOutright525::sbeTemplateId():
        {
            sbe::ExecutionReportTradeOutright525 executionReportTradeOutright;
            CBIF::exec_report_param_t param;
            auto msg = executionReportTradeOutright.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportTradeOutright525::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.LastQty = msg.lastQty();
            param.CumQty = msg.cumQty();
            param.LeavesQty = msg.leavesQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.ExecType = msg.getExecTypeAsString();
            param.lastPx_mantissa = msg.lastPx().mantissa();
            param.lastPx_exponent = msg.lastPx().exponent();
            param.SideTradeID = msg.sideTradeID();
            cbif->executionReport(param);

            break;
        }

        case sbe::ExecutionReportTradeSpread526::sbeTemplateId():
        {
            sbe::ExecutionReportTradeSpread526 executionReportTradeSpread;
            CBIF::exec_report_param_t param;
            auto msg = executionReportTradeSpread.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportTradeSpread526::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.CumQty = msg.cumQty();
            param.LeavesQty = msg.leavesQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.ExecType = msg.getExecTypeAsString();
            param.lastPx_mantissa = msg.lastPx().mantissa();
            param.lastPx_exponent = msg.lastPx().exponent();
            param.SideTradeID = msg.sideTradeID();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportTradeSpreadLeg527::sbeTemplateId():
        {
            std::cerr << "ExecutionReportTradeSpreadLeg527 not implemented" << std::endl;
            assert(false);
        }

        case sbe::ExecutionReportElimination524::sbeTemplateId():
        {
            sbe::ExecutionReportElimination524 executionReportElimination;
            CBIF::exec_report_param_t param;
            auto msg = executionReportElimination.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportElimination524::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.DispQty = msg.displayQty();
            param.CumQty = msg.cumQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            param.ExecType = msg.getExecTypeAsString();
            param.lastPx_mantissa = 0;
            param.lastPx_exponent = 0;
            param.SideTradeID = 0;
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportReject523::sbeTemplateId():
        {
            sbe::ExecutionReportReject523 executionReportReject;
            CBIF::exec_report_param_t param;
            auto msg = executionReportReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_err("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportReject523::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.Price_mantissa = msg.price().mantissa();
            param.Price_exponent = msg.price().exponent();
            param.StopPx_mantissa = msg.stopPx().mantissa();
            param.StopPx_exponent = msg.stopPx().exponent();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrderQty = msg.orderQty();
            param.DispQty = msg.displayQty();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.TimeInForce = msg.timeInForce();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            param.ExecType = msg.getExecTypeAsString();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportTradeAddendumOutright548::sbeTemplateId():
        {
            sbe::ExecutionReportTradeAddendumOutright548 executionReportTradeAddendumOutright;
            CBIF::exec_report_param_t param;
            auto msg = executionReportTradeAddendumOutright.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportTradeAddendumOutright548::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = 0;
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.Side = msg.side();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.lastPx_mantissa = msg.lastPx().mantissa();
            param.lastPx_exponent = msg.lastPx().exponent();
            param.SideTradeID = msg.sideTradeID();
            param.OrigSideTradeID = msg.origSideTradeID();
            cbif->executionReport(param);
            break;
        }

        case sbe::ExecutionReportTradeAddendumSpread549::sbeTemplateId():
        {
            sbe::ExecutionReportTradeAddendumSpread549 executionReportTradeAddendumSpread;
            CBIF::exec_report_param_t param;
            auto msg = executionReportTradeAddendumSpread.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::ExecutionReportTradeAddendumSpread549::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.Location = msg.getLocationAsString();
            param.SecurityID = msg.securityID();
            param.OrdType = msg.ordType();
            param.Side = msg.side();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.lastPx_mantissa = msg.lastPx().mantissa();
            param.lastPx_exponent = msg.lastPx().exponent();
            param.SideTradeID = msg.sideTradeID();
            param.OrigSideTradeID = msg.origSideTradeID();
            cbif->executionReport(param);
            break;
        }

        case sbe::OrderCancelReject535::sbeTemplateId():
        {
            sbe::OrderCancelReject535 orderCancelReject;
            CBIF::canc_rej_param_t param;
            auto msg = orderCancelReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::OrderCancelReject535::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            param.CxlRejResponseTo = msg.getCxlRejResponseToAsString();
            param.CxlRejReason = msg.cxlRejReason();
            cbif->cancelReject(param);
            break;
        }

        case sbe::OrderCancelReplaceReject536::sbeTemplateId():
        {
            sbe::OrderCancelReplaceReject536 orderCancelReplaceReject;
            CBIF::canc_rej_param_t param;
            auto msg = orderCancelReplaceReject.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            param.templateId = sbe::OrderCancelReplaceReject536::sbeTemplateId();
            param.UUID = msg.uUID();
            param.SeqNum = msg.seqNum();
            param.ExecID = msg.getExecIDAsString();
            param.SenderID = msg.getSenderIDAsString();
            param.ClOrdID = msg.getClOrdIDAsString();
            param.PartyDetailsListReqID = msg.partyDetailsListReqID();
            param.OrderID = msg.orderID();
            param.TransactTime = msg.transactTime();
            param.SendingTime = msg.sendingTimeEpoch();
            param.OrderRequestID = msg.orderRequestID();
            param.Location = msg.getLocationAsString();
            param.ManualOrderIndicator = msg.manualOrderIndicator();
            param.PossRetransFlag = msg.possRetransFlag();
            param.OrdStatus = msg.getOrdStatusAsString();
            cbif->cancelReject(param);
            break;
        }

        case sbe::Terminate507::sbeTemplateId():
        {
            sbe::Terminate507 terminate;
            auto msg = terminate.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_err("msg: %s", ss.str());

            auto UUID = msg.uUID();
            auto err = msg.errorCodes();
            auto reason = msg.getReasonAsString();
            cbif->terminate(UUID, err, reason);
            break;
        }

        case sbe::PartyDetailsDefinitionRequestAck519::sbeTemplateId():
        {
            sbe::PartyDetailsDefinitionRequestAck519 partyDetailsDefinitionRequestAck;
            auto msg = partyDetailsDefinitionRequestAck.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            auto SeqNum = msg.seqNum();
            auto UUID = msg.uUID();
            auto PartyDetailsListReqID = msg.partyDetailsListReqID();
            auto SendingTime = msg.sendingTimeEpoch();
            auto PartyRequestStatus = msg.partyDetailRequestStatus();
            auto PossRetransFlag = msg.possRetransFlag();
            auto noPartyDetails = msg.noPartyDetails();
            std::vector<std::string> partyDetailID, partyDetailSource;
            std::vector<sbe::PartyDetailRole::Value> partyDetailRole;
            while (noPartyDetails.hasNext())
            {
                noPartyDetails.next();
                auto partyDetailID_ = noPartyDetails.getPartyDetailIDAsString();
                auto partyDetailSource_ = noPartyDetails.getPartyDetailIDSourceAsString();
                auto partyDetailRole_ = noPartyDetails.partyDetailRole();
                partyDetailID.push_back(partyDetailID_);
                partyDetailSource.push_back(partyDetailSource_);
                partyDetailRole.push_back(partyDetailRole_);
            }
            cbif->partyDetailAck(SeqNum, UUID, PartyDetailsListReqID, SendingTime, PartyRequestStatus, PossRetransFlag, partyDetailID, partyDetailSource, partyDetailRole);
            break;
        }

        case sbe::PartyDetailsListReport538::sbeTemplateId():
        {
            sbe::PartyDetailsListReport538 partyDetailsListReport;
            auto msg = partyDetailsListReport.wrapForDecode(msg_buf, 0, header->BlockLength, header->Version, header->MsgSize);
            if (debug)
            {
                std::cerr << "msg: " << msg << std::endl;
            }

            std::stringstream ss;
            ss << msg;
            log_inf("msg: %s", ss.str());

            auto SeqNum = msg.seqNum();
            auto UUID = msg.uUID();
            auto PartyDetailsListReqID = msg.partyDetailsListReqID();
            auto SendingTime = msg.sendingTimeEpoch();
            auto noPartyDetails = msg.noPartyDetails();
            std::vector<std::string> partyDetailID, partyDetailSource;
            while (noPartyDetails.hasNext())
            {
                noPartyDetails.next();
                auto partyDetailID_ = noPartyDetails.getPartyDetailIDAsString();
                auto partyDetailSource_ = noPartyDetails.getPartyDetailIDSourceAsString();
                partyDetailID.push_back(partyDetailID_);
                partyDetailSource.push_back(partyDetailSource_);
            }
            cbif->partyDetailReport(SeqNum, UUID, PartyDetailsListReqID, SendingTime, partyDetailID, partyDetailSource);
            break;
        }

        default:
        {
            std::cerr << "*** Unknown template id: " << header->TemplateID << std::endl;
            log_err("Unknown template id: ", header->TemplateID);
        }

        }

        return true;
    }

}