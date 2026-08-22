/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Batched async ping-pong benchmark, with optional producer-side batching and a
 * message pool (compile-time flags), to measure what beats the 1.6x that
 * consumer-side batching (Lever 1+2) alone gives.
 *
 *   -DUSE_SEND_BATCH : fire a burst with one send_batch() (one lock) vs N send()
 *   -DUSE_POOL       : recycle Ping/Pong through a free-list vs new/delete
 */

#include <iostream>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>
#include <condition_variable>
#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"
#ifdef USE_POOL
#include "actors/MemoryPool.hpp"   // framework's thread-local-cache + slab pool
#endif

using namespace actors;
using namespace std;

static const int BATCH = 10000;  // messages per burst
static const int ROUNDS = 1000;  // number of bursts => BATCH*ROUNDS round-trips

static std::mutex g_mtx;
static std::condition_variable g_cv;
static bool g_done = false;

// Use the framework's MemoryPool (inherited => pooled operator new/delete).
// Message has a virtual dtor, so `delete (Message*)p` dispatches to the derived
// pool's sized operator delete.
#ifdef USE_POOL
struct Ping : public Message_N<100>, public MemoryPool<Ping> { int count; Ping(int c) : count(c) {} };
struct Pong : public Message_N<101>, public MemoryPool<Pong> { int count; Pong(int c) : count(c) {} };
#else
struct Ping : public Message_N<100> { int count; Ping(int c) : count(c) {} };
struct Pong : public Message_N<101> { int count; Pong(int c) : count(c) {} };
#endif

class PingActor : public Actor {
  Actor* pong_actor;
  Actor* manager;
  long recv = 0;
  int  bursts_sent = 0;
public:
  PingActor(Actor* pong, Actor* mgr) : pong_actor(pong), manager(mgr) {
    strncpy(name, "PingActor", sizeof(name));
    MESSAGE_HANDLER(msg::Start, on_start);
    MESSAGE_HANDLER(Pong, on_pong);
  }
  void fire() {
    for (int i = 0; i < BATCH; i++) pong_actor->send(new Ping(1), this);
    bursts_sent++;
  }
  void on_start(const msg::Start*) { fire(); }
  void on_pong(const Pong*) {
    recv++;
    if (recv >= (long)BATCH * ROUNDS) {
      { std::lock_guard<std::mutex> lk(g_mtx); g_done = true; }
      g_cv.notify_one();
      return;
    }
    if (recv % BATCH == 0 && bursts_sent < ROUNDS) fire();
  }
};

class PongActor : public Actor {
public:
  PongActor() {
    strncpy(name, "PongActor", sizeof(name));
    MESSAGE_HANDLER(Ping, on_ping);
  }
  void on_ping(const Ping* m) { reply(new Pong(m->count)); }
};

int main() {
  const long total = (long)BATCH * ROUNDS;
  cout << "=== C++ batched ping-pong"
#ifdef USE_SEND_BATCH
       << " +send_batch"
#endif
#ifdef USE_POOL
       << " +pool"
#endif
       << ": burst=" << BATCH << " round-trips=" << total << " ===" << endl;

  Manager mgr;
  auto* pong = new PongActor();
  auto* ping = new PingActor(pong, &mgr);
  mgr.add_to_manage_q(pong);
  mgr.add_to_manage_q(ping);

  auto t0 = chrono::steady_clock::now();
  mgr.init();
  { std::unique_lock<std::mutex> lk(g_mtx); g_cv.wait(lk, [] { return g_done; }); }
  double dt = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
  mgr.end();

  cout << total << " round-trips in " << dt << "s  ("
       << total / dt / 1e6 << " M rt/s, " << 2.0 * total / dt / 1e6 << " M msgs/s, "
       << dt / total * 1e9 << " ns/rt)" << endl;
  return 0;
}
