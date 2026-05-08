/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/ActorRef.hpp"
#include <stdexcept>

namespace actors {

void RustActorRef::send(const Message* m, Actor*) {
    delete m;
    throw std::runtime_error("RustActorRef::send() not available - link with interop library for C++/Rust communication");
}

} // namespace actors
