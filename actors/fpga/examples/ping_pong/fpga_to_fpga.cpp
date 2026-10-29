/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Ping-pong between two FPGA actors, each its own process, joined by a link
// (two FIFOs). A testbench stands in for the host: it sends Start to Ping and
// waits for Done.
//
//   testbench --tb.fwd-->  Ping  --pp.fwd-->  Pong
//   testbench <--tb.back-- Ping  <--pp.back-- Pong
//
// The design runs twice: stepped on one thread (deterministic), then with one
// thread per process. The wiring is in fpga_to_fpga_design.hpp, shared with
// the HLS top.

#include <cstdio>

#include "fpga_to_fpga_design.hpp"
#include "kfpga/soft_card.hpp"

using namespace pingpong;
using namespace pingpong::f2f;

struct Design
{
  kfpga::Link tb;   // testbench <-> Ping
  kfpga::Link pp;   // Ping <-> Pong
  PingProc ping_proc;
  PongProc pong_proc;
  kfpga::Port ping_ports[2] = {tb.server(), pp.client()};
  kfpga::Port pong_ports[1] = {pp.server()};

  Design() { wire(); }
  uint32_t pings() const { return PongProc::member<PongActor>().pings; }
};

static bool check(const char *mode, const Design &d, const Done &done, uint32_t rounds)
{
  std::printf("%-8s Done: %u round trips, last Pong(%u); Pong saw %u pings\n", mode, done.rounds,
              done.final_count, d.pings());
  return done.rounds == rounds && done.final_count == rounds && d.pings() == rounds &&
         d.ping_proc.unhandled() == 0 && d.pong_proc.unhandled() == 0;
}

int main()
{
  constexpr uint32_t kRounds = 1000;
  Start start;
  start.rounds = kRounds;
  bool ok = true;

  {
    Design d;
    kfpga::SoftCard card;
    card.add(d.ping_proc, d.ping_ports);
    card.add(d.pong_proc, d.pong_ports);
    d.tb.fwd.write(kfpga::encode(start, kTestbench, kPing));
    card.run();
    kfpga::Envelope e;
    ok &= d.tb.back.read_nb(e) && e.msg == Done::id &&
          check("stepped", d, kfpga::decode<Done>(e), kRounds);
  }

  {
    Design d;
    kfpga::SoftCard card;
    card.add(d.ping_proc, d.ping_ports);
    card.add(d.pong_proc, d.pong_ports);
    card.start();
    d.tb.fwd.write(kfpga::encode(start, kTestbench, kPing));
    const kfpga::Envelope e = d.tb.back.read();
    card.stop();
    ok &= e.msg == Done::id && check("threads", d, kfpga::decode<Done>(e), kRounds);
  }

  std::printf("%s\n", ok ? "OK" : "FAILED");
  return ok ? 0 : 1;
}
