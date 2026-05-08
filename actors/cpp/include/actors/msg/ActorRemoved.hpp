#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <string>

namespace actors {
namespace msg {

/**
 * ActorRemoved - Notification that an actor was removed from a Group
 *
 * Sent by a Group to the original requester (sender of AddActor) when
 * an actor is removed from the group (typically after receiving Shutdown).
 * This allows the requester to know when the actor has been cleaned up.
 */
struct ActorRemoved : public Message_N<12> {  // Use unique message ID
    std::string actor_name;  // Name of the actor that was removed

    ActorRemoved(const std::string& name) : actor_name(name) {}
};

}}
