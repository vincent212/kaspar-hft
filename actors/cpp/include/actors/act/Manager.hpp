#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"

// Forward declarations
namespace actors::registry {
class RegistryClient;
}

namespace actors
{

class ZmqSender;
class ZmqReceiver;

  /**
   * Manager - Manages the lifecycle of actors
   *
   * The Manager:
   * - Registers actors and starts their threads
   * - Handles CPU affinity and thread priority
   * - Coordinates startup and shutdown
   * - Provides actor lookup (local and remote via GlobalRegistry)
   *
   * Usage:
   *   class MyManager : public actors::Manager {
   *   public:
   *     MyManager() {
   *       set_registry("tcp://localhost:5555");  // Connect to GlobalRegistry
   *       manage(new MyActor(), {0}, 50, SCHED_FIFO);  // Pin to CPU 0
   *     }
   *   };
   *
   *   MyManager mgr;
   *   mgr.init();  // Start all actors
   *
   *   // Actors can lookup other actors by name (local or remote)
   *   ActorRef ref = mgr.get_actor_by_name("OtherActor");
   *   ref.send(new MyMessage{}, this);
   *
   *   mgr.end();   // Wait for actors to finish
   */
  class Manager : public Actor
  {
    std::list<actor_ptr> actor_list;
    std::list<std::thread*> thread_list;
    std::map<std::string, actor_ptr> managed_name_map;
    std::map<std::string, actor_ptr> expanded_name_map;

    // Registry support
    std::unique_ptr<registry::RegistryClient> registry_client_;
    std::shared_ptr<ZmqSender> zmq_sender_;
    std::string local_endpoint_;

  public:
    Manager(const std::string& manager_name = "");
    ~Manager();

  protected:
    /// Handle internal messages (Start, Shutdown)
    void process_message(const Message* m) override;

  public:
    /**
     * Start all managed actors
     * Sends Start message to each actor and launches their threads.
     * Call this after registering all actors with manage().
     */
    void init() override;

    /**
     * Wait for all actors to finish
     * Blocks until all actor threads have terminated.
     */
    void end() override;

    /**
     * Add actor to management queue (does NOT start it!)
     *
     * IMPORTANT: This only adds the actor to the list. The actor will NOT receive
     * messages until init() is called. For dynamic actors created after init(),
     * use manage_and_start() instead.
     *
     * @param actor The actor to queue for management (takes ownership)
     * @param affinity Set of CPU cores to pin the actor to (empty = no pinning)
     * @param priority Thread priority 1-99 (requires CAP_SYS_NICE, 0 = default)
     * @param priority_type SCHED_OTHER (default), SCHED_FIFO, or SCHED_RR
     */
    void add_to_manage_q(actor_ptr actor,
                         std::set<int> affinity = {},
                         int priority = 0,
                         int priority_type = SCHED_OTHER);

    /**
     * Manage and immediately start a single actor
     * @param actor The actor to manage and start
     * @param affinity Set of CPU cores to pin the actor to (empty = no pinning)
     * @param priority Thread priority 1-99 (requires CAP_SYS_NICE, 0 = default)
     * @param priority_type SCHED_OTHER (default), SCHED_FIFO, or SCHED_RR
     */
    void manage_and_start(actor_ptr actor,
                          std::set<int> affinity = {},
                          int priority = 0,
                          int priority_type = SCHED_OTHER);

    /**
     * Stop and unmanage an actor
     * Sends Shutdown message, waits for thread to finish, removes from management
     * @param actor The actor to unmanage
     */
    void unmanage(actor_ptr actor);

    /**
     * Connect to a GlobalRegistry for cross-process actor lookup.
     * Must be called before manage() if you want actors auto-registered.
     *
     * @param registry_endpoint ZMQ endpoint of GlobalRegistry (e.g., "tcp://localhost:5555")
     * @param local_endpoint ZMQ endpoint where this Manager's actors are reachable
     * @param zmq_sender Shared ZMQ sender for creating remote ActorRefs
     */
    void set_registry(const std::string& registry_endpoint,
                      const std::string& local_endpoint,
                      std::shared_ptr<ZmqSender> zmq_sender);

    /**
     * Find an actor by name (local or remote via GlobalRegistry).
     *
     * First checks local actors, then queries GlobalRegistry if connected.
     *
     * @param name Actor name to search for
     * @return ActorRef for the actor (local or remote)
     * @throws registry::ActorNotFoundError if not found locally or in registry
     * @throws registry::ActorOfflineError if found but manager is offline
     */
    ActorRef get_actor_by_name(const std::string& name);

    /**
     * Find a local actor by name (does not query registry).
     * @param name Actor name to search for
     * @return Pointer to actor, or nullptr if not found locally
     */
    actor_ptr get_local_actor(const std::string& name) const noexcept;

    /**
     * Get map of all actor names to actor pointers
     * Includes actors inside groups.
     */
    const std::map<std::string, actor_ptr>& get_name_map() const noexcept {
      return expanded_name_map;
    }

    /**
     * Get list of all managed actor names
     * Includes actors inside groups.
     */
    std::list<std::string> get_managed_names() const noexcept;

    /**
     * Get list of all top-level managed actors
     * Groups are returned as single entries (not expanded).
     */
    std::list<actor_ptr> get_managed_actors() const noexcept { return actor_list; }

    /**
     * Get total pending messages across all actors
     * Useful for monitoring backpressure.
     */
    std::size_t total_queue_length();

    /**
     * Get pending message count per actor
     * @return Map of actor name to queue length
     */
    std::map<std::string, std::size_t> get_queue_lengths() const noexcept;

    /**
     * Get thread ID and message count per actor
     * @return Map of actor name to (tid, message_count) tuple
     */
    std::map<std::string, std::tuple<pid_t, int>> get_message_counts() const noexcept;
  };
}
