#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"

namespace actors {
namespace msg {

/**
 * ShutdownThisActor - Request to shut down a single actor (not whole group)
 *
 * Unlike Shutdown (which shuts down entire group when sent from outside),
 * ShutdownThisActor is meant to shut down only the destination actor.
 *
 * Typical usage: An actor sends this to itself when it completes its work
 * (e.g., PCAPReader at EOF). The Group will:
 * - Forward the message to the actor
 * - Remove the actor from the group
 * - Send ActorRemoved notification to the requester
 * - Keep other group members running
 */
struct ShutdownThisActor : public Message_N<15> {
    ShutdownThisActor() = default;
};

}}
