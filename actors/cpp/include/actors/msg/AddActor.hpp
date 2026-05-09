#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/Actor.hpp"

namespace actors {
namespace msg {

/**
 * AddActor - Request to dynamically add an actor to a Group
 *
 * Sent to a Group to request that an actor be added and started.
 * The Group will:
 * - Add the actor to its members
 * - Initialize and start the actor
 * - Track the sender for later ActorRemoved notification
 */
struct AddActor : public Message_N<11> {  // Use unique message ID
    actor_ptr actor;  // The actor to add to the group

    AddActor(actor_ptr a) : actor(a) {}
};

}}
