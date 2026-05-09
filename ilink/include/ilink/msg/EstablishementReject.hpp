/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{
    struct EstablishmentReject : public  actors::Message_N<102>
    {
        // variables
        uint64_t RequestTimeStamp;
        uint64_t UUID;
        sbe::FTI::Value FTI;
        uint32_t NextSeqNo;
        uint16_t errorCodes;
        std::string Reason;

        // constructor
        EstablishmentReject(
            uint64_t _RequestTimeStamp,
            uint64_t _UUID,
            sbe::FTI::Value _FTI,
            uint32_t _NextSeqNo,
            uint16_t _errorCodes,
            const std::string &_Reason)
            : RequestTimeStamp(_RequestTimeStamp),
              UUID(_UUID),
              FTI(_FTI),
              NextSeqNo(_NextSeqNo),
              errorCodes(_errorCodes),
              Reason(_Reason) {}
    };
}