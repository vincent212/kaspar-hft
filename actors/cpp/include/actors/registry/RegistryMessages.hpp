#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <optional>
#include <chrono>
#include "actors/Message.hpp"
#include "actors/ActorRef.hpp"

namespace actors::registry {

// Message IDs 300-307 reserved for registry protocol (must be < 512)
constexpr int MSG_REGISTER_ACTOR = 300;
constexpr int MSG_UNREGISTER_ACTOR = 301;
constexpr int MSG_REGISTRATION_OK = 302;
constexpr int MSG_REGISTRATION_FAILED = 303;
constexpr int MSG_LOOKUP_ACTOR = 304;
constexpr int MSG_LOOKUP_RESULT = 305;
constexpr int MSG_HEARTBEAT = 306;
constexpr int MSG_HEARTBEAT_ACK = 307;

/**
 * RegisterActor - Manager registers an actor with GlobalRegistry
 *
 * Sent during Manager::manage() to register actor name -> ActorRef mapping.
 * GlobalRegistry replies with RegistrationOk or RegistrationFailed.
 */
struct RegisterActor : public Message_N<MSG_REGISTER_ACTOR> {
  std::string manager_id;
  std::string actor_name;
  ActorRef actor_ref;

  RegisterActor() = default;
  RegisterActor(std::string mgr, std::string name, ActorRef ref)
    : manager_id(std::move(mgr))
    , actor_name(std::move(name))
    , actor_ref(std::move(ref)) {}
};

/**
 * UnregisterActor - Remove an actor from the registry
 *
 * Sent when an actor is stopped or Manager shuts down.
 */
struct UnregisterActor : public Message_N<MSG_UNREGISTER_ACTOR> {
  std::string actor_name;

  UnregisterActor() = default;
  explicit UnregisterActor(std::string name)
    : actor_name(std::move(name)) {}
};

/**
 * RegistrationOk - Confirms successful actor registration
 */
struct RegistrationOk : public Message_N<MSG_REGISTRATION_OK> {
  std::string actor_name;

  RegistrationOk() = default;
  explicit RegistrationOk(std::string name)
    : actor_name(std::move(name)) {}
};

/**
 * RegistrationFailed - Registration was rejected
 *
 * Common reasons: name already registered, invalid ActorRef
 */
struct RegistrationFailed : public Message_N<MSG_REGISTRATION_FAILED> {
  std::string actor_name;
  std::string reason;

  RegistrationFailed() = default;
  RegistrationFailed(std::string name, std::string why)
    : actor_name(std::move(name))
    , reason(std::move(why)) {}
};

/**
 * LookupActor - Request ActorRef for a named actor
 *
 * Manager sends this when local lookup fails.
 * GlobalRegistry replies with LookupResult via standard reply() mechanism.
 */
struct LookupActor : public Message_N<MSG_LOOKUP_ACTOR> {
  std::string actor_name;

  LookupActor() = default;
  explicit LookupActor(std::string name)
    : actor_name(std::move(name)) {}
};

/**
 * LookupResult - Response to LookupActor
 *
 * Contains the ActorRef if found, and online status.
 * If actor_ref is empty, the actor was not found.
 * If online is false, the actor's Manager has missed heartbeats.
 */
struct LookupResult : public Message_N<MSG_LOOKUP_RESULT> {
  std::string actor_name;
  std::optional<ActorRef> actor_ref;
  bool online;

  LookupResult() : online(false) {}
  LookupResult(std::string name, std::optional<ActorRef> ref, bool is_online)
    : actor_name(std::move(name))
    , actor_ref(std::move(ref))
    , online(is_online) {}
};

/**
 * Heartbeat - Manager health check
 *
 * Managers send this every 2 seconds.
 * GlobalRegistry marks Manager offline after 6 seconds without heartbeat.
 */
struct Heartbeat : public Message_N<MSG_HEARTBEAT> {
  std::string manager_id;
  uint64_t timestamp;  // milliseconds since epoch

  Heartbeat() : timestamp(0) {}
  explicit Heartbeat(std::string mgr)
    : manager_id(std::move(mgr))
    , timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()) {}
};

/**
 * HeartbeatAck - Acknowledgement of heartbeat
 */
struct HeartbeatAck : public Message_N<MSG_HEARTBEAT_ACK> {
  HeartbeatAck() = default;
};

} // namespace actors::registry
