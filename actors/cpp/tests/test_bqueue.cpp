/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * BQueue unit tests: FIFO/last-flag basics plus the batched-drain scenarios
 * (pop_batch drains ring-then-overflow in FIFO, clears its out-param, blocks
 * until pushed via transition-notify, and stays correct under a concurrent
 * producer/consumer).
 */

#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <chrono>
#include <tuple>
#include "actors/BQueue.hpp"

using actors::BQueue;

TEST(BQueue, FifoAndLastFlag) {
  BQueue<int> q(2);
  q.push(1);
  q.push(2);
  q.push(3);  // spills to overflow
  EXPECT_EQ(q.length(), 3u);
  EXPECT_EQ(q.pop(), std::make_tuple(1, false));
  EXPECT_EQ(q.pop(), std::make_tuple(2, false));
  EXPECT_EQ(q.pop(), std::make_tuple(3, true));  // last
  EXPECT_TRUE(q.is_empty());
}

TEST(BQueue, OverflowStickyPreservesOrder) {
  BQueue<int> q(1);
  for (int i = 0; i < 10; ++i) q.push(i);
  for (int i = 0; i < 10; ++i) {
    auto [v, last] = q.pop();
    EXPECT_EQ(v, i);
    EXPECT_EQ(last, i == 9);
  }
}

// ---- batched-drain scenarios ----

TEST(BQueue, PopBatchDrainsRingFifo) {
  BQueue<int> q(8);
  for (int i = 0; i < 5; ++i) q.push(i);
  std::vector<int> out;
  q.pop_batch(out);
  EXPECT_EQ(out, (std::vector<int>{0, 1, 2, 3, 4}));
  EXPECT_TRUE(q.is_empty());
}

TEST(BQueue, PopBatchDrainsRingThenOverflowFifo) {
  // cap 2: 0,1 in ring; 2,3,4 spill to overflow. One drain returns all five
  // in order (ring first, then overflow).
  BQueue<int> q(2);
  for (int i = 0; i < 5; ++i) q.push(i);
  std::vector<int> out;
  q.pop_batch(out);
  EXPECT_EQ(out, (std::vector<int>{0, 1, 2, 3, 4}));
  EXPECT_TRUE(q.is_empty());
}

TEST(BQueue, PopBatchClearsOutParam) {
  BQueue<int> q(4);
  q.push(42);
  std::vector<int> out{7, 8, 9};  // stale contents must be discarded
  q.pop_batch(out);
  EXPECT_EQ(out, (std::vector<int>{42}));
}

TEST(BQueue, PopBatchReturnsOnlyCurrentlyQueued) {
  BQueue<int> q(8);
  q.push(1);
  q.push(2);
  std::vector<int> out;
  q.pop_batch(out);
  EXPECT_EQ(out, (std::vector<int>{1, 2}));
  q.push(3);  // arrived after the drain
  q.pop_batch(out);
  EXPECT_EQ(out, (std::vector<int>{3}));
}

TEST(BQueue, PopBatchBlocksUntilPushedThenWakes) {
  // A consumer blocked in pop_batch on an empty queue must be woken by a push
  // into the empty queue (exercises the transition-notify path).
  BQueue<int> q(4);
  std::vector<int> got;
  std::thread consumer([&] { q.pop_batch(got); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let it park
  q.push(99);
  consumer.join();
  EXPECT_EQ(got, (std::vector<int>{99}));
}

TEST(BQueue, ConcurrentProducerConsumerInOrder) {
  // Single producer streams 0..N; single consumer drains via pop_batch. Every
  // item must arrive exactly once, in order. Stresses the transition-notify
  // wakeup under a real backlog/drain race.
  constexpr int N = 200000;
  BQueue<int> q(1024);
  std::thread producer([&] {
    for (int i = 0; i < N; ++i) q.push(i);
  });
  int expect = 0;
  std::vector<int> buf;
  while (expect < N) {
    q.pop_batch(buf);
    for (int v : buf) {
      ASSERT_EQ(v, expect);
      ++expect;
    }
  }
  producer.join();
  EXPECT_EQ(expect, N);
}
