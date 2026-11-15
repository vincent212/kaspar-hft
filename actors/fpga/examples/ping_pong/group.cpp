/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// A group of FPGA actors as one process: PingActor, PongActor and SyncPing.
//
//   PingActor -> PongActor  send, through the group's own queue: each Ping is
//                           handled on a later step, as in a CPU Group
//   SyncPing  -> PongActor  fast_send: an inline call, no FIFO at all
//
// PingActor is the same code as in fpga_to_fpga.cpp, where Pong is another
// process: only the wiring below differs. A testbench sends Start to Ping and
// to SyncPing on one link and collects the two Dones. The wiring is in
// group_design.hpp, shared with the HLS top.

#include <cstdio>

#include "group_design.hpp"
#include "kfpga/soft_card.hpp"

using namespace pingpong;
using namespace pingpong::grp;

struct Design
{
  kfpga::Link tb;   // testbench <-> the group
  Grp grp;
  kfpga::Port ports[1] = {tb.server()};
#ifdef PONG_IN_OTHER_GROUP
  PongGrp pong_grp;
#endif

  Design() { wire(); }
};

static bool run(const char *mode, bool threaded, uint32_t rounds)
{
  Design d;
  kfpga::SoftCard card;
  card.add(d.grp, d.ports);
  Start s;
  s.rounds = rounds;
  if (threaded)
    card.start();
  d.tb.fwd.write(kfpga::encode(s, kTestbench, kPing));
  d.tb.fwd.write(kfpga::encode(s, kTestbench, kSyncPing));
  if (!threaded)
    card.run();

  bool ok = true;
  for (int k = 0; k < 2; ++k)
  {
    const kfpga::Envelope e = d.tb.back.read();
    const Done done = kfpga::decode<Done>(e);
    std::printf("%-8s %-9s Done: %u round trips, last Pong(%u)\n", mode,
                e.src == kPing ? "PingActor" : "SyncPing", done.rounds, done.final_count);
    ok &= e.msg == Done::id && done.rounds == rounds && done.final_count == rounds;
  }
  if (threaded)
    card.stop();
  const uint32_t pings = kfpga::instance<PongActor>().pings;
  std::printf("%-8s Pong saw %u pings\n", mode, pings);
  return ok && pings == 2 * rounds && d.grp.unhandled() == 0;
}

int main()
{
  constexpr uint32_t kRounds = 1000;
  bool ok = run("stepped", false, kRounds);
  ok &= run("threads", true, kRounds);
  std::printf("%s\n", ok ? "OK" : "FAILED");
  return ok ? 0 : 1;
}
