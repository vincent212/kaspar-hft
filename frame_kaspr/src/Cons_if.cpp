/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KASPR: Cons factory function

#include "frame/cons/act/Cons.hpp"
#include "interface/cons/if/Cons.hpp"

actor_ptr create_CONS(
    actors::Manager *mgr,
    const std::string &name)
{
    return new frame::cons::act::Cons(mgr, name);
}
