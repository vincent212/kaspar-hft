#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <list>
#include <map>
#include <string>
#include <cstring>
#include <memory>
#include <vector>
#include <mutex>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/ShutdownThisActor.hpp"

// Forward declarations
namespace actors {
  class ZmqSender;

  namespace registry {
    class RegistryClient;
  }

  namespace coordination {
    struct RegisterAckMessage;
    struct RegisterNackMessage;
    struct PermissionGrantMessage;
    struct PermissionWaitMessage;
  }

  namespace msg {
    struct AddActor;
    struct ActorRemoved;
  }
}

namespace actors
{
  /**
   * Coordination mode for deterministic simulation.
   */
  enum class GroupMode {
    NON_COORDINATED,  // Default - no GroupManager, normal async operation
    COORDINATED       // Deterministic mode - coordinates with GroupManager
  };

  /**
   * Group - Run multiple actors in a single thread
   *
   * Use when you have lightweight actors that don't need
   * separate threads. All actors in a group share one thread
   * and process messages sequentially.
   *
   * In COORDINATED mode, the Group connects to a GroupManager
   * for deterministic execution across processes.
   *
   * Usage (non-coordinated):
   *   actors::Group grp("my_group");
   *   grp.add(new LightActor1());
   *   grp.add(new LightActor2());
   *   mgr.manage(&grp);  // All run in single thread
   *
   * Usage (coordinated):
   *   actors::Group grp("my_group");
   *   grp.set_mode(GroupMode::COORDINATED, "tcp://localhost:5555");
   *   grp.add(new LightActor1());
   *   mgr.manage(&grp);  // Coordinates with GroupManager
   */
  class Group : public Actor
  {
    friend class Manager;

    char name[256];
    std::list<actor_ptr> members;
    std::map<std::string, actor_ptr> name_to_actor;

    // Coordination mode
    GroupMode mode_ = GroupMode::NON_COORDINATED;
    std::string coord_address_;
    std::shared_ptr<ZmqSender> coord_sender_;  // ZmqSender for coordination messages
    bool registered_ = false;
    uint64_t local_timestamp_ = 0;
    uint64_t last_granted_sequence_ = 0;

    // Track pending permission request
    std::string pending_actor_id_;  // Actor waiting for permission
    const Message* pending_message_ = nullptr;  // Message waiting for permission

    // GlobalRegistry integration
    std::string registry_address_;
    std::string my_endpoint_;
    std::unique_ptr<registry::RegistryClient> registry_client_;

    // Track who requested each dynamically-added actor
    // Maps actor name → requesting actor (for ActorRemoved notification)
    std::map<std::string, Actor*> actor_requesters_;

  public:
    explicit Group(const std::string& group_name);

    virtual ~Group();

    bool is_group() const override { return true; }

    void add(actor_ptr actor);

    /**
     * Add and immediately start an actor (for dynamic actor lifecycle).
     * This is for adding actors AFTER the group has already started.
     * Calls init() and sends Start message to the actor.
     */
    void add_and_start(actor_ptr actor);

    /**
     * Set coordination mode.
     * Must be called BEFORE adding actors and starting the group.
     *
     * @param mode NON_COORDINATED or COORDINATED
     * @param coord_address Coordinator address (e.g., "tcp://localhost:5555")
     */
    void set_mode(GroupMode mode, const std::string& coord_address = "");

    /**
     * Get current coordination mode.
     */
    GroupMode get_mode() const { return mode_; }

    /**
     * Check if registered with GroupManager.
     */
    bool is_registered() const { return registered_; }

    /**
     * Get list of member actor names.
     */
    std::vector<std::string> get_member_names() const;

    /**
     * Configure GlobalRegistry for remote actor discovery.
     * Must be called BEFORE starting the group.
     *
     * @param registry_address GlobalRegistry address (e.g., "tcp://localhost:5556")
     * @param my_endpoint Our endpoint for incoming messages (e.g., "tcp://localhost:7001")
     */
    void set_registry(const std::string& registry_address, const std::string& my_endpoint);

    /**
     * Check if registry is configured.
     */
    bool has_registry() const { return !registry_address_.empty(); }

    /**
     * Look up an actor by name.
     * Checks local actors first, then queries GlobalRegistry if configured.
     *
     * @param name Actor name (unqualified "actor" or qualified "manager::actor")
     * @return Actor pointer if found locally, nullptr if not found or remote
     */
    actor_ptr lookup_actor(const std::string& name);

    /**
     * Look up an actor's endpoint.
     * Checks local actors first, then queries GlobalRegistry if configured.
     *
     * @param name Actor name (unqualified or qualified)
     * @return Endpoint string if found, empty string if not found
     */
    std::string lookup_actor_endpoint(const std::string& name);

    /**
     * Send permission token to coordinator (for cross-group messages).
     * Called by ZmqSender to announce remote sends at send time.
     *
     * @param sender_id Full sender ID (e.g., "sim_group.Timer")
     * @param recipient_id Full recipient ID (e.g., "java_mm_group.java_market_maker")
     */
    void send_permission_token(const std::string& sender_id, const std::string& recipient_id);

    /**
     * Get group name (made public for ZmqSender to build sender IDs).
     */
    const char* get_name() const override { return name; }

  protected:
    void process_message(const Message* m) override;
    void init() override {}
    void end() override;

    void start_handler(const msg::Start* m);
    void shutdown_handler(const msg::Shutdown* m);
    void shutdown_this_actor_handler(const msg::ShutdownThisActor* m);
    void forward(const Message* m);

    // Handler for dynamic actor addition
    void add_actor_handler(const msg::AddActor* m);

  private:
    // Coordination helpers
    void connect_to_coordinator();
    void register_with_coordinator();
    void disconnect_from_coordinator();

    // MESSAGE_HANDLER declarations for coordination protocol
    void on_register_ack(const coordination::RegisterAckMessage* msg);
    void on_register_nack(const coordination::RegisterNackMessage* msg);
    void on_permission_grant(const coordination::PermissionGrantMessage* msg);
    void on_permission_wait(const coordination::PermissionWaitMessage* msg);

    // Permission protocol helpers
    void request_permission(const std::string& actor_id);
    void send_permission_done(const std::string& actor_id, uint64_t sequence);

    // Registry helpers
    void connect_to_registry();
    void register_with_registry();
    void disconnect_from_registry();
  };
}
