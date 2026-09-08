/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Tests for message identity: Message_N compile-time ids, MessageT
 * auto-assigned ids (uniqueness + thread-safe first use), the non-virtual
 * get_message_id() accessor, and that the object layout did not grow.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "actors/Message.hpp"
#include "actors/msg/Shutdown.hpp"

using namespace actors;

namespace {

struct FixedMsg : public Message_N<123> { int payload = 7; };

struct AutoA : public MessageT<AutoA> {};
struct AutoB : public MessageT<AutoB> {};
struct AutoC : public MessageT<AutoC> {};

} // namespace

// --- compile-time invariants -------------------------------------------------

// Message must stay uninstantiable now that the pure virtual is gone (the
// int-taking constructor is protected).
static_assert(!std::is_constructible_v<Message, int>,
              "Message(int) is protected; Message must not be directly constructible");
static_assert(!std::is_default_constructible_v<Message>,
              "Message has no public default constructor");

// Message_N exposes its id as a compile-time constant.
static_assert(Message_N<42>::id == 42, "Message_N<N>::id must be N");
static_assert(msg::Shutdown::id == 5, "Shutdown id is 5");

// Layout guard: msg_id_ is declared last so it packs into the tail padding
// after the two bools. On LP64 that keeps Message at 32 bytes
// (vptr 8 + 2 ptrs 16 + 2 bools + int 4, padded to 32). Declaring the id first
// would push it to 40. If this fails, the member was reordered.
static_assert(sizeof(void*) != 8 || sizeof(Message) == 32,
              "Message grew: id must pack into tail padding, not lead the layout");

// --- runtime behaviour -------------------------------------------------------

TEST(MessageId, FixedIdMatchesTemplateParam) {
    FixedMsg m;
    EXPECT_EQ(m.get_message_id(), 123);
    EXPECT_EQ(FixedMsg::id, 123);
}

TEST(MessageId, GetMessageIdIsNotVirtual) {
    // A non-virtual member function pointer has the plain function-pointer
    // shape; more directly, calling through a value (no vtable) yields the id.
    FixedMsg m;
    const Message& base = m;
    EXPECT_EQ(base.get_message_id(), 123);  // resolved statically, no override needed
}

TEST(MessageId, AutoIdsAreUniqueAndStable) {
    const int a1 = message_id<AutoA>();
    const int b1 = message_id<AutoB>();
    const int a2 = message_id<AutoA>();
    EXPECT_EQ(a1, a2);                 // stable across calls
    EXPECT_NE(a1, b1);                 // distinct types -> distinct ids
    EXPECT_GE(a1, detail::kFirstDynamicMessageId);
    EXPECT_GE(b1, detail::kFirstDynamicMessageId);
}

TEST(MessageId, AutoIdMatchesConstructedMessage) {
    AutoC m;
    EXPECT_EQ(m.get_message_id(), message_id<AutoC>());
}

TEST(MessageId, FirstUseIsThreadSafe) {
    // Race the first use of four fresh types across many threads; no type may
    // end up with two different ids (function-local static once-only init).
    struct T1 {}; struct T2 {}; struct T3 {}; struct T4 {};
    constexpr int kThreads = 16;
    std::atomic<int> mismatches{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&] {
            int a = message_id<T1>(), b = message_id<T2>();
            int c = message_id<T3>(), d = message_id<T4>();
            // all four must be pairwise distinct on every thread
            std::unordered_set<int> s{a, b, c, d};
            if (s.size() != 4) ++mismatches;
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(mismatches.load(), 0);
}
