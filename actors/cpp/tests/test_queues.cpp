/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Unit tests for the actor mailbox queue types (BQueue, BQueueBatched,
 * ShardedBQueue, LockFreeMPSC) and the std::variant mailbox mechanism the
 * actor uses (issue #54).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include "actors/BQueue.hpp"
#include "actors/BQueueBatched.hpp"
#include "actors/ShardedBQueue.hpp"
#include "actors/LockFreeMPSC.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"

using namespace actors;

// --- per-type construction (queues take different sizing args) -------------
template <class Q> struct QMaker;
template <class T> struct QMaker<BQueue<T>> {
  static std::unique_ptr<BQueue<T>> make() { return std::make_unique<BQueue<T>>(1024); }
};
template <class T> struct QMaker<BQueueBatched<T>> {
  static std::unique_ptr<BQueueBatched<T>> make() { return std::make_unique<BQueueBatched<T>>(1024); }
};
template <class T> struct QMaker<ShardedBQueue<T>> {
  static std::unique_ptr<ShardedBQueue<T>> make() { return std::make_unique<ShardedBQueue<T>>(8); }
};
template <class T> struct QMaker<LockFreeMPSC<T>> {
  static std::unique_ptr<LockFreeMPSC<T>> make() { return std::make_unique<LockFreeMPSC<T>>(1u << 16); }
};

// ===========================================================================
// Behaviours common to every queue type
// ===========================================================================
template <class Q> class QueueTest : public ::testing::Test {};
using QueueTypes = ::testing::Types<
    BQueue<int>, BQueueBatched<int>, ShardedBQueue<int>, LockFreeMPSC<int>>;
TYPED_TEST_SUITE(QueueTest, QueueTypes);

TYPED_TEST(QueueTest, BasicPushPop)
{
  auto q = QMaker<TypeParam>::make();
  q->push(42);
  auto [v, last] = q->pop();
  EXPECT_EQ(v, 42);
  EXPECT_TRUE(last) << "queue should report empty after the only item is popped";
}

TYPED_TEST(QueueTest, EmptyAndLength)
{
  auto q = QMaker<TypeParam>::make();
  EXPECT_TRUE(q->is_empty());
  EXPECT_EQ(q->length(), 0u);
  q->push(1);
  q->push(2);
  EXPECT_FALSE(q->is_empty());
  EXPECT_EQ(q->length(), 2u);
  q->pop();
  q->pop();
  EXPECT_TRUE(q->is_empty());
  EXPECT_EQ(q->length(), 0u);
}

TYPED_TEST(QueueTest, Peek)
{
  auto q = QMaker<TypeParam>::make();
  q->push(9);
  EXPECT_EQ(q->peek(), 9);
  EXPECT_EQ(q->length(), 1u) << "peek must not consume";
}

// pop() must BLOCK until an item is pushed (not spin-return a default).
TYPED_TEST(QueueTest, BlockingPopWakesOnPush)
{
  auto q = QMaker<TypeParam>::make();
  std::future<int> f = std::async(std::launch::async, [&] { return std::get<0>(q->pop()); });
  EXPECT_EQ(f.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout)
      << "pop() returned before anything was pushed — it is not blocking";
  q->push(7);
  ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready)
      << "pop() did not wake after push";
  EXPECT_EQ(f.get(), 7);
}

// Multi-producer / single-consumer: no message may be lost or duplicated.
TYPED_TEST(QueueTest, NoLossMPSC)
{
  auto q = QMaker<TypeParam>::make();
  constexpr int P = 4, K = 10000, N = P * K;

  std::atomic<bool> go{false};
  std::vector<std::thread> producers;
  for (int p = 0; p < P; ++p)
    producers.emplace_back([&, p] {
      while (!go.load(std::memory_order_acquire)) { /* spin to start together */ }
      for (int i = 0; i < K; ++i) q->push(p * K + i);
    });

  std::vector<char> seen(N, 0);
  std::thread consumer([&] {
    for (int n = 0; n < N; ++n) {
      int v = std::get<0>(q->pop());
      if (v >= 0 && v < N) seen[v] = 1;
    }
  });

  go.store(true, std::memory_order_release);
  for (auto& t : producers) t.join();
  consumer.join();

  int missing = 0;
  for (int i = 0; i < N; ++i)
    if (!seen[i]) ++missing;
  EXPECT_EQ(missing, 0) << missing << " of " << N << " messages lost or duplicated";
}

// ===========================================================================
// FIFO ordering — for the order-preserving queues only.
// (ShardedBQueue round-robins across lanes and is documented NOT to preserve
//  per-sender FIFO, so it is excluded here.)
// ===========================================================================
template <class Q> void fifo_check()
{
  auto q = QMaker<Q>::make();
  constexpr int N = 1000;
  for (int i = 0; i < N; ++i) q->push(i);
  for (int i = 0; i < N; ++i) {
    auto [v, last] = q->pop();
    ASSERT_EQ(v, i) << "FIFO order violated at " << i;
    EXPECT_EQ(last, i == N - 1);
  }
}
TEST(QueueFIFO, BQueue)        { fifo_check<BQueue<int>>(); }
TEST(QueueFIFO, BQueueBatched) { fifo_check<BQueueBatched<int>>(); }
TEST(QueueFIFO, LockFreeMPSC)  { fifo_check<LockFreeMPSC<int>>(); }

// ===========================================================================
// pop_batch — whole-mailbox drain.
// ===========================================================================
TEST(PopBatch, BQueueBatchedDrainsAllFIFO)
{
  BQueueBatched<int> q(1024);
  constexpr int K = 500;
  for (int i = 0; i < K; ++i) q.push(i);
  std::vector<int> out;
  q.pop_batch(out);
  ASSERT_EQ(out.size(), static_cast<size_t>(K));
  for (int i = 0; i < K; ++i) EXPECT_EQ(out[i], i) << "batch drain must be FIFO";
}

TEST(PopBatch, ShardedDrainsAll)
{
  ShardedBQueue<int> q(8);
  constexpr int K = 500;
  for (int i = 0; i < K; ++i) q.push(i);
  std::vector<int> out;
  q.pop_batch(out);  // no concurrent producers -> drains everything in one call
  ASSERT_EQ(out.size(), static_cast<size_t>(K));
  std::sort(out.begin(), out.end());
  for (int i = 0; i < K; ++i) EXPECT_EQ(out[i], i) << "sharded drain lost/duplicated an item";
}

TEST(PopBatch, LockFreeDrainsAllFIFO)
{
  LockFreeMPSC<int> q(1u << 16);
  constexpr int K = 500;
  for (int i = 0; i < K; ++i) q.push(i);
  std::vector<int> out;
  q.pop_batch(out);
  ASSERT_EQ(out.size(), static_cast<size_t>(K));
  for (int i = 0; i < K; ++i) EXPECT_EQ(out[i], i) << "ring drain must be FIFO";
}

// ===========================================================================
// The std::variant mailbox mechanism itself (the actor's design): these queue
// types are non-movable (mutex/atomic members), so the variant must be built
// in place and dispatched via std::visit. This mirrors Actor::msgq.
// ===========================================================================
TEST(VariantMailbox, VisitPushPopEveryAlternative)
{
  using Mailbox = std::variant<
      BQueue<int>, BQueueBatched<int>, ShardedBQueue<int>, LockFreeMPSC<int>>;

  auto roundtrip = [](Mailbox& mb) {
    std::visit([](auto& q) { q.push(11); }, mb);
    int got = std::visit([](auto& q) { return std::get<0>(q.pop()); }, mb);
    EXPECT_EQ(got, 11);
    EXPECT_TRUE(std::visit([](auto& q) { return q.is_empty(); }, mb));
  };

  Mailbox a(std::in_place_type<BQueue<int>>, 64);        roundtrip(a);
  Mailbox b(std::in_place_type<BQueueBatched<int>>, 64); roundtrip(b);
  Mailbox c(std::in_place_type<ShardedBQueue<int>>, 8);  roundtrip(c);
  Mailbox d(std::in_place_type<LockFreeMPSC<int>>, 1024); roundtrip(d);
}

// ===========================================================================
// Regression tests for the code-review fixes (issue #54 follow-up).
// ===========================================================================

// FIX #1: a full LockFreeMPSC ring must BLOCK the producer and RESUME when the
// consumer frees a slot — not busy-spin forever (the pre-fix behavior was
// `while(!try_push(x)) cpu_relax();`). Uses a future so a regression FAILS the
// test rather than hanging it.
TEST(LockFreeFull, PushBlocksWhenFullThenResumes)
{
  LockFreeMPSC<int> q(4);                 // 4-slot ring
  ASSERT_EQ(q.capacity(), 4u);
  for (int i = 0; i < 4; ++i) q.push(i);  // fill it

  std::future<void> f = std::async(std::launch::async, [&] { q.push(99); });
  EXPECT_EQ(f.wait_for(std::chrono::milliseconds(150)), std::future_status::timeout)
      << "push into a full ring returned/dropped instead of blocking";

  auto [v0, last0] = q.pop();             // free one slot
  (void)last0;
  ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready)
      << "push did not resume after a slot was freed (spin/hang regression)";
  f.get();

  std::vector<int> got{v0};
  for (int i = 0; i < 4; ++i) got.push_back(std::get<0>(q.pop()));
  std::sort(got.begin(), got.end());
  EXPECT_EQ(got, (std::vector<int>{0, 1, 2, 3, 99})) << "a message was lost across the full/resume path";
}

// FIX #3: each queue kind must have a sensible default size (the values
// set_mailbox(kind, cap=0) maps to). Guards against the old default that forced
// ACTOR_BQUEUE_SIZE (64) onto every kind — 64 *lanes* for ShardedBQueue.
TEST(Defaults, PerKindSensibleSizes)
{
  EXPECT_EQ(ShardedBQueue<int>().lanes(), 8u)   << "ShardedBQueue default should be 8 lanes, not 64";
  EXPECT_EQ(LockFreeMPSC<int>().capacity(), 1024u) << "LockFreeMPSC default should be 1024 slots";
}

// FIX #4: peek() runs on a different thread than the consumer's pop(), so
// pull_cursor_ must be atomic. This exercises concurrent peek-while-popping;
// a regression (plain size_t) is a data race that ThreadSanitizer flags, and
// the run must never lose a message or crash regardless.
TEST(ShardedRace, ConcurrentPeekWhilePopping)
{
  ShardedBQueue<int> q(8);
  constexpr int N = 50000;
  std::atomic<bool> done{false};
  std::thread peeker([&] {
    while (!done.load(std::memory_order_acquire)) { volatile int v = q.peek(); (void)v; }
  });

  for (int i = 0; i < N; ++i) q.push(i);
  std::vector<char> seen(N, 0);
  for (int i = 0; i < N; ++i) { int v = std::get<0>(q.pop()); if (v >= 0 && v < N) seen[v] = 1; }

  done.store(true, std::memory_order_release);
  peeker.join();
  int missing = 0;
  for (int i = 0; i < N; ++i) if (!seen[i]) ++missing;
  EXPECT_EQ(missing, 0) << missing << " messages lost while peek() ran concurrently";
}

// FIX #6 (and #2): for a BQueueBatched actor, m->last must mean "mailbox empty
// after this message" — exactly one `true`, on the final message of a fully
// drained batch — matching the single-message path, not "end of this batch
// snapshot". Drives a real actor on its own thread.
namespace {
struct Ping : public actors::MessageT<Ping> { int n; explicit Ping(int n_) : n(n_) {} };

struct LastProbe : public actors::Actor {
  std::vector<char>  lasts;          // read only after join()
  std::atomic<int>   processed{0};
  LastProbe() {
    set_mailbox(actors::Actor::MailboxKind::BQueueBatched);
    MESSAGE_HANDLER(Ping, on_ping);
  }
  void on_ping(const Ping *m) {
    lasts.push_back(m->last ? 1 : 0);
    processed.fetch_add(1, std::memory_order_release);
  }
};
}  // namespace

TEST(BatchedLast, LastMeansMailboxEmptyNotBatchEnd)
{
  LastProbe a;
  constexpr int K = 6;
  for (int i = 0; i < K; ++i) a.send(new Ping(i));   // all enqueued before the consumer starts -> one batch

  std::thread t(std::ref(a));
  for (int spin = 0; spin < 1000 && a.processed.load(std::memory_order_acquire) < K; ++spin)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT_EQ(a.processed.load(std::memory_order_acquire), K) << "actor did not drain the batch";

  a.send(new actors::msg::Shutdown());   // separate batch -> stops the loop
  t.join();

  ASSERT_EQ(a.lasts.size(), static_cast<size_t>(K));
  for (int i = 0; i < K; ++i)
    EXPECT_EQ(a.lasts[i], i == K - 1 ? 1 : 0)
        << "m->last wrong at index " << i << " (expected true only on the last message)";
}
