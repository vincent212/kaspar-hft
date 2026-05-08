/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{
    struct RetransmissionReject : public  actors::Message_N<111>
    {
        // variables
        uint64_t UUID;
        uint64_t LastUUID;
        uint64_t RequestTimeStamp;
        uint16_t errorCodes;
        std::string Reason;

        // constructor
        RetransmissionReject(
            uint64_t _UUID,
            uint64_t _LastUUID,
            uint64_t _RequestTimeStamp,
            uint16_t _errorCodes,
            const std::string &_Reason)
            : UUID(_UUID),
              LastUUID(_LastUUID),
              RequestTimeStamp(_RequestTimeStamp),

              errorCodes(_errorCodes),
              Reason(_Reason)
        {
        }
    };
}