/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{

    struct BusinessReject : public  actors::Message_N<100>
    {
        // variables
        uint64_t UUID;
        uint32_t SeqNum;
        const std::string &Text;
        uint64_t SendingTime;
        uint16_t BusinessRejectRefID;
        uint32_t RefSeqNum;
        uint16_t TagId;
        uint16_t BusinessRejectReason;
        const std::string RefMsgType;
        bool PossRetransFlag;

        // constructor
        BusinessReject(
            uint64_t _UUID,
            uint32_t _SeqNum,
            const std::string &_Text,
            uint64_t _SendingTime,
            uint16_t _BusinessRejectRefID,
            uint32_t _RefSeqNum,
            uint16_t _TagId,
            uint16_t _BusinessRejectReason,
            const std::string &_RefMsgType,
            bool _PossRetransFlag)
            : UUID(_UUID),
              SeqNum(_SeqNum),
              Text(_Text),
              SendingTime(_SendingTime),
              BusinessRejectRefID(_BusinessRejectRefID),
              RefSeqNum(_RefSeqNum),
              TagId(_TagId),
              BusinessRejectReason(_BusinessRejectReason),
              RefMsgType(_RefMsgType),
              PossRetransFlag(_PossRetransFlag) {}
    };

}