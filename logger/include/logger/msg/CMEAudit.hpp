#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <vector>
#include <any>

namespace polonaise::logger::msg
{
    struct CMEAudit : public  actors::Message_N<121>
    {
        std::vector<std::any> val_arr;
        CMEAudit(std::vector<std::any> _val_arr) : val_arr(_val_arr) {}
    };
}