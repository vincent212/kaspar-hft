/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Depth-1 async ping-pong: exactly one message in flight. The consumer's mailbox
 * is empty between messages, so each round-trip pays two condvar wakeups. This is
 * the microsecond, wakeup-bound regime (contrast ping_pong_batch.cpp). Batching
 * cannot help here — there is never more than one message to batch.
 */

#include <iostream>
#include <chrono>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"

using namespace actors;
using namespace std;

static const long MAX = 1'000'000; // round-trips

static std::mutex g_mtx;
static std::condition_variable g_cv;
static bool g_done = false;

struct Ping : public Message_N<100> { int count; Ping(int c) : count(c) {} };
struct Pong : public Message_N<101> { int count; Pong(int c) : count(c) {} };

class PingActor : public Actor {
  Actor* pong_actor;
  long   recv = 0;
public:
  PingActor(Actor* pong) : pong_actor(pong) {
    strncpy(name, "PingActor", sizeof(name));
    MESSAGE_HANDLER(msg::Start, on_start);
    MESSAGE_HANDLER(Pong, on_pong);
  }
  void on_start(const msg::Start*) { pong_actor->send(new Ping(1), this); }
  void on_pong(const Pong*) {
    recv++;
    if (recv >= MAX) {
      { std::lock_guard<std::mutex> lk(g_mtx); g_done = true; }
      g_cv.notify_one();
      return;
    }
    pong_actor->send(new Ping(1), this);  // one in flight
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
  cout << "=== C++ depth-1 ping-pong: round-trips=" << MAX << " ===" << endl;
  Manager mgr;
  auto* pong = new PongActor();
  auto* ping = new PingActor(pong);
  mgr.add_to_manage_q(pong);
  mgr.add_to_manage_q(ping);

  auto t0 = chrono::steady_clock::now();
  mgr.init();
  { std::unique_lock<std::mutex> lk(g_mtx); g_cv.wait(lk, [] { return g_done; }); }
  double dt = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
  mgr.end();

  cout << MAX << " round-trips in " << dt << "s  ("
       << MAX / dt / 1e6 << " M rt/s, " << dt / MAX * 1e6 << " us/round-trip)" << endl;
  return 0;
}
