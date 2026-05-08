#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"

actor_ptr create_CONS(
    actors::Manager *_mgr,
    const std::string &_name
    );
