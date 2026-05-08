#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "ilink/ILinkCBIF.hpp"
#include <iostream>

#include "ilink/msg/NegotiationReseponse.hpp"
#include "ilink/msg/NegotiationReject.hpp"
#include "ilink/msg/EstablishmentAck.hpp"
#include "ilink/msg/EstablishementReject.hpp"
#include "ilink/msg/NotApplied.hpp"
#include "ilink/msg/Retransmission.hpp"
#include "ilink/msg/RetransmissionReject.hpp"
#include "ilink/msg/BusinessReject.hpp"
#include "ilink/msg/ExecutionReport.hpp"
#include "ilink/msg/CancelReject.hpp"
#include "ilink/msg/PartyDetailsAck.hpp"
#include "ilink/msg/Terminate.hpp"
#include "ilink/msg/Sequence.hpp"

#include "actors/Actor.hpp"

namespace ilink
{

    struct IlinkCBImpl : public m2::ilink::CBIF
    {

        actors::Actor *ilink;

        // constructor
        IlinkCBImpl(actors::Actor *_ilink) : ilink(_ilink) {}

        void sequence(
            uint32_t NextSeqNo,
            sbe::FTI::Value FaultToleranceIndicator,
            sbe::KeepAliveLapsed::Value KeepAliveLapsed) override
        {
            ilink::msg::Sequence seq_msg(
                NextSeqNo,
                FaultToleranceIndicator,
                KeepAliveLapsed);
            ilink->fast_send(&seq_msg, nullptr);
        }

        void negotiationResponse(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint32_t PreviousSeqNo,
            uint64_t PreviousUUID) override
        {
            ilink::msg::NegotiationResponse neg_resp_msg(
                RequestTimeStamp,
                UUID,
                FTI,
                PreviousSeqNo,
                PreviousUUID);
            ilink->fast_send(&neg_resp_msg, nullptr);
        }

        void negotiationReject(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint16_t errorCodes,
            const std::string &Reason) override
        {
            ilink::msg::NegotiationReject neg_rej_msg(
                RequestTimeStamp,
                UUID,
                FTI,
                errorCodes,
                Reason);
            ilink->fast_send(&neg_rej_msg, nullptr);
        }

        void establishementAck(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint32_t PreviousSeqNo,
            uint64_t PreviousUUID,
            uint32_t NextSeqNo,
            uint16_t KeepAliveInterval) override
        {
            ilink::msg::EstablishmentAck est_ack_msg(
                RequestTimeStamp,
                UUID,
                FTI,
                PreviousSeqNo,
                PreviousUUID,
                NextSeqNo,
                KeepAliveInterval);
            ilink->fast_send(&est_ack_msg, nullptr);
        }

        void establishmentReject(
            uint64_t RequestTimeStamp,
            uint64_t UUID,
            sbe::FTI::Value FTI,
            uint32_t NextSeqNo,
            uint16_t errorCodes,
            const std::string &Reason) override
        {
            ilink::msg::EstablishmentReject est_rej_msg(
                RequestTimeStamp,
                UUID,
                FTI,
                NextSeqNo,
                errorCodes,
                Reason);
            ilink->fast_send(&est_rej_msg, nullptr);
        }

        void notApplied(
            uint64_t UUID,
            uint32_t FromSeqNo,
            uint32_t MsgCount) override
        {
            ilink::msg::NotApplied not_applied_msg(
                UUID,
                FromSeqNo,
                MsgCount);
            ilink->fast_send(&not_applied_msg, nullptr);
        }

        void retransmission(
            uint64_t UUID,
            uint64_t RequestTimestamp,
            uint64_t LastUUID,
            uint32_t FromSeqNo,
            uint32_t MsgCount) override
        {
            ilink::msg::Retransmission retrans_msg(
                UUID,
                RequestTimestamp,
                LastUUID,
                FromSeqNo,
                MsgCount);
            ilink->fast_send(&retrans_msg, nullptr);
        }

        void
        retransmitReject(
            uint64_t UUID,
            uint64_t LastUUID,
            uint64_t RequestTimestamp,
            uint16_t ErrorCodes,
            const std::string &Reason) override
        {
            ilink::msg::RetransmissionReject retrans_rej_msg(
                UUID,
                LastUUID,
                RequestTimestamp,
                ErrorCodes,
                Reason);
            ilink->fast_send(&retrans_rej_msg, nullptr);
        }

        void businessReject(
            uint64_t UUID,
            uint32_t SeqNum,
            const std::string &Text,
            uint64_t fast_sendingTime,
            uint16_t BusinessRejectRefID,
            uint32_t RefSeqNum,
            uint16_t TagId,
            uint16_t BusinessRejectReason,
            const std::string &RefMsgType,
            bool PossRetransFlag) override
        {
            ilink::msg::BusinessReject biz_rej_msg(
                UUID,
                SeqNum,
                Text,
                fast_sendingTime,
                BusinessRejectRefID,
                RefSeqNum,
                TagId,
                BusinessRejectReason,
                RefMsgType,
                PossRetransFlag);
            ilink->fast_send(&biz_rej_msg, nullptr);
        }

        void executionReport(
            const exec_report_param_t &param) override
        {
            ilink::msg::ExecutionReport exec_rep_msg(param);
            ilink->fast_send(&exec_rep_msg, nullptr);
        }

        void cancelReject(
            const canc_rej_param_t &param) override
        {
            ilink::msg::CancelReject canc_rej_msg(param);
            ilink->fast_send(&canc_rej_msg, nullptr);
        }

        void partyDetailAck(
            uint64_t UUID,
            uint32_t SeqNum,
            uint64_t PartyDetailsListReqID,
            uint64_t SendingTime,
            uint8_t PartyRequestStatus,
            bool PossRetransFlag,
            const std::vector<std::string> &partyDetailID,
            const std::vector<std::string> &partyDetailSource,
            const std::vector<sbe::PartyDetailRole::Value> &partyDetailRole)
            override
        {
            ilink::msg::PartyDetailsAck party_ack_msg(
                UUID,
                SeqNum,
                PartyDetailsListReqID,
                SendingTime,
                PartyRequestStatus,
                PossRetransFlag,
                partyDetailID,
                partyDetailSource,
                partyDetailRole);
            ilink->fast_send(&party_ack_msg, nullptr);
        }

        void partyDetailReport(
            [[maybe_unused]] uint64_t UUID,
            [[maybe_unused]] uint32_t SeqNum,
            [[maybe_unused]] uint64_t PartyDetailsListReqID,
            [[maybe_unused]] uint64_t SendingTime,
            [[maybe_unused]] const std::vector<std::string> &partyDetailID,
            [[maybe_unused]] const std::vector<std::string> &partyDetailSource) override
        {
        }

        void terminate(
            uint64_t UUID,
            uint16_t ErrorCodes,
            const std::string &Reason) override
        {
            ilink::msg::Terminate term_msg(UUID, ErrorCodes, Reason);
            ilink->fast_send(&term_msg, nullptr);
        }
    };

}
