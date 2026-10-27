#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * Ping and Pong as FPGA actors.
 *
 *   PingActor  on Start, sends Ping to `pong` and waits for each Pong before
 *              the next; after `rounds` round trips, sends Done to `starter`
 *   PongActor  replies to each Ping; its state counts the pings it has seen
 *
 * Neither knows where its peers are. In fpga_to_fpga.cpp Pong is another
 * process on the chip; in cpu_fpga.cpp Ping's `pong` is a CPU actor. Only the
 * wiring in the top changes.
 */

#include "kfpga/actor.hpp"
#include "messages.hpp"

namespace pingpong {

struct PingActor : kfpga::Actor<PingActor>
{
  static constexpr ActorId kId = kPing;

  kfpga::ActorRef pong;      // bound in the top
  kfpga::ActorRef starter;   // gets Done
  uint32_t rounds = 0;
  uint32_t replies = 0;

  KFPGA_HANDLERS(KFPGA_ON(Start, on_start) KFPGA_ON(Pong, on_pong))

  void on_start(const Start &m)
  {
    rounds = m.rounds;
    replies = 0;
    Ping p;
    p.count = 1;
    pong.send(p, this);
  }

  void on_pong(const Pong &m)
  {
    ++replies;
    if (replies < rounds)
    {
      Ping p;
      p.count = replies + 1;
      pong.send(p, this);
      return;
    }
    Done d;
    d.final_count = m.count;
    d.rounds = replies;
    starter.send(d, this);
  }
};

struct PongActor : kfpga::Actor<PongActor>
{
  static constexpr ActorId kId = kPong;

  uint32_t pings = 0;

  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))

  void on_ping(const Ping &m)
  {
    ++pings;
    Pong r;
    r.count = m.count;
    reply(r);
  }
};

} // namespace pingpong
