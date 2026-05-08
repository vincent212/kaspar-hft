#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"

cfsmp create_ILinkRec(
    en::x _x,
    int _sock, void *_cbif
);