/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink
{
    namespace msg
    {
        struct EstablishmentAck : public  actors::Message_N<103>
        {
            // variables
            uint64_t RequestTimeStamp;
            uint64_t UUID;
            sbe::FTI::Value FTI;
            uint32_t PreviousSeqNo;
            uint64_t PreviousUUID;
            uint32_t NextSeqNo;
            uint16_t KeepAliveInterval;

            // constructor
            EstablishmentAck(
                uint64_t _RequestTimeStamp,
                uint64_t _UUID,
                sbe::FTI::Value _FTI,
                uint32_t _PreviousSeqNo,
                uint64_t _PreviousUUID,
                uint32_t _NextSeqNo,
                uint16_t _KeepAliveInterval)
                : RequestTimeStamp(_RequestTimeStamp),
                  UUID(_UUID),
                  FTI(_FTI),
                  PreviousSeqNo(_PreviousSeqNo),
                  PreviousUUID(_PreviousUUID),
                  NextSeqNo(_NextSeqNo),
                  KeepAliveInterval(_KeepAliveInterval) {}
        };
    }
}