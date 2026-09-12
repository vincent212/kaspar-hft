#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/Message.hpp"
#include <typeindex>

namespace unit_test {

/**
 * TestHelper - Provides access to protected Actor methods for testing
 *
 * Since process_message_internal is protected, we use the public handlers map
 * and invoke handlers directly. This is the cleanest approach that doesn't
 * require modifying the Actor base class.
 *
 * Usage:
 *   MarketMaker mm(&mock_timer, &mock_pm, "ZNH6", 10);
 *   actors::msg::Start start_msg;
 *   TestHelper::invoke_handler(&mm, &start_msg);
 */
class TestHelper {
public:
  /**
   * Invoke the registered handler for a message type on an actor.
   * This accesses the public 'handlers' map to call the handler directly.
   *
   * @param actor   The actor to invoke the handler on
   * @param msg     The message to process
   * @param sender  Optional sender actor (set on message->sender)
   * @return true if a handler was found and invoked, false otherwise
   */
  static bool invoke_handler(actors::Actor* actor, const actors::Message* msg,
                             actors::Actor* sender = nullptr) {
    // Set sender on message for reply routing
    const_cast<actors::Message*>(msg)->sender = sender;

    // Look up handler in the public handlers map
    std::type_index ti(typeid(*msg));
    auto it = actor->handlers.find(ti);
    if (it != actor->handlers.end()) {
      // Invoke the member function pointer with the actor instance
      // handlers stores void (Actor::*)(const Message*) pointers
      auto handler = it->second;
      (actor->*handler)(msg);
      return true;
    }
    return false;
  }

  /**
   * Process multiple messages in sequence
   */
  template<typename... Messages>
  static void invoke_handlers(actors::Actor* actor, actors::Actor* sender,
                              Messages*... msgs) {
    (invoke_handler(actor, msgs, sender), ...);
  }
};

} // namespace unit_test
