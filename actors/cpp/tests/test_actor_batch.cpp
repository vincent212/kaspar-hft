/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Actor-level tests for the batch-drain run loop:
 *  - DefaultMailboxIsBQueue: the default Actor mailbox is a plain BQueue (the
 *    sharded / lock-free mailboxes are strictly opt-in). This is a guard test —
 *    it fails if anyone changes the default mailbox type.
 *  - MidBatchTerminateNoLeak: when a handler terminates the actor mid-batch, the
 *    remaining co-drained messages are freed, not leaked.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include "actors/Actor.hpp"
#include "actors/BQueue.hpp"
#include "actors/ShardedBQueue.hpp"

using namespace actors;

// --- guard: the default mailbox must stay a plain BQueue ---------------------

class ProbeActor : public Actor {
public:
  ProbeActor() { strncpy(name, "Probe", sizeof(name)); }
  Queue<const Message *> *mailbox() const { return msgq; }
};

TEST(ActorMailbox, DefaultIsBQueue) {
  ProbeActor a;
  auto *mb = a.mailbox();
  EXPECT_NE(dynamic_cast<BQueue<const Message *> *>(mb), nullptr)
      << "default Actor mailbox must be a BQueue";
  EXPECT_EQ(dynamic_cast<ShardedBQueue<const Message *> *>(mb), nullptr)
      << "default Actor mailbox must NOT be a ShardedBQueue";
}

// --- leak guard: mid-batch terminate frees the rest of the batch -------------

struct CountedMsg : public Message_N<50> {
  static std::atomic<int> alive;
  CountedMsg() { alive.fetch_add(1); }
  ~CountedMsg() override { alive.fetch_sub(1); }
};
std::atomic<int> CountedMsg::alive{0};

// Terminates itself when it processes the FIRST message (like RegistryActor /
// CoordinatorActor, which set terminated from a handler).
class Terminator : public Actor {
  int seen_ = 0;
public:
  Terminator() {
    strncpy(name, "Terminator", sizeof(name));
    MESSAGE_HANDLER(CountedMsg, on_msg);
  }
  void on_msg(const CountedMsg *) {
    if (++seen_ == 1) terminated = true;   // terminate mid-batch
  }
};

TEST(ActorBatch, MidBatchTerminateNoLeak) {
  ASSERT_EQ(CountedMsg::alive.load(), 0);
  {
    Terminator a;
    for (int i = 0; i < 5; ++i) a.send(new CountedMsg(), nullptr);  // 5 queued together
    EXPECT_EQ(CountedMsg::alive.load(), 5);
    a();  // run loop: drains all 5 in one batch, processes #1 (terminates), must
          // delete the remaining 4 rather than leak them
  }
  EXPECT_EQ(CountedMsg::alive.load(), 0)
      << "messages drained after a mid-batch terminate must be freed, not leaked";
}
