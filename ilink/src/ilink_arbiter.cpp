/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "ilink/act/ILinkArbiter.hpp"

actor_ptr create_ILinkArbiter(
    en::x _exch,
    bool _doinit, 
    actor_ptr _handler_primary, 
    actor_ptr _handler_secondary)
{
    return new ilink::ILinkArbiter(
        _exch,
        _doinit,
        _handler_primary, 
        _handler_secondary);
}

void subscribe_ILinkArbiter(
    actor_ptr _arbiter, actor_ptr _sub)
{
    ((ilink::ILinkArbiter *)_arbiter)->subscribe(_sub);
}