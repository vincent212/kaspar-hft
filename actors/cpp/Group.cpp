/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <list>
#include <string>
#include <typeinfo>
#include <exception>
#include <memory>
#include <iostream>
#include <cassert>
#include <stdexcept>

#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/ShutdownThisActor.hpp"
#include "actors/msg/AddActor.hpp"
#include "actors/msg/ActorRemoved.hpp"
#include "actors/act/Group.hpp"
#include "actors/coordination/messages.hpp"
#include "actors/coordination/CoordinationActorMessages.hpp"
#include "actors/remote/ZmqSender.hpp"
#include "actors/registry/RegistryClient.hpp"

using namespace actors;

Group::Group(const std::string& group_name)
  : Actor()
{
  strncpy(name, group_name.c_str(), sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
}

Group::~Group()
{
  disconnect_from_registry();
  disconnect_from_coordinator();
}

void Group::add(actor_ptr a)
{
  assert(a != nullptr && "adding null actor");
  assert(a->get_name() != nullptr && "actor name is null");
  assert(strlen(a->get_name()) > 0 && "actor name is empty");
  a->set_group(this);
  members.push_back(a);
  name_to_actor[a->get_name()] = a;

  // Register message handlers (done once per message type)
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(actors::msg::ShutdownThisActor, shutdown_this_actor_handler);
  MESSAGE_HANDLER(actors::msg::AddActor, add_actor_handler);

  // Register coordination message handlers
  MESSAGE_HANDLER(coordination::RegisterAckMessage, on_register_ack);
  MESSAGE_HANDLER(coordination::RegisterNackMessage, on_register_nack);
  MESSAGE_HANDLER(coordination::PermissionGrantMessage, on_permission_grant);
  MESSAGE_HANDLER(coordination::PermissionWaitMessage, on_permission_wait);
}

void Group::add_and_start(actor_ptr a)
{
  // First add the actor to the group
  add(a);

  // Then initialize and start it (same as start_handler does for initial members)
  a->init();
  a->fast_send(new msg::Start(), this);

  std::cout << get_name() << " Dynamically added and started actor " << a->get_name() << std::endl;
}

void Group::add_actor_handler(const msg::AddActor* m)
{
  // Track who requested this addition (for ActorRemoved notification)
  actor_requesters_[m->actor->get_name()] = m->sender;

  // Add and start the actor
  add_and_start(m->actor);

  std::cout << get_name() << " Added actor " << m->actor->get_name()
            << " (requested by " << (m->sender ? m->sender->get_name() : "unknown")
            << ")" << std::endl;
}

void Group::set_mode(GroupMode mode, const std::string& coord_address)
{
  if (registered_) {
    throw std::runtime_error("Cannot change mode after registration");
  }
  mode_ = mode;
  coord_address_ = coord_address;
}

std::vector<std::string> Group::get_member_names() const
{
  std::vector<std::string> names;
  for (const auto& a : members) {
    names.push_back(a->get_name());
  }
  return names;
}

void Group::start_handler(const actors::msg::Start *m)
{
  if (m->sender != this)
  {
    // Connect and register in coordinated mode
    if (mode_ == GroupMode::COORDINATED && !registered_) {
      connect_to_coordinator();
      register_with_coordinator();
      // NOTE: GlobalRegistry registration will happen in on_register_ack()
      // after coordinator confirms registration
    }
    // Connect to registry but DON'T register yet if in coordinated mode
    else if (!registry_address_.empty() && !registry_client_) {
      connect_to_registry();
      register_with_registry();
    }

    for (auto a : members)
    {
      assert(a != nullptr && "null actor in group");
#ifdef DEBUG
      std::cout << get_name() << " Group::start_handler sending start to "
                << a->get_name() << std::endl;
#endif
      a->init();
      a->fast_send(new msg::Start(), this);
    }
  }
  else
  {
    forward(m);
  }
}

void Group::shutdown_handler(const actors::msg::Shutdown *m)
{
  if (m->sender != this)
  {
    for (auto a : members)
    {
      a->fast_send(new msg::Shutdown(), this);
      a->end();
    }
  }
  else
  {
    forward(m);
  }
}

void Group::shutdown_this_actor_handler(const actors::msg::ShutdownThisActor *m)
{
  // ShutdownThisActor is always forwarded to the destination actor
  // The forward() function will handle actor removal
  forward(m);
}

void Group::end()
{
  disconnect_from_registry();
  disconnect_from_coordinator();
}

void Group::process_message(const Message *m)
{
  // In coordinated mode, queue message and request permission
  // DON'T forward until grant arrives
  if (mode_ == GroupMode::COORDINATED && registered_) {
    // Queue message on actor's queue (it waits there)
    // For now, just store the message - we'll forward when grant arrives
    // TODO: Use actor's actual queue instead of storing in Group

    std::string recipient_id = std::string(get_name()) + "." + m->destination->get_name();

    // Construct sender_id for TOKEN
    std::string sender_id;
    bool sender_in_group = false;
    bool sender_is_remote_proxy = false;

    if (m->sender) {
      sender_in_group = name_to_actor.find(m->sender->get_name()) != name_to_actor.end();
      sender_is_remote_proxy = (strncmp(m->sender->get_name(), "RemoteReplyProxy", 16) == 0);
    }

    if (m->sender && sender_in_group) {
      sender_id = std::string(get_name()) + "." + m->sender->get_name();
    } else if (m->sender && sender_is_remote_proxy) {
      sender_id = std::string(get_name())  + "." + m->sender->get_name() + ".remote_proxy";
    } else if (m->sender) {
      sender_id = std::string(get_name())  + "." + m->sender->get_name() + ".external";
    } else {
      sender_id = std::string(get_name())  + ".external";
    }

    // Store the pending message
    pending_message_ = m;

    // Send TOKEN first (announces the message)
    send_permission_token(sender_id, recipient_id);

    // Request permission (async - grant will arrive later)
    request_permission(recipient_id);

    // DON'T forward yet - return and wait for grant
    // The grant will trigger on_permission_grant() which will call forward()
    return;
  }

  // Non-coordinated mode - forward normally
  forward(m);
}

void Group::forward(const Message *m)
{
  assert(!m->is_fast && "cannot fast send here");

  // forward() is called either:
  // 1. Directly from process_message() in non-coordinated mode
  // 2. From on_permission_grant() in coordinated mode (after permission granted)

  // Store actor pointer before processing (in case we need to remove it)
  actor_ptr actor_to_remove = nullptr;
  bool is_shutdown_this_actor = (typeid(*m) == typeid(actors::msg::ShutdownThisActor));

  if (is_shutdown_this_actor) {
    actor_to_remove = m->destination;
  }

  // Just deliver the message - coordination handled elsewhere
  m->destination->reply_to = m->sender;
  m->destination->process_message_internal(m, true);

  // In coordinated mode, send DONE after processing
  if (mode_ == GroupMode::COORDINATED && registered_) {
    std::string recipient_id = std::string(get_name()) + "." + m->destination->get_name();
    send_permission_done(recipient_id, last_granted_sequence_);
  }

  // If this was a ShutdownThisActor message, remove actor from group
  if (is_shutdown_this_actor && actor_to_remove) {
    std::string removed_name = actor_to_remove->get_name();

    // Look up who requested this actor (if anyone)
    Actor* requester = nullptr;
    auto it = actor_requesters_.find(removed_name);
    if (it != actor_requesters_.end()) {
      requester = it->second;
      actor_requesters_.erase(it);
    }

    assert(requester && "No requester found for dynamically added actor shutdown") ;

    std::cout << get_name() << " Auto-removing actor " << removed_name
              << " after ShutdownThisActor" << std::endl;

    // Remove from members list
    members.remove(actor_to_remove);

    // Remove from name map
    name_to_actor.erase(removed_name);

    // Call end() to clean up
    actor_to_remove->end();

    // Delete the actor
    delete actor_to_remove;

    // Notify requester that actor was removed (if there was one)
    if (requester) {
      requester->send(new msg::ActorRemoved(removed_name), this);
      std::cout << get_name() << " Sent ActorRemoved(" << removed_name << ") to "
                << requester->get_name() << std::endl;
    }
    // else: No requester - actor was added statically, not via AddActor message
  }
}

// ============================================================================
// Coordination helpers
// ============================================================================

void Group::connect_to_coordinator()
{
  std::string coordinator_address;

  // Try to lookup coordinator from GlobalRegistry first
  if (registry_client_) {
    std::cout << get_name() << " Looking up coordinator from GlobalRegistry..." << std::endl;
    try {
      auto result = registry_client_->lookup_actor("coordinator.coordinator");
      if (result.found && !result.ambiguous) {
        coordinator_address = result.endpoint;
        std::cout << get_name() << " Found coordinator at: " << coordinator_address << std::endl;
      } else if (result.ambiguous) {
        std::cerr << get_name() << " WARNING: Coordinator lookup is ambiguous" << std::endl;
      } else {
        std::cerr << get_name() << " WARNING: Coordinator not found in registry" << std::endl;
      }
    } catch (const std::exception& e) {
      std::cerr << get_name() << " WARNING: Registry lookup failed: " << e.what() << std::endl;
    }
  }

  // Fall back to coord_address_ if registry lookup didn't work
  if (coordinator_address.empty()) {
    if (coord_address_.empty()) {
      throw std::runtime_error("Coordinator address not available (not in registry and coord_address not set)");
    }
    coordinator_address = coord_address_;
    std::cout << get_name() << " Using configured coordinator address: " << coordinator_address << std::endl;
  }

  // Create ZmqSender for sending coordination messages
  coord_sender_ = std::make_shared<ZmqSender>(coordinator_address);

  std::cout << get_name() << " Connected to coordinator at " << coordinator_address << std::endl;
}

void Group::register_with_coordinator()
{
  using namespace coordination;

  // 1. Register the group
  RegisterGroup reg_group(get_name());
  auto* reg_msg = new RegisterGroupMessage(reg_group);
  coord_sender_->send_to(coord_address_, "coordinator", reg_msg, this);

  // 2. Register each actor
  for (const auto& actor : members) {
    std::string actor_name = actor->get_name();
    if (actor_name.empty()) {
      std::cerr << get_name() << " ERROR: Actor with empty name in group!" << std::endl;
      std::cerr << "  Actor pointer: " << actor << std::endl;
      std::cerr << "  Total members: " << members.size() << std::endl;
      throw std::runtime_error("Cannot register actor with empty name");
    }
#ifdef DEBUG
    std::cerr << get_name() << " registering actor: " << actor_name << std::endl;
#endif
    RegisterActor reg_actor(get_name(), actor_name);
    auto* actor_msg = new RegisterActorMessage(reg_actor);
    coord_sender_->send_to(coord_address_, "coordinator", actor_msg, this);
  }

  // NOTE: registered_ will be set to true in on_register_ack()
  // after coordinator confirms registration
  std::cout << get_name() << " sent registration to coordinator ("
            << members.size() << " actors), waiting for ACK..." << std::endl;
}

void Group::disconnect_from_coordinator()
{
  if (coord_sender_) {
    coord_sender_.reset();
  }
  registered_ = false;
}

// ============================================================================
// Permission Protocol (Phase 2.2/2.3)
// ============================================================================

void Group::send_permission_token(const std::string& sender_id, const std::string& recipient_id)
{
  using namespace coordination;

  // Increment local timestamp for Lamport-style ordering
  local_timestamp_++;

  // Constructor is PermissionToken(recipient, sender, ts)
  PermissionToken token(recipient_id, sender_id, local_timestamp_);
  auto* token_msg = new PermissionTokenMessage(token);
  coord_sender_->send_to(coord_address_, "coordinator", token_msg, this);

  // Token is fire-and-forget - no response expected
}

void Group::request_permission(const std::string& actor_id)
{
  using namespace coordination;

  // Send permission request asynchronously
  // Reply will arrive via on_permission_grant() MESSAGE_HANDLER
  PermissionRequest req(actor_id, get_name());
  auto* req_msg = new PermissionRequestMessage(req);
  coord_sender_->send_to(coord_address_, "coordinator", req_msg, this);

  // Store pending actor ID for when grant arrives
  pending_actor_id_ = actor_id;

  // DO NOT WAIT - this is now async
  // The grant will arrive via MESSAGE_HANDLER and trigger message processing
}

void Group::send_permission_done(const std::string& actor_id, uint64_t sequence)
{
  using namespace coordination;

  PermissionDone done(actor_id, sequence);
  auto* done_msg = new PermissionDoneMessage(done);
  coord_sender_->send_to(coord_address_, "coordinator", done_msg, this);

  // Done is fire-and-forget - no response expected
}

// ============================================================================
// Registry Integration
// ============================================================================

void Group::set_registry(const std::string& registry_address, const std::string& my_endpoint)
{
  if (registry_client_) {
    throw std::runtime_error("Cannot change registry after connection");
  }
  registry_address_ = registry_address;
  my_endpoint_ = my_endpoint;
}

void Group::connect_to_registry()
{
  if (registry_address_.empty()) {
    return;  // No registry configured
  }

  registry_client_ = std::make_unique<registry::RegistryClient>(registry_address_);
  registry_client_->connect();

  std::cout << get_name() << " connected to GlobalRegistry at " << registry_address_ << std::endl;
}

void Group::register_with_registry()
{
  if (!registry_client_) {
    return;
  }

  // Collect actor names
  std::vector<std::string> actor_names;
  for (const auto& a : members) {
    actor_names.push_back(a->get_name());
  }

  // Register manager and actors
  bool success = registry_client_->register_manager(get_name(), my_endpoint_, actor_names);
  if (!success) {
    throw std::runtime_error(std::string(get_name()) + " failed to register with GlobalRegistry");
  }

  // Start heartbeat thread
  registry_client_->start_heartbeat_thread(get_name());

  std::cout << get_name() << " registered with GlobalRegistry ("
            << actor_names.size() << " actors)" << std::endl;
}

void Group::disconnect_from_registry()
{
  if (registry_client_) {
    registry_client_->stop_heartbeat_thread();
    if (!std::string(get_name()).empty()) {
      registry_client_->unregister_manager(get_name());
    }
    registry_client_->disconnect();
    registry_client_.reset();
  }
}

actor_ptr Group::lookup_actor(const std::string& name)
{
  // Check if it's a qualified name for this manager
  std::string qualified_prefix = std::string(get_name()) + "::";
  std::string local_name = name;

  if (name.find(qualified_prefix) == 0) {
    // It's qualified for us - extract the local name
    local_name = name.substr(qualified_prefix.length());
  } else if (name.find("::") != std::string::npos) {
    // It's qualified for a different manager - not local
    return nullptr;
  }

  // Check local actors first
  auto it = name_to_actor.find(local_name);
  if (it != name_to_actor.end()) {
    return it->second;
  }

  // Not found locally
  return nullptr;
}

std::string Group::lookup_actor_endpoint(const std::string& name)
{
  // Check local first
  actor_ptr local = lookup_actor(name);
  if (local) {
    // Local actor - return our endpoint
    return my_endpoint_;
  }

  // Query GlobalRegistry if configured
  if (!registry_client_) {
    return "";  // No registry configured
  }

  auto result = registry_client_->lookup_actor(name);
  if (result.found) {
    return result.endpoint;
  }

  if (result.ambiguous) {
    std::cerr << "Actor '" << name << "' is ambiguous (exists in multiple managers). "
              << "Use qualified name like 'manager::actor'" << std::endl;
  }

  return "";
}

// ============================================================================
// MESSAGE_HANDLER Implementations (Coordination Protocol)
// ============================================================================

void Group::on_permission_grant(const coordination::PermissionGrantMessage* msg)
{
  // Grant received - store sequence and forward the pending message
  last_granted_sequence_ = msg->grant.sequence;

#ifdef DEBUG
  std::cout << get_name() << " Permission granted for " << pending_actor_id_
            << " (seq=" << last_granted_sequence_ << ")" << std::endl;
#endif

  // Forward the pending message
  if (pending_message_) {
    forward(pending_message_);
    pending_message_ = nullptr;  // Clear after forwarding
  } else {
    std::cerr << get_name() << " WARNING: Permission grant received but no pending message!" << std::endl;
  }
}

void Group::on_register_ack(const coordination::RegisterAckMessage* msg)
{
  // Registration acknowledged
  std::cout << get_name() << " Registration ACK: " << msg->ack.id << std::endl;

  // Mark as registered
  registered_ = true;

  // Now register with GlobalRegistry if configured
  if (!registry_address_.empty() && !registry_client_) {
    connect_to_registry();
    register_with_registry();
  }
}

void Group::on_register_nack(const coordination::RegisterNackMessage* msg)
{
  // Registration failed
  std::cerr << get_name() << " Registration NACK: " << msg->nack.reason << std::endl;
}

void Group::on_permission_wait(const coordination::PermissionWaitMessage* msg)
{
  // Waiting for permission - no action needed, grant will follow
#ifdef DEBUG
  //std::cout << get_name() << " Permission WAIT (position=" << msg->wait.queue_position << ")" << std::endl;
#endif
}
