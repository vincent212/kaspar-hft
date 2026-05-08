#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <typeinfo>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include "actors/Message.hpp"
#include <mutex>
#include <typeindex>
#include <atomic>
#include <cstring>
#include <cassert>

#define ACTOR_BQUEUE_SIZE 64
#define ACTOR_HANDLER_CACHE_SIZE 2048

// Register a message handler for this actor
// Usage: MESSAGE_HANDLER(MessageType, handler_method)
#define MESSAGE_HANDLER(message_type, function_name)                            \
  {                                                                             \
    typedef typename std::remove_reference<decltype(*this)>::type ActorT;      \
    actors::register_handler<ActorT, message_type>(this)(&ActorT::function_name); \
  }

namespace actors
{
  class Actor;
  class Manager;
  class Group;
}

// Pointer to an Actor
typedef actors::Actor* actor_ptr;
typedef actors::Actor* cfsmp;  // compatibility alias for cfsm code

namespace actors
{

  typedef void (Actor::*generic_handler_t)(const Message *);
  template <class T> class Queue;

  /**
   * Actor - Base class for all actors in the system
   *
   * An Actor is an independent entity that:
   * - Runs in its own thread
   * - Processes messages sequentially from its queue
   * - Communicates with other actors only via messages
   * - Has isolated state (no shared mutable state)
   *
   * Usage:
   *   class MyActor : public actors::Actor {
   *   public:
   *     MyActor() {
   *       MESSAGE_HANDLER(msg::Start, on_start);
   *       MESSAGE_HANDLER(msg::MyMessage, on_my_message);
   *     }
   *   private:
   *     void on_start(const msg::Start*) noexcept { ... }
   *     void on_my_message(const msg::MyMessage* m) noexcept { ... }
   *   };
   */
  class Actor
  {
    friend class Manager;
    friend class Group;

  public:
    Actor();
    virtual ~Actor();

    // Non-copyable
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    /**
     * Send a message asynchronously (fire-and-forget)
     * Message is queued and processed later by receiver's thread
     * @param m Message to send (must be heap-allocated, Actor takes ownership)
     * @param sender The sending actor (for reply routing)
     */
    virtual void send(const Message *m, Actor *sender = nullptr) noexcept;

    /**
     * Send a message synchronously and wait for reply
     * Handler runs immediately in caller's thread
     * @param m Message to send (can be stack-allocated)
     * @param sender The sending actor
     * @return Reply message, or nullptr if no reply
     */
    std::unique_ptr<const Message> fast_send(const Message *m, Actor *sender) noexcept;

    /**
     * Reply to the current message
     * Works for both async (send) and sync (fast_send) messages
     */
    void reply(const Message *m) noexcept;

    virtual const char* get_name() const { return name; }
    std::size_t queue_length() const noexcept;
    const Message* peek() const;
    bool check_is_part_of_group() const { return is_part_of_group; }
    Actor* get_group_ptr() const { return group; }
    bool is_terminated() const noexcept { return terminated; }

    /**
     * Main processing loop - runs in dedicated thread
     * Called by Manager via std::thread
     */
    virtual void operator()() noexcept;

    /// Initiate graceful shutdown
    virtual void terminate() noexcept;

  protected:
    bool terminated = false;
    inline static bool terminate_called = false;
    Actor *reply_to = nullptr;
    long long msg_cnt = 0;
    char name[256];

    /**
     * Override to handle messages not registered via MESSAGE_HANDLER
     */
    virtual void process_message(const Message *) {}

    /**
     * Called before actor starts processing messages
     */
    virtual void init() {}

    /**
     * Called after actor stops processing messages
     */
    virtual void end() {}

    virtual bool is_group() const { return false; }
    virtual void fast_terminate() noexcept;

    // For Group support
    void set_group(Actor *pgroup);
    Actor *get_group() const;
    void process_message_internal(const Message *m, bool dontdel = false) noexcept;

    // Message queue (protected to allow custom operator() implementations)
    Queue<const Message *> *msgq;

  private:
    std::mutex fast_send_mutex;
    bool using_fast_send = false;
    const Message *reply_message = nullptr;
    Actor *group = nullptr;
    std::vector<generic_handler_t> handler_cache;
    std::vector<bool> dont_have_handler;
    bool is_managed = false;
    bool is_part_of_group = false;
    std::set<int> affinity;
    int priority = 0;
    int priority_type = 0;
    std::atomic<Manager*> manager{nullptr};
    pid_t tid = 0;

    // Handler registration (public for macro, but only used internally)
  public:
    std::map<std::type_index, generic_handler_t> handlers;

  protected:
    Manager *get_manager() const { return manager.load(std::memory_order_acquire); }

  private:
    void add_message_to_queue(const Message *m);
    bool call_handler(const Message *m) noexcept;

    void set_manager(Manager *mgr) { manager.store(mgr, std::memory_order_release); }
  };

  // Helper template for registering handlers
  template <typename ActorT, typename MsgT>
  struct register_handler
  {
    Actor *actor;
    register_handler(Actor *a) : actor(a) {}
    typedef void (ActorT::*handler_t)(const MsgT *);

    void operator()(handler_t ptr) const
    {
      generic_handler_t generic_ptr = reinterpret_cast<generic_handler_t>(ptr);
      actor->handlers[std::type_index(typeid(MsgT))] = generic_ptr;
    }
  };

}
