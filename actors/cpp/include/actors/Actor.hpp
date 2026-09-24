#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
#include <variant>
#include "actors/BQueue.hpp"
#include "actors/BQueueBatched.hpp"
#include "actors/ShardedBQueue.hpp"
#include "actors/LockFreeMPSC.hpp"

// Mailbox ring slots. Past this depth BQueue::push() falls back to
// overflow_.push_back(), a heap allocation on the PRODUCER thread while the
// mailbox mutex is held — i.e. inside the feed-handler's send path during
// exactly the bursts we are trying to measure. Note this does NOT reduce
// queue depth or queueing latency: overflow_ is unbounded, so nothing was
// ever dropped. The ring size trades allocation frequency against footprint.
//
// 128 slots = 1 KB per actor, and the live recorder runs ~282 actors, so
// ~282 KB total. The ring stays well inside L1d (32-48 KB), so the producer's
// store lands on a line the core already owns. A 32000-slot ring is 256 KB
// per actor: it does not fit in L1d, and the producer walks all 256 KB as the
// ring advances, paying a miss per push on the hot path this work exists to
// measure.
//
// The cost of 128 is that bursts deeper than 128 packets still reach
// overflow_. Two consequences, both measurable rather than assumed:
//   - one allocation under the mailbox mutex per packet past 128,
//   - Message::qlen saturates at 128, because circ_buf_len() counts the ring
//     only (see BQueue.hpp). A reported 128 means "at least 128".
// If the qlen histogram piles up at exactly 128, this number is too small and
// the tail is censored. Check that before trusting any qlen-vs-latency fit.
#define ACTOR_BQUEUE_SIZE 128
#define ACTOR_HANDLER_CACHE_SIZE 2048
// The handler cache is indexed directly by message id, so its size is the id
// ceiling enforced in Message.hpp (kMessageIdCap). Keep the two in lockstep.
static_assert(ACTOR_HANDLER_CACHE_SIZE == actors::detail::kMessageIdCap,
              "ACTOR_HANDLER_CACHE_SIZE must equal actors::detail::kMessageIdCap");

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

    // Which concrete mailbox an actor uses (see set_mailbox). Public so callers
    // can name a kind; the setter itself is protected (an actor selects its own
    // mailbox, in its constructor, before its thread starts).
    enum class MailboxKind { BQueue, BQueueBatched, ShardedBQueue, LockFreeMPSC };

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
    // Unlocked, approximate, ring-only depth. For samplers that must not
    // contend on the mailbox mutex. See Queue::circ_buf_len.
    std::size_t circ_buf_len() const noexcept;
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

    // The mailbox. Held BY VALUE in a std::variant of the concrete queue types
    // (a closed set) rather than behind a Queue<T>* base pointer. std::visit
    // hands the producer/consumer a CONCRETE queue reference, so push/pop are
    // devirtualized and the consumer drain loop inlines — no per-message vtable
    // indirection. See tech_reports/queue_dispatch_design.md (issue #54).
    using MailboxMsg = const Message *;
    using Mailbox = std::variant<
        BQueue<MailboxMsg>,
        BQueueBatched<MailboxMsg>,
        ShardedBQueue<MailboxMsg>,
        LockFreeMPSC<MailboxMsg>>;
    Mailbox msgq;

    // Replace the mailbox with `kind`. Select BEFORE the actor thread starts
    // (e.g. in the constructor); switching a live mailbox is not safe.
    //
    // `cap` is a sizing hint that means different things per kind:
    //   BQueue / BQueueBatched : ring/overflow size
    //   ShardedBQueue          : LANE count
    //   LockFreeMPSC           : ring capacity (rounded up to a power of two)
    // Pass cap = 0 (the default) to use each kind's own sensible default rather
    // than forcing one number across kinds — ACTOR_BQUEUE_SIZE for BQueue(Batched),
    // 8 lanes for ShardedBQueue, 1024 slots for LockFreeMPSC.
    void set_mailbox(MailboxKind kind, size_t cap = 0)
    {
      switch (kind) {
        case MailboxKind::BQueue:        msgq.emplace<BQueue<MailboxMsg>>(cap ? cap : ACTOR_BQUEUE_SIZE); break;
        case MailboxKind::BQueueBatched: msgq.emplace<BQueueBatched<MailboxMsg>>(cap ? cap : ACTOR_BQUEUE_SIZE); break;
        case MailboxKind::ShardedBQueue: msgq.emplace<ShardedBQueue<MailboxMsg>>(cap ? cap : 8); break;
        case MailboxKind::LockFreeMPSC:  msgq.emplace<LockFreeMPSC<MailboxMsg>>(cap ? cap : 1024); break;
      }
    }

  private:
    // Consumer drain loop, instantiated per concrete queue type via std::visit
    // (defined in Actor.cpp). BQueueBatched drains the whole mailbox per lock;
    // the others drain one message at a time.
    template <class Q> void run_loop(Q& q) noexcept;

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
