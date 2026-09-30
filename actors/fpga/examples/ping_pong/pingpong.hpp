#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * Ping-pong on the FPGA runtime.
 *
 *   PingActor  (id 1)  on Start from the host, sends Ping to Pong `rounds` times,
 *                      one at a time through the router, then sends Done to the host
 *   PongActor  (id 2)  replies to each Ping; its state counts the pings it has seen
 *   FusedPing  (id 3)  owns its own Pong (id 4) in the same process and calls it with
 *                      fast_send: no router, no FIFO, the reply comes back by value
 *
 * The host can also fast_send a Ping straight to Pong (id 2): it goes to Pong's
 * fast_send port and is served before anything waiting in Pong's mailbox.
 */

#include "actors_fpga/actor.hpp"

namespace pingpong {

using kfpga::ActorId;
using kfpga::Ctx;
using kfpga::MsgId;

struct Start
{
  static constexpr MsgId id = 300;
  uint32_t rounds = 0;
  KFPGA_FIELDS(rounds)
};

struct Ping
{
  static constexpr MsgId id = 301;
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};

struct Pong
{
  static constexpr MsgId id = 302;
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};

struct Done
{
  static constexpr MsgId id = 303;
  uint32_t last = 0;     // the last Pong's count
  uint32_t rounds = 0;   // round trips completed
  KFPGA_FIELDS(last, rounds)
};

constexpr ActorId kPing = 1;
constexpr ActorId kPong = 2;
constexpr ActorId kFusedPing = 3;
constexpr ActorId kFusedPong = 4;
constexpr uint32_t kMaxRounds = 8;   // loop bound for FusedPing

template <ActorId Id>
struct PongActor
{
  static constexpr ActorId kId = Id;
  uint32_t pings = 0;

  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))

  void on_ping(const Ping &m, Ctx &ctx)
  {
    ++pings;
    Pong r;
    r.count = m.count + 1000 * pings;
    ctx.reply(r);
  }
};

struct PingActor
{
  static constexpr ActorId kId = kPing;
  uint32_t rounds = 0;
  uint32_t replies = 0;

  KFPGA_HANDLERS(KFPGA_ON(Start, on_start) KFPGA_ON(Pong, on_pong))

  void on_start(const Start &m, Ctx &ctx)
  {
    rounds = m.rounds;
    replies = 0;
    Ping p;
    p.count = 1;
    ctx.send(kPong, p);
  }

  void on_pong(const Pong &m, Ctx &ctx)
  {
    ++replies;
    if (replies < rounds)
    {
      Ping p;
      p.count = 1 + replies;
      ctx.send(kPong, p);
    }
    else
    {
      Done d;
      d.last = m.count;
      d.rounds = replies;
      ctx.send(kfpga::kHost, d);
    }
  }
};

struct FusedPing
{
  static constexpr ActorId kId = kFusedPing;
  PongActor<kFusedPong> pong;   // lives inside this actor's process

  KFPGA_HANDLERS(KFPGA_ON(Start, on_start))

  void on_start(const Start &m, Ctx &ctx)
  {
    Done d;
    for (uint32_t i = 0; i < kMaxRounds; ++i)
    {
      if (i >= m.rounds)
        break;
      Ping p;
      p.count = 1 + i;
      Pong r;
      if (ctx.fast_send(pong, p, r))   // runs pong.on_ping now; reply by value
      {
        d.last = r.count;
        ++d.rounds;
      }
    }
    ctx.send(kfpga::kHost, d);
  }
};

// ---- the design: three processes, a router and a host link --------------------

constexpr int kNumActors = 3;       // mailbox ports: 0 Ping, 1 Pong, 2 FusedPing
constexpr int kRouterInputs = 4;    // 0..2 actors' outputs, 3 host_in

inline kfpga::RouteTable routes()
{
  kfpga::RouteTable rt;
  for (int i = 0; i < kfpga::kMaxActors; ++i)
    rt.port[i] = kfpga::kNoRoute;
  rt.port[kfpga::kHost] = kfpga::kToHost;
  rt.port[kPing] = 0;
  rt.port[kPong] = 1;
  rt.port[kFusedPing] = 2;
  return rt;
}

} // namespace pingpong
