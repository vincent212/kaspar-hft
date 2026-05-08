#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

namespace actors
{
  class Actor;

  /**
   * Base class for all messages in the actor system
   *
   * Messages are the only way actors communicate.
   * Each message type has a unique ID (0-511 for optimal performance).
   */
  struct Message
  {
    virtual int get_message_id() const = 0;
    mutable Actor *sender = nullptr;
    mutable Actor *destination = nullptr;
    mutable bool is_fast = false;
    mutable bool last = false;

    Message() = default;

    Message(const Message& other)
      : sender(other.sender)
      , destination(nullptr)
      , is_fast(other.is_fast)
      , last(other.last)
    {}

    Message& operator=(const Message& other) {
      if (this != &other) {
        sender = other.sender;
        destination = nullptr;
        is_fast = other.is_fast;
        last = other.last;
      }
      return *this;
    }

    virtual ~Message() = default;
  };

  /**
   * Template for creating message types with a specific ID
   *
   * Usage:
   *   struct MyMessage : public actors::Message_N<100> {
   *     int data;
   *     MyMessage(int d) : data(d) {}
   *   };
   *
   * Use IDs 0-511 for best performance (handler cache optimization)
   */
  template <int N>
  struct Message_N : public Message
  {
    constexpr int get_message_id() const override { return N; }
  };
}

typedef actors::Message* msg_ptr;
typedef const actors::Message* const_msg_ptr;
typedef const actors::Message* cmsgt;  // compatibility alias
