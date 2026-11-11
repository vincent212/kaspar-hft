/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Manager::unmanage: stop one actor, join its thread, delete it.
 *
 * Two bugs it had: it joined the first joinable thread in the Manager's list,
 * whichever actor that belonged to (so unmanaging any actor but the first
 * waited on a thread that never exits), and it called get_name() on the actor
 * after deleting it. These tests pin the fixed behaviour. The ones that would
 * hang on the old code run in a child process with an alarm.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"

namespace
{
  std::atomic<int> g_destroyed{0};

  class Worker : public actors::Actor
  {
  public:
    explicit Worker(const char *n) { std::strncpy(name, n, sizeof(name) - 1); }
    ~Worker() override { ++g_destroyed; }
    const char *get_name() const override { return name; }
  };

  template <class P>
  bool wait_until(P p)
  {
    for (int i = 0; i < 1000 && !p(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return p();
  }
}

// The actor is shut down, deleted, and forgotten by name.
TEST(ManagerUnmanage, StopsDeletesAndForgetsTheActor)
{
  g_destroyed = 0;
  actors::Manager mgr("m");
  auto *w = new Worker("solo");
  mgr.manage_and_start(w);
  ASSERT_EQ(mgr.get_local_actor("solo"), w);

  mgr.unmanage(w);
  EXPECT_EQ(g_destroyed, 1);
  EXPECT_EQ(mgr.get_local_actor("solo"), nullptr);
  EXPECT_TRUE(mgr.get_managed_actors().empty());

  // The name is free again.
  auto *again = new Worker("solo");
  mgr.manage_and_start(again);
  EXPECT_EQ(mgr.get_local_actor("solo"), again);
  mgr.unmanage(again);
  EXPECT_EQ(g_destroyed, 2);
}

// Unmanaging the second actor must join the second actor's thread, not the
// first one's (which keeps running and would never be joined).
TEST(ManagerUnmanage, JoinsTheRightThread)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(
      {
        ::alarm(20);
        actors::Manager mgr("m");
        auto *a = new Worker("first");
        auto *b = new Worker("second");
        mgr.manage_and_start(a);
        mgr.manage_and_start(b);
        mgr.unmanage(b); // the old code joined a's thread here and hung
        if (mgr.get_local_actor("first") != a || mgr.get_local_actor("second") != nullptr)
          std::_Exit(2);
        mgr.unmanage(a);
        std::_Exit(g_destroyed == 2 ? 0 : 3);
      },
      ::testing::ExitedWithCode(0), "");
}

// Out-of-order unmanage of several actors, then the Manager's own teardown
// with one left: nothing joins or deletes twice.
namespace
{
  int many_actors_any_order()
  {
    {
      actors::Manager mgr("m");
      Worker *w0 = new Worker("w0"), *w1 = new Worker("w1"), *w2 = new Worker("w2"), *w3 = new Worker("w3");
      for (auto *x : {w0, w1, w2, w3})
        mgr.manage_and_start(x);
      mgr.unmanage(w2);
      mgr.unmanage(w0);
      mgr.unmanage(w3);
      if (mgr.get_managed_actors().size() != 1)
        return 2;
      mgr.end(); // stops w1 and joins its thread
      delete w1;
    }
    return g_destroyed == 4 ? 0 : 3;
  }
}

TEST(ManagerUnmanage, ManyActorsAnyOrder)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(
      {
        ::alarm(20);
        std::_Exit(many_actors_any_order());
      },
      ::testing::ExitedWithCode(0), "");
}
