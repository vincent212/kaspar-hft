#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <atomic>
#include <stdexcept>
#include <string>

namespace actors
{
  class Actor;

  /**
   * Base class for all messages in the actor system
   *
   * Messages are the only way actors communicate. Each message type has an
   * integer id used for O(1) handler dispatch.
   *
   * The id is stored as a data member (`msg_id_`) rather than returned by a
   * virtual function: `Actor::call_handler` reads it on every message, and a
   * virtual call there cannot be devirtualised (the static type is
   * `const Message*`). A member read is a single load. The id is set once at
   * construction and is immutable identity thereafter.
   *
   * `msg_id_` is declared last so it packs into the tail padding after the two
   * bools; the object size is unchanged.
   */
  struct Message
  {
    // Non-virtual: a plain member read, not a vtable dispatch.
    int get_message_id() const noexcept { return msg_id_; }

    mutable Actor *sender = nullptr;
    mutable Actor *destination = nullptr;
    mutable bool is_fast = false;
    mutable bool last = false;

    Message(const Message& other)
      : sender(other.sender)
      , destination(nullptr)
      , is_fast(other.is_fast)
      , last(other.last)
      , msg_id_(other.msg_id_)  // identity is preserved across a copy
    {}

    Message& operator=(const Message& other) {
      if (this != &other) {
        sender = other.sender;
        destination = nullptr;
        is_fast = other.is_fast;
        last = other.last;
        // msg_id_ is identity: not reassigned.
      }
      return *this;
    }

    virtual ~Message() = default;  // messages are deleted through Message*

  protected:
    // Protected so Message stays uninstantiable now that the pure virtual is
    // gone; subclasses supply their id via Message_N<N> or MessageT<Derived>.
    explicit Message(int id) noexcept : msg_id_(id) {}

  private:
    int msg_id_;
  };

  /**
   * Message type with a fixed, compile-time id.
   *
   * Usage:
   *   struct MyMessage : public actors::Message_N<100> {
   *     int data;
   *     MyMessage(int d) : data(d) {}
   *   };
   *
   * Ids must be unique per type; there is no compile-time uniqueness check
   * across the tree (run setclassid/setclassid.py to catch duplicates). Prefer
   * MessageT<Derived> for new types, which assigns a collision-free id
   * automatically.
   */
  namespace detail
  {
    // Ids handed out by MessageT start above the hand-assigned 0-511 range so
    // they can never collide with a Message_N<N>.
    inline constexpr int kFirstDynamicMessageId = 512;

    // Every id indexes Actor::handler_cache / dont_have_handler, which are sized
    // ACTOR_HANDLER_CACHE_SIZE. Ids must stay below this cap or dispatch reads
    // out of bounds. This is the single source of truth; Actor.hpp static_asserts
    // that ACTOR_HANDLER_CACHE_SIZE matches it.
    inline constexpr int kMessageIdCap = 2048;
  }

  // A hand-assigned id must live in [0, kFirstDynamicMessageId) so it never
  // collides with the dynamically-assigned MessageT range -- checked at compile
  // time, unlike cross-type uniqueness (see setclassid.py).
  template <int N>
  struct Message_N : public Message
  {
    static_assert(N >= 0 && N < detail::kFirstDynamicMessageId,
                  "Message_N<N> id must be in [0, 512); 512+ is reserved for MessageT");
    static constexpr int id = N;  // compile-time id, for case labels / comparisons
    Message_N() noexcept : Message(N) {}
  };

  namespace detail
  {
    inline std::atomic<int>& message_id_counter() noexcept
    {
      static std::atomic<int> c{kFirstDynamicMessageId};
      return c;
    }
    // Assign the next dynamic id, failing loudly rather than handing back an id
    // that would index past the end of Actor::handler_cache. The number of
    // distinct MessageT types is (returned id - kFirstDynamicMessageId + 1).
    inline int next_message_id()
    {
      const int id = message_id_counter().fetch_add(1, std::memory_order_relaxed);
      if (id >= kMessageIdCap)
      {
        throw std::runtime_error(
          "actors: too many MessageT types (" + std::to_string(id - kFirstDynamicMessageId + 1) +
          "); id " + std::to_string(id) + " would exceed the handler-cache cap of " +
          std::to_string(kMessageIdCap) + " (raise ACTOR_HANDLER_CACHE_SIZE / kMessageIdCap)");
      }
      return id;
    }
  }

  /**
   * One dense, process-unique id per type T, assigned on first use.
   *
   * The function-local static gives thread-safe, once-only initialisation. Ids
   * are NOT stable across runs (assignment follows first-use order) and are not
   * constant expressions -- they are in-process dispatch indices only, never
   * serialised (remote transport keys on the type name). Throws if the id space
   * (kMessageIdCap) is exhausted.
   */
  template <class T>
  inline int message_id()
  {
    static const int id = detail::next_message_id();
    return id;
  }

  /**
   * Message type whose id is auto-assigned (collision-free). CRTP: the base is
   * templated on the derived type so message_id<Derived>() names a distinct id.
   *
   * Usage:
   *   struct MyMessage : public actors::MessageT<MyMessage> { int data; };
   */
  template <class Derived>
  struct MessageT : public Message
  {
    // Not noexcept: the first construction of each Derived assigns its id and
    // throws if the id space is exhausted (see next_message_id).
    MessageT() : Message(message_id<Derived>()) {}
  };
}

typedef actors::Message* msg_ptr;
typedef const actors::Message* const_msg_ptr;
typedef const actors::Message* cmsgt;  // compatibility alias
