#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/Message.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include <vector>
#include <memory>
#include <cstring>

namespace unit_test {

/**
 * MockActor - Base class for mocking actors in unit tests
 *
 * Captures all messages sent to this actor, allowing tests to verify
 * that the correct messages were sent.
 *
 * Usage:
 *   MockActor mock_timer("MockTimer");
 *   actor_under_test->send(new SomeMessage(), &mock_timer);
 *   // Later, verify messages received by mock_timer
 *   EXPECT_EQ(mock_timer.message_count(), 1);
 *   auto msg = mock_timer.get_message<ExpectedMsgType>(0);
 *   EXPECT_EQ(msg->some_field, expected_value);
 */
class MockActor : public actors::Actor {
protected:
  std::vector<const actors::Message*> captured;
  char mock_name[256];
  actors::Actor* last_sender = nullptr;

public:
  MockActor(const char* name = "MockActor") {
    std::strncpy(mock_name, name, sizeof(mock_name) - 1);
    mock_name[sizeof(mock_name) - 1] = '\0';
    std::strncpy(this->name, name, sizeof(this->name) - 1);
    this->name[sizeof(this->name) - 1] = '\0';
  }

  const char* get_name() const override { return mock_name; }

  /**
   * Override send to capture messages instead of queuing them
   * This allows synchronous testing without threading
   */
  void send(const actors::Message* m, actors::Actor* sender = nullptr) noexcept override {
    captured.push_back(m);
    last_sender = sender;
  }

  /**
   * Get the number of messages captured
   */
  size_t message_count() const { return captured.size(); }

  /**
   * Get a captured message by index, cast to the specified type
   * Returns nullptr if index is out of range or type doesn't match
   */
  template<typename T>
  const T* get_message(size_t idx) const {
    if (idx < captured.size()) {
      return dynamic_cast<const T*>(captured[idx]);
    }
    return nullptr;
  }

  /**
   * Get the raw message at index (without type cast)
   */
  const actors::Message* get_raw_message(size_t idx) const {
    if (idx < captured.size()) {
      return captured[idx];
    }
    return nullptr;
  }

  /**
   * Get the sender of the last message
   */
  actors::Actor* get_last_sender() const { return last_sender; }

  /**
   * Clear all captured messages
   * Note: This deletes the messages, so don't use them after clearing
   */
  void clear() {
    for (auto m : captured) {
      delete m;
    }
    captured.clear();
    last_sender = nullptr;
  }

  /**
   * Check if any message of the given type was captured
   */
  template<typename T>
  bool has_message_of_type() const {
    for (const auto* m : captured) {
      if (dynamic_cast<const T*>(m) != nullptr) {
        return true;
      }
    }
    return false;
  }

  /**
   * Count messages of a specific type
   */
  template<typename T>
  size_t count_messages_of_type() const {
    size_t count = 0;
    for (const auto* m : captured) {
      if (dynamic_cast<const T*>(m) != nullptr) {
        count++;
      }
    }
    return count;
  }

  ~MockActor() override {
    clear();
  }
};

// Convenience typedefs for common mock actors
class MockTimer : public MockActor {
public:
  MockTimer() : MockActor("MockTimer") {}
};

class MockPositionManager : public MockActor {
public:
  MockPositionManager() : MockActor("MockPM") {}
};

class MockOB : public MockActor {
public:
  MockOB(const char* name = "MockOB") : MockActor(name) {}
};

class MockLight : public MockActor {
public:
  MockLight(const char* name = "MockLight") : MockActor(name) {}
};

class MockSOM : public MockActor {
  int next_oid = 1;
public:
  MockSOM() : MockActor("MockSOM") {
    // Register handler for Order messages to return Ack
    MESSAGE_HANDLER(frame::som::msg::Order, order_handler);
    MESSAGE_HANDLER(frame::som::msg::Cancel, cancel_handler);
  }

  void order_handler(const frame::som::msg::Order* o) {
    // Capture the order for inspection
    auto* copy = new frame::som::msg::Order(*o);
    copy->oid = next_oid++;
    captured.push_back(copy);
    // Reply with Ack
    reply(new frame::som::msg::Ack(copy->oid));
  }

  void cancel_handler(const frame::som::msg::Cancel* c) {
    // Capture the cancel for inspection
    captured.push_back(new frame::som::msg::Cancel(*c));
    // Reply with CancAck (id, fillsz, sz)
    reply(new frame::som::msg::CancAck(c->id, 5, 0));  // fillsz=5 cancelled, sz=0 remains
  }
};

class MockAggregator : public MockActor {
public:
  MockAggregator() : MockActor("MockAgg") {}
};

class MockDB : public MockActor {
public:
  MockDB() : MockActor("MockDB") {}
};

class MockSuper : public MockActor {
public:
  MockSuper() : MockActor("MockSuper") {}
};

} // namespace unit_test
