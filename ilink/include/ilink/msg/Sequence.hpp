/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

#include "ilink_v8/FTI.h"
#include "ilink_v8/KeepAliveLapsed.h"

namespace ilink::msg
{
    struct Sequence : public  actors::Message_N<113>
    {
        // variables
        uint32_t NextSeqNo;
        sbe::FTI::Value FaultToleranceIndicator;
        sbe::KeepAliveLapsed::Value KeepAliveLapsed;

        // constructor
        Sequence(
            uint32_t _NextSeqNo,
            sbe::FTI::Value _FaultToleranceIndicator,
            sbe::KeepAliveLapsed::Value _KeepAliveLapsed)
            : NextSeqNo(_NextSeqNo),
              FaultToleranceIndicator(_FaultToleranceIndicator),
              KeepAliveLapsed(_KeepAliveLapsed) {}
    };
}
