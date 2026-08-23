/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * LockFreeMPSC tests: no-loss/no-dup (incl. many concurrent producers), ring
 * wraparound reuse, the full-ring-then-drain path (push must unblock when the
 * consumer frees a slot), the pop_batch per-call drain cap (must RETURN under a
 * flood instead of looping unbounded), capacity rounding, and the park/wake path.
 */

#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <tuple>
#include "actors/LockFreeMPSC.hpp"

using actors::LockFreeMPSC;
using namespace std::chrono_literals;

static constexpr size_t MAX_DRAIN = 8192;  // must match LockFreeMPSC::kMaxDrain

TEST(LockFreeMPSC, DrainsAllNoLoss) {
  LockFreeMPSC<int> q(1024);
  for (int i = 0; i < 500; ++i) q.push(i);
  EXPECT_EQ(q.length(), 500u);
  std::vector<int> out;
  q.pop_batch(out);
  ASSERT_EQ(out.size(), 500u);
  for (int i = 0; i < 500; ++i) EXPECT_EQ(out[i], i);   // MPSC single producer => FIFO
  EXPECT_TRUE(q.is_empty());
}

TEST(LockFreeMPSC, SinglePopRetrievesEverythingInOrder) {
  LockFreeMPSC<int> q(64);
  for (int i = 0; i < 20; ++i) q.push(i);
  for (int i = 0; i < 20; ++i) {
    auto [v, last] = q.pop();
    EXPECT_EQ(v, i);
    EXPECT_EQ(last, i == 19);
  }
  EXPECT_TRUE(q.is_empty());
}

TEST(LockFreeMPSC, WrapAroundReuseNoLoss) {
  // Small ring, push then pop in lockstep so the ring stays shallow but the
  // position advances far past capacity => cells are reused (sequence wrap).
  LockFreeMPSC<int> q(8);
  for (int i = 0; i < 100000; ++i) {          // 100k >> capacity 8 => ~12.5k wraps
    q.push(i);
    auto [v, last] = q.pop();
    EXPECT_EQ(v, i);
    EXPECT_TRUE(last);                          // depth returns to 0 each round
  }
  EXPECT_TRUE(q.is_empty());
}

TEST(LockFreeMPSC, ManyProducersNoLossNoDup) {
  constexpr int P = 8, K = 100000;
  constexpr long TOTAL = (long)P * K;
  LockFreeMPSC<int> q(1 << 20);
  std::vector<std::thread> prod;
  for (int p = 0; p < P; ++p)
    prod.emplace_back([&q, p] { for (int i = 0; i < K; ++i) q.push(p * K + i); });
  std::vector<char> seen(TOTAL, 0);
  long recv = 0; std::vector<int> buf;
  while (recv < TOTAL) {
    q.pop_batch(buf);
    for (int v : buf) { ASSERT_GE(v, 0); ASSERT_LT(v, TOTAL); ASSERT_FALSE(seen[v]); seen[v] = 1; ++recv; }
  }
  for (auto& t : prod) t.join();
  EXPECT_EQ(recv, TOTAL);
}

// #4: push() must not spin forever on a full ring — it unblocks once the consumer
// frees a slot.
TEST(LockFreeMPSC, PushBlocksWhenFullThenUnblocks) {
  LockFreeMPSC<int> q(8);                 // capacity 8
  for (int i = 0; i < 8; ++i) q.push(i);  // ring now full
  std::atomic<bool> pushed{false};
  std::thread producer([&] { q.push(999); pushed.store(true); });  // blocks: ring full
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(pushed.load()) << "push should still be blocked on a full ring";
  std::vector<int> out;
  q.pop_batch(out);                       // frees slots
  producer.join();
  EXPECT_TRUE(pushed.load());             // push completed once space appeared
}

// #5: pop_batch must RETURN even when producers flood faster than we drain. We
// prove the per-call cap directly: push more than the cap, one call returns the
// cap, the rest come on the next call (no loss, no unbounded batch).
TEST(LockFreeMPSC, PopBatchIsBounded) {
  LockFreeMPSC<int> q(1 << 15);
  const size_t N = MAX_DRAIN + 4096;
  for (size_t i = 0; i < N; ++i) q.push((int)i);
  std::vector<int> out;
  q.pop_batch(out);
  EXPECT_EQ(out.size(), MAX_DRAIN) << "pop_batch must cap the batch, not drain everything";
  std::vector<int> rest;
  q.pop_batch(rest);
  EXPECT_EQ(out.size() + rest.size(), N);   // no loss across the two calls
}

TEST(LockFreeMPSC, CapacityRoundsUpToPowerOfTwo) {
  LockFreeMPSC<int> q(5);                  // rounds up to 8
  for (int i = 0; i < 8; ++i) q.push(i);   // must accept at least 8 without blocking
  EXPECT_EQ(q.length(), 8u);
  LockFreeMPSC<int> q0(0);                 // degenerate: rounds to 1
  q0.push(42);
  auto [v, last] = q0.pop();
  EXPECT_EQ(v, 42); EXPECT_TRUE(last);
}

TEST(LockFreeMPSC, PopBatchBlocksUntilPushedThenWakes) {
  LockFreeMPSC<int> q(64);
  std::vector<int> got;
  std::thread consumer([&] { q.pop_batch(got); });   // parks: empty
  std::this_thread::sleep_for(50ms);
  q.push(7);
  consumer.join();
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], 7);
}
