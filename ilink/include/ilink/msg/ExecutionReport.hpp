/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "ilink/ILinkCBIF.hpp"

namespace ilink::msg
{

    struct ExecutionReport : public actors::MessageT<ExecutionReport>
    {
        // variables
        m2::ilink::CBIF::exec_report_param_t exec_report_param;
        // constructor
        ExecutionReport(
            m2::ilink::CBIF::exec_report_param_t _exec_report_param)
            : exec_report_param(_exec_report_param) {}
    };

}