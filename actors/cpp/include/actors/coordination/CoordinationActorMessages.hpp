/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Actor message wrappers for coordination protocol.
 *
 * These wrappers allow coordination protocol structs (PermissionToken, PermissionGrant, etc.)
 * to be sent as Actor messages via ZmqSender/ZmqReceiver instead of using DEALER-ROUTER sockets.
 */

#pragma once

#include "actors/Message.hpp"
#include "actors/coordination/messages.hpp"
#include <string>

namespace actors::coordination {

/**
 * RegisterGroup message wrapper (ID 1010)
 * Sent by Group to CoordinatorActor to register the group
 */
struct RegisterGroupMessage : public Message_N<500> {
    RegisterGroup reg;

    RegisterGroupMessage() = default;
    explicit RegisterGroupMessage(RegisterGroup r) : reg(std::move(r)) {}
};

/**
 * RegisterActor message wrapper (ID 1011)
 * Sent by Group to CoordinatorActor to register an actor within a group
 */
struct RegisterActorMessage : public Message_N<501> {
    RegisterActor reg;

    RegisterActorMessage() = default;
    explicit RegisterActorMessage(RegisterActor r) : reg(std::move(r)) {}
};

/**
 * RegisterAck message wrapper (ID 1012)
 * Sent by CoordinatorActor to Group to acknowledge successful registration
 */
struct RegisterAckMessage : public Message_N<502> {
    RegisterAck ack;

    RegisterAckMessage() = default;
    explicit RegisterAckMessage(RegisterAck a) : ack(std::move(a)) {}
};

/**
 * RegisterNack message wrapper (ID 1013)
 * Sent by CoordinatorActor to Group to indicate registration failure
 */
struct RegisterNackMessage : public Message_N<503> {
    RegisterNack nack;

    RegisterNackMessage() = default;
    explicit RegisterNackMessage(RegisterNack n) : nack(std::move(n)) {}
};

/**
 * PermissionToken message wrapper (ID 1014)
 * Sent by CoordinatorActor to Group to forward a permission token
 */
struct PermissionTokenMessage : public Message_N<504> {
    PermissionToken token;

    PermissionTokenMessage() = default;
    explicit PermissionTokenMessage(PermissionToken t) : token(std::move(t)) {}
};

/**
 * PermissionRequest message wrapper (ID 1015)
 * Sent by Group to CoordinatorActor to request permission to send
 */
struct PermissionRequestMessage : public Message_N<505> {
    PermissionRequest request;

    PermissionRequestMessage() = default;
    explicit PermissionRequestMessage(PermissionRequest r) : request(std::move(r)) {}
};

/**
 * PermissionGrant message wrapper (ID 1016)
 * Sent by CoordinatorActor to Group to grant permission to send
 */
struct PermissionGrantMessage : public Message_N<506> {
    PermissionGrant grant;

    PermissionGrantMessage() = default;
    explicit PermissionGrantMessage(PermissionGrant g) : grant(std::move(g)) {}
};

/**
 * PermissionWait message wrapper (ID 1017)
 * Sent by CoordinatorActor to Group to indicate waiting in queue
 */
struct PermissionWaitMessage : public Message_N<507> {
    PermissionWait wait;

    PermissionWaitMessage() = default;
    explicit PermissionWaitMessage(PermissionWait w) : wait(std::move(w)) {}
};

/**
 * PermissionDone message wrapper (ID 1018)
 * Sent by Group to CoordinatorActor to signal completion of send
 */
struct PermissionDoneMessage : public Message_N<508> {
    PermissionDone done;

    PermissionDoneMessage() = default;
    explicit PermissionDoneMessage(PermissionDone d) : done(std::move(d)) {}
};

/**
 * DebugFlush message wrapper (ID 1019)
 * Debug command to flush coordinator token queue and pending requests
 */
struct DebugFlushMessage : public Message_N<509> {
    DebugFlushMessage() = default;
};

/**
 * DebugContinue message wrapper (ID 1020)
 * Debug command to resume coordination (exit debug pause)
 */
struct DebugContinueMessage : public Message_N<510> {
    DebugContinueMessage() = default;
};

/**
 * DebugStop message wrapper (ID 1021)
 * Debug command to pause coordination
 */
struct DebugStopMessage : public Message_N<511> {
    std::string reason;

    DebugStopMessage() = default;
    explicit DebugStopMessage(std::string r) : reason(std::move(r)) {}
};

} // namespace actors::coordination
