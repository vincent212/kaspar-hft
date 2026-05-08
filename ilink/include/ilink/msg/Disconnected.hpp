/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace ilink::msg
{
    struct Disconnected : public  actors::Message_N<116>
    {
        bool is_primary;
        Disconnected(bool _is_primary) : is_primary(_is_primary) {}
    };
}
