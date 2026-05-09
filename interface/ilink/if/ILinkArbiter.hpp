#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"

cfsmp create_ILinkArbiter(
    en::x _exch,
    bool _doinit,
    cfsmp _handler_primary, 
    cfsmp _handler_secondary);

void subscribe_ILinkArbiter(
    cfsmp _arbiter, 
    cfsmp _sub);