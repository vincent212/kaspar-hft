/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{
    struct NotApplied : public actors::MessageT<NotApplied>
    {
        // variables
        uint64_t RequestTimeStamp;
        uint64_t UUID;
        uint32_t NextSeqNo;

        // constructor
        NotApplied(
            uint64_t _RequestTimeStamp,
            uint64_t _UUID,
            uint32_t _NextSeqNo)
            : RequestTimeStamp(_RequestTimeStamp),
              UUID(_UUID),
              NextSeqNo(_NextSeqNo) {}
    };
}