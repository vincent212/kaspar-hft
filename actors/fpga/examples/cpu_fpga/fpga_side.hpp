#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The FPGA half of the CPU <-> FPGA example: two FPGA actors, their messages,
 * and the design that connects them.
 *
 *   FpgaPong    (id 2)  replies to each Ping; its state counts pings seen
 *   FpgaCaller  (id 3)  on Go, plays `rounds` round trips with the CPU actor
 *                       CpuPong (id 8): with send (mode 0) or with fast_send
 *                       (mode 1), or with FpgaPong on the chip with fast_send
 *                       (mode 2), then reports Done to CpuDriver (id 9)
 */

#include <functional>
#include <vector>

#include "actors_fpga/actor.hpp"

namespace fpga_side {

using kfpga::ActorId;
using kfpga::Envelope;
using kfpga::MsgId;

// Actor ids, shared by both sides.
constexpr ActorId kFpgaPong = 2;
constexpr ActorId kFpgaCaller = 3;
constexpr ActorId kCpuPong = 8;
constexpr ActorId kCpuDriver = 9;

struct Go
{
  static constexpr MsgId id = 300;
  uint32_t rounds = 0;
  uint32_t mode = 0;   // 0 = send to CpuPong, 1 = fast_send to CpuPong, 2 = fast_send to FpgaPong
  KFPGA_FIELDS(rounds, mode)
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
  uint32_t last = 0;
  uint32_t rounds = 0;
  KFPGA_FIELDS(last, rounds)
};

struct FpgaPong
{
  static constexpr ActorId kId = kFpgaPong;
  uint32_t pings = 0;

  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))

  template <class Ctx>
  void on_ping(const Ping &m, Ctx &ctx)
  {
    ++pings;
    Pong r;
    r.count = m.count + 1000 * pings;
    ctx.reply(r);   // to a CPU actor's send: into its mailbox; to its fast_send: returned
  }
};

struct FpgaCaller
{
  static constexpr ActorId kId = kFpgaCaller;
  static constexpr uint32_t kMaxRounds = 8;
  uint32_t rounds = 0;
  uint32_t replies = 0;

  KFPGA_HANDLERS(KFPGA_ON(Go, on_go) KFPGA_ON(Pong, on_pong))

  template <class Ctx>
  void on_go(const Go &m, Ctx &ctx)
  {
    rounds = m.rounds;
    replies = 0;
    if (m.mode == 0)
    {
      // FPGA -> CPU send: the reply comes back to this actor's mailbox (on_pong).
      Ping p;
      p.count = 1;
      ctx.send(kCpuPong, p);
      return;
    }
    // fast_send: this process waits for each reply. Mode 1 calls the CPU
    // actor through the host; mode 2 calls FpgaPong, another process on this
    // chip, over the FIFOs between the two processes.
    const ActorId callee = m.mode == 2 ? kFpgaPong : kCpuPong;
    Done d;
    for (uint32_t i = 0; i < kMaxRounds; ++i)
    {
      if (i >= rounds)
        break;
      Ping p;
      p.count = 1 + i;
      Pong r;
      if (ctx.fast_send(callee, p, r))
      {
        d.last = r.count;
        ++d.rounds;
      }
    }
    ctx.send(kCpuDriver, d);
  }

  template <class Ctx>
  void on_pong(const Pong &m, Ctx &ctx)
  {
    ++replies;
    if (replies < rounds)
    {
      Ping p;
      p.count = 1 + replies;
      ctx.send(kCpuPong, p);
      return;
    }
    Done d;
    d.last = m.count;
    d.rounds = replies;
    ctx.send(kCpuDriver, d);
  }
};

// ---- the design -------------------------------------------------------------------

constexpr int kNumActors = 2;                // endpoints: 0 FpgaPong, 1 FpgaCaller
constexpr int kEndpoints = kNumActors + 1;   // 2 = the host link

inline kfpga::DiscoveryTable discovery()
{
  kfpga::DiscoveryTable rt;
  for (int i = 0; i < kfpga::kMaxActors; ++i)
    rt.port[i] = kfpga::kNoRoute;
  rt.port[kfpga::kHost] = kfpga::kToHost;
  rt.port[kFpgaPong] = 0;
  rt.port[kFpgaCaller] = 1;
  rt.port[kCpuPong] = kfpga::kToHost;     // CPU actors are reached through the host
  rt.port[kCpuDriver] = kfpga::kToHost;
  return rt;
}

struct Design
{
  FpgaPong pong;
  FpgaCaller caller;

  hls::stream<Envelope> from_host, to_pcie;
  kfpga::Links<kEndpoints> links;

  kfpga::DiscoveryTable rt = discovery();
  kfpga::ActorPorts<kEndpoints> pong_st, caller_st;
  int out_last = 0;

  // One step function per process; each runs in its own thread on a SoftCard.
  std::vector<std::function<bool()>> steps()
  {
    return {
        [this] { return kfpga::host_in_step(from_host, links, rt); },
        [this] { return kfpga::actor_step<FpgaPong, kEndpoints, 0>(pong, links, rt, pong_st); },
        [this] { return kfpga::actor_step<FpgaCaller, kEndpoints, 1>(caller, links, rt, caller_st); },
        [this] { return kfpga::host_out_step(links, to_pcie, out_last); },
    };
  }
};

} // namespace fpga_side
