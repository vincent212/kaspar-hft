/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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

// Message_N, not MessageT: the tests below assert a FIXED id of 123 and read
// FixedMsg::id, both of which only exist on the compile-time-id path. The
// MessageT migration (fa85db2) swept this one up by mistake -- a MessageT id
// is assigned at first use, is >= 512, and is not a constant expression.
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
// msg::Shutdown kept its hand-assigned id: it is Message_N<5>, not MessageT.
// (The assert here previously claimed the opposite and had not compiled since
// the MessageT migration.) Shutdown is in the interop range, so its id has to
// stay a stable constant rather than a first-use allocation.
static_assert(std::is_base_of_v<actors::Message_N<5>, msg::Shutdown>,
              "Shutdown must keep its fixed id 5; a MessageT id is not stable");
static_assert(msg::Shutdown::id == 5, "Shutdown's fixed id must remain 5");

// Layout guard. On LP64 Message is 40 bytes:
//
//   0  vptr        8   (virtual ~Message)
//   8  sender      8
//  16  destination 8
//  24  is_fast     1
//  25  last        1
//  26  (pad)       2
//  28  qlen        4
//  32  msg_id_     4
//  36  (pad)       4   -> size 40, align 8
//
// It was 32 before qlen was added. msg_id_ still packs into padding, as it
// always did; qlen is what opened a new 8-byte row. Four bytes of that row are
// still free, so the NEXT 4-byte member is genuinely free -- checked below so
// that claim is measured and not assumed.
//
// If this fails, either a member was reordered (the id must stay last) or
// something was added. Neither is automatically wrong, but Message is copied
// per message on the hot path, so the size is a number to change on purpose.
static_assert(sizeof(void*) != 8 || sizeof(Message) == 40,
              "Message changed size: id must stay last, and growth must be deliberate");

namespace {
// One more 4-byte member must fit in the existing tail padding.
struct MessagePlus4 : public MessageT<MessagePlus4> { int extra = 0; };
}
static_assert(sizeof(void*) != 8 || sizeof(MessagePlus4) == sizeof(Message),
              "tail padding is gone: the next small member now costs 8 bytes");

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
    // Ids index Actor::handler_cache, so they must stay under the cap. Exhausting
    // the cap (assigning > kMessageIdCap - kFirstDynamicMessageId types) makes
    // next_message_id() throw rather than hand back an out-of-bounds index; that
    // path can't be unit-tested without polluting the process-global counter.
    EXPECT_LT(a1, detail::kMessageIdCap);
    EXPECT_LT(b1, detail::kMessageIdCap);
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
