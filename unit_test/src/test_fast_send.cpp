/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Actor::fast_send ownership contract.
 *
 * fast_send runs the receiver's handler inline and never deletes the request:
 * the caller owns it. The reply, if any, comes back as a unique_ptr the caller
 * also owns. fast_send(new X(...)) used to compile and leaked every call, so
 * the API now takes the request by reference and rejects a non-const pointer
 * at compile time; a const pointer is still accepted, for forwarding a message
 * a handler received.
 *
 * These tests pin that contract: the request is neither copied nor freed, the
 * reply is freed exactly once by its unique_ptr, and the overload set accepts
 * and rejects exactly what it should (checked with static_assert, so a
 * regression fails the build of this file).
 */

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/Message.hpp"

namespace
{
  // Counts live instances, so a test can see whether fast_send freed or
  // copied a message.
  struct Req : public actors::MessageT<Req>
  {
    static inline int alive = 0;
    int v;
    explicit Req(int _v) : v(_v) { ++alive; }
    Req(const Req &o) : actors::MessageT<Req>(o), v(o.v) { ++alive; }
    ~Req() override { --alive; }
  };

  struct Rep : public actors::MessageT<Rep>
  {
    static inline int alive = 0;
    int v;
    explicit Rep(int _v) : v(_v) { ++alive; }
    ~Rep() override { --alive; }
  };

  // Has a registered handler, which does not reply.
  struct Silent : public actors::MessageT<Silent>
  {
  };

  // Has no registered handler: fast_send falls back to process_message, the
  // way OB takes Subscribe and BBBOSub.
  struct Unhandled : public actors::MessageT<Unhandled>
  {
    static inline int alive = 0;
    Unhandled() { ++alive; }
    ~Unhandled() override { --alive; }
  };

  class Echo : public actors::Actor
  {
  public:
    const Req *seen = nullptr;
    const actors::Actor *seen_sender = nullptr;
    bool seen_last = false;
    int calls = 0;
    const actors::Message *fallback_seen = nullptr;
    const actors::Actor *fallback_sender = nullptr;

    Echo()
    {
      strncpy(name, "Echo", sizeof(name) - 1);
      MESSAGE_HANDLER(Req, on_req);
      MESSAGE_HANDLER(Silent, on_silent);
    }
    const char *get_name() const override { return "Echo"; }

  protected:
    void process_message(const actors::Message *m) override
    {
      fallback_seen = m;
      fallback_sender = m->sender;
      if (dynamic_cast<const Unhandled *>(m))
        reply(new Rep(-1));
    }

  private:
    void on_req(const Req *m) noexcept
    {
      ++calls;
      seen = m;
      seen_sender = m->sender;
      seen_last = m->last;
      reply(new Rep(m->v * 2));
    }
    void on_silent(const Silent *) noexcept { ++calls; }
  };

  class Caller : public actors::Actor
  {
  public:
    const char *get_name() const override { return "Caller"; }
  };

  // ---- overload set, checked at compile time --------------------------------

  template <class Target, class Arg>
  concept FastSendable = requires(Target &t, Arg a) { t.fast_send(a, nullptr); };

  // Actor
  static_assert(FastSendable<actors::Actor, const Req &>, "stack object by reference");
  static_assert(FastSendable<actors::Actor, Req &>, "non-const lvalue binds to const&");
  static_assert(FastSendable<actors::Actor, const Req *>, "const pointer (forwarding)");
  static_assert(FastSendable<actors::Actor, const actors::Message *>, "const base pointer");
  static_assert(!FastSendable<actors::Actor, Req *>, "fast_send(new Req) must not compile");
  static_assert(!FastSendable<actors::Actor, actors::Message *>, "non-const base pointer rejected");

  // LocalActorRef
  static_assert(FastSendable<actors::LocalActorRef, const Req &>);
  static_assert(FastSendable<actors::LocalActorRef, const Req *>);
  static_assert(!FastSendable<actors::LocalActorRef, Req *>);

  // ActorRef (local/remote variant)
  static_assert(FastSendable<actors::ActorRef, const Req &>);
  static_assert(FastSendable<actors::ActorRef, const Req *>);
  static_assert(!FastSendable<actors::ActorRef, Req *>);

  // ---- runtime contract ------------------------------------------------------

  class FastSendTest : public ::testing::Test
  {
  protected:
    void SetUp() override
    {
      Req::alive = 0;
      Rep::alive = 0;
      Unhandled::alive = 0;
    }
    Echo echo;
    Caller caller;
  };

} // namespace

// The handler sees the caller's object itself: no copy is made and nothing
// frees it, so it is still alive (and unchanged) after fast_send returns.
TEST_F(FastSendTest, RequestIsNeitherCopiedNorFreed)
{
  {
    Req req(21);
    auto reply = echo.fast_send(req, &caller);
    EXPECT_EQ(echo.calls, 1);
    EXPECT_EQ(echo.seen, &req);
    EXPECT_EQ(Req::alive, 1);
    EXPECT_EQ(req.v, 21);
  }
  EXPECT_EQ(Req::alive, 0);
}

// The reply is the caller's: one live instance while the unique_ptr holds it,
// none once it goes out of scope.
TEST_F(FastSendTest, ReplyIsOwnedByCallerAndFreedOnce)
{
  Req req(21);
  {
    auto reply = echo.fast_send(req, &caller);
    ASSERT_NE(reply, nullptr);
    auto rep = dynamic_cast<const Rep *>(reply.get());
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->v, 42);
    EXPECT_EQ(Rep::alive, 1);
  }
  EXPECT_EQ(Rep::alive, 0);
}

// fast_send stamps the sender and last fields on the request before dispatch.
TEST_F(FastSendTest, StampsSenderAndLast)
{
  Req req(1);
  auto reply = echo.fast_send(req, &caller);
  EXPECT_EQ(echo.seen_sender, &caller);
  EXPECT_TRUE(echo.seen_last);
  EXPECT_EQ(req.sender, &caller);
}

TEST_F(FastSendTest, NullSenderIsAllowed)
{
  Req req(1);
  auto reply = echo.fast_send(req, nullptr);
  EXPECT_EQ(echo.seen_sender, nullptr);
  ASSERT_NE(reply, nullptr);
}

// The const-pointer overload (used to forward a message a handler received)
// behaves exactly like the reference overload.
TEST_F(FastSendTest, ConstPointerFormForwardsWithoutFreeing)
{
  const Req req(5);
  const Req *fwd = &req;
  auto reply = echo.fast_send(fwd, &caller);
  EXPECT_EQ(echo.seen, &req);
  ASSERT_NE(reply, nullptr);
  EXPECT_EQ(static_cast<const Rep *>(reply.get())->v, 10);
  EXPECT_EQ(Req::alive, 1);
}

// A handler that does not reply yields an empty unique_ptr, not a stale reply
// from an earlier call.
TEST_F(FastSendTest, NoReplyGivesNull)
{
  Req req(1);
  auto first = echo.fast_send(req, &caller);
  ASSERT_NE(first, nullptr);

  Silent s;
  auto second = echo.fast_send(s, &caller);
  EXPECT_EQ(second, nullptr);
  EXPECT_EQ(echo.calls, 2);
}

// Many calls with stack requests leave nothing behind: the counters only stay
// at zero if fast_send neither leaks nor double-frees.
TEST_F(FastSendTest, RepeatedCallsBalance)
{
  for (int i = 0; i < 1000; ++i)
  {
    Req req(i);
    auto reply = echo.fast_send(req, &caller);
    ASSERT_NE(reply, nullptr);
    ASSERT_EQ(static_cast<const Rep *>(reply.get())->v, 2 * i);
  }
  EXPECT_EQ(echo.calls, 1000);
  EXPECT_EQ(Req::alive, 0);
  EXPECT_EQ(Rep::alive, 0);
}

// Through LocalActorRef / ActorRef the contract is the same.
TEST_F(FastSendTest, ActorRefForwardsReference)
{
  actors::ActorRef ref(&echo);
  Req req(4);
  auto reply = ref.fast_send(req, &caller);
  EXPECT_EQ(echo.seen, &req);
  ASSERT_NE(reply, nullptr);
  EXPECT_EQ(static_cast<const Rep *>(reply.get())->v, 8);
  EXPECT_EQ(Req::alive, 1);
}

// No registered handler: fast_send hands the caller's object to
// process_message (same address, sender stamped), does not free it, and
// returns what process_message replied.
TEST_F(FastSendTest, UnhandledTypeFallsBackToProcessMessage)
{
  {
    Unhandled u;
    auto reply = echo.fast_send(u, &caller);
    EXPECT_EQ(echo.calls, 0);
    EXPECT_EQ(echo.fallback_seen, &u);
    EXPECT_EQ(echo.fallback_sender, &caller);
    EXPECT_EQ(Unhandled::alive, 1);
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(static_cast<const Rep *>(reply.get())->v, -1);
  }
  EXPECT_EQ(Unhandled::alive, 0);
  EXPECT_EQ(Rep::alive, 0);
}
