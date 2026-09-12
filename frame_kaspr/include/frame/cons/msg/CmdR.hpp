#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <string>

namespace frame::cons::msg
{
    struct CmdR : public actors::MessageT<CmdR>
    {
        std::string res;
        CmdR(const std::string&_res):res(_res){}
    };
}
