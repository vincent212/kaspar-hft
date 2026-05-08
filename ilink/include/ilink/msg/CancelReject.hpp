/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "ilink/ILinkCBIF.hpp"

namespace ilink::msg
{
    struct CancelReject : public  actors::Message_N<101>
    {
        // variables
        m2::ilink::CBIF::canc_rej_param_t cancel_reject_param;

        // constructor
        CancelReject(
            m2::ilink::CBIF::canc_rej_param_t _cancel_reject_param)
            : cancel_reject_param(_cancel_reject_param) {}
    };
}