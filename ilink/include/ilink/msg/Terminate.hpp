#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{
    struct Terminate : public  actors::Message_N<112>
    {
        // variables
        uint64_t UUID;
        uint16_t ErrorCodes;
        std::string Reason;

        // constructor
        Terminate(
            uint64_t _UUID,
            uint16_t _ErrorCodes,
            const std::string &_Reason)
            : UUID(_UUID),
              ErrorCodes(_ErrorCodes),
              Reason(_Reason) {}
    };
}