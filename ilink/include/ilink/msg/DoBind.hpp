/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{
    struct DoBind : public  actors::Message_N<118>
    {
        uint64_t uuid;
        uint32_t nextseq_from_us; 
        uint32_t lastseq_from_cme;     
        // when param of 0 for uuid is used uuid/seq will not be reset
        DoBind(uint64_t _uuid = 0, uint32_t _nextseq_from_us = 0, uint32_t _lastseq_from_cme = 0)
            : uuid(_uuid), nextseq_from_us(_nextseq_from_us), lastseq_from_cme(_lastseq_from_cme)
        {
        }
    };
}

