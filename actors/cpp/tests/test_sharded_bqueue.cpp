/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * ShardedBQueue tests. Round-robin sharding does NOT preserve FIFO order, so the
 * correctness properties are: (1) no message lost or duplicated, and (2) the
 * consumer wakes when a producer pushes into an empty queue. The headline test
 * is many concurrent producers -> one consumer with zero loss/dup, which is the
 * high-contention case the sharded mailbox exists for.
 */

#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <chrono>
#include <tuple>
#include "actors/ShardedBQueue.hpp"

using actors::ShardedBQueue;

TEST(ShardedBQueue, DrainsAllItemsNoLoss) {
  ShardedBQueue<int> q(8);
  for (int i = 0; i < 100; ++i) q.push(i);
  EXPECT_EQ(q.length(), 100u);
  std::vector<int> out;
  q.pop_batch(out);
  ASSERT_EQ(out.size(), 100u);
  std::vector<char> seen(100, 0);
  for (int v : out) { ASSERT_GE(v, 0); ASSERT_LT(v, 100); EXPECT_FALSE(seen[v]); seen[v] = 1; }
  EXPECT_TRUE(q.is_empty());
}

TEST(ShardedBQueue, SinglePopRetrievesEverything) {
  ShardedBQueue<int> q(4);
  for (int i = 0; i < 20; ++i) q.push(i);
  std::vector<char> seen(20, 0);
  for (int i = 0; i < 20; ++i) {
    auto [v, last] = q.pop();
    ASSERT_GE(v, 0); ASSERT_LT(v, 20);
    EXPECT_FALSE(seen[v]); seen[v] = 1;
    EXPECT_EQ(last, i == 19);  // last == queue now empty
  }
  EXPECT_TRUE(q.is_empty());
}

TEST(ShardedBQueue, PopBatchBlocksUntilPushedThenWakes) {
  ShardedBQueue<int> q(8);
  std::vector<int> got;
  std::thread consumer([&] { q.pop_batch(got); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let it park
  q.push(99);
  consumer.join();
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], 99);
}

TEST(ShardedBQueue, ManyProducersNoLossNoDup) {
  // 8 producers hammering one 8-lane mailbox; one consumer drains via pop_batch.
  // Every value must arrive exactly once (order is not guaranteed).
  constexpr int P = 8;
  constexpr int K = 100000;
  constexpr long TOTAL = (long)P * K;
  ShardedBQueue<int> q(8);

  std::vector<std::thread> producers;
  for (int p = 0; p < P; ++p) {
    producers.emplace_back([&q, p] {
      for (int i = 0; i < K; ++i) q.push(p * K + i);  // globally unique value
    });
  }

  std::vector<char> seen(TOTAL, 0);
  long received = 0;
  std::vector<int> buf;
  while (received < TOTAL) {
    q.pop_batch(buf);
    for (int v : buf) {
      ASSERT_GE(v, 0); ASSERT_LT(v, TOTAL);
      ASSERT_FALSE(seen[v]) << "duplicate value " << v;
      seen[v] = 1;
      ++received;
    }
  }
  for (auto& t : producers) t.join();
  EXPECT_EQ(received, TOTAL);
  EXPECT_TRUE(q.is_empty());
}
