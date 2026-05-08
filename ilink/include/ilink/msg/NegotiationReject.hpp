/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "ilink_v8/FTI.h"

namespace ilink::msg
{
    struct NegotiationReject : public  actors::Message_N<105>
    {
        uint64_t RequestTimeStamp;
        uint64_t UUID;
        sbe::FTI::Value FTI;
        uint16_t errorCodes;
        std::string Reason;

        // constructor
        NegotiationReject(
            uint64_t _RequestTimeStamp,
            uint64_t _UUID,
            sbe::FTI::Value _FTI,
            uint16_t _errorCodes,
            const std::string &_Reason)
            : RequestTimeStamp(_RequestTimeStamp),
              UUID(_UUID),
              FTI(_FTI),
              errorCodes(_errorCodes),
              Reason(_Reason) {}
    };
}