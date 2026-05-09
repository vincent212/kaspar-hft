#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "ilink_v8/PartyDetailRole.h"

namespace ilink::msg
{
    struct PartyDetailsAck : public  actors::Message_N<108>
    {
        // variables
        uint64_t UUID;
        uint32_t SeqNum;
        uint64_t PartyDetailsListReqID;
        uint64_t SendingTime;
        uint8_t PartyRequestStatus;
        bool PossRetransFlag;
        const std::vector<std::string> partyDetailID;
        const std::vector<std::string> partyDetailSource;
        const std::vector<sbe::PartyDetailRole::Value> partyDetailRole;
        // constructor
        PartyDetailsAck(
            uint64_t _UUID,
            uint32_t _SeqNum,
            uint64_t _PartyDetailsListReqID,
            uint64_t _SendingTime,
            uint8_t _PartyRequestStatus,
            bool _PossRetransFlag,
            const std::vector<std::string> &_partyDetailID,
            const std::vector<std::string> &_partyDetailSource,
            const std::vector<sbe::PartyDetailRole::Value> &_partyDetailRole)
            : UUID(_UUID),
              SeqNum(_SeqNum),
              PartyDetailsListReqID(_PartyDetailsListReqID),
              SendingTime(_SendingTime),
              PartyRequestStatus(_PartyRequestStatus),
              PossRetransFlag(_PossRetransFlag),
              partyDetailID(_partyDetailID),
              partyDetailSource(_partyDetailSource),
              partyDetailRole(_partyDetailRole) {}
    };

}