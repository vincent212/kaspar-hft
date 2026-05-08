/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "light/qcoord.hpp"
#include "light/if/PCoord.hpp"

light::PCoord* create_PCoord()
{
    return new light::PCoord();
}
