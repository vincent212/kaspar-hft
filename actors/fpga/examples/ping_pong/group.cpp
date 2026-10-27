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
// to SyncPing on one link and collects the two Dones.
//
// Build with -DPONG_IN_OTHER_GROUP to see the build-time check: Pong moves to
// a group of its own, and binding SyncPing's fast_send reference to it fails
// to compile (make check_group_fast_send).

#include <cstdio>

#include "fpga_actors.hpp"
#include "kfpga/soft_card.hpp"

using namespace pingpong;

struct Design
{
  kfpga::Link tb;   // testbench <-> the group
  PingActor ping;
  PongActor pong;
  SyncPing sync;
#ifndef PONG_IN_OTHER_GROUP
  kfpga::Group<1, PingActor, PongActor, SyncPing> grp{{tb.server_input()}, ping, pong, sync};
#else
  kfpga::Group<1, PingActor, SyncPing> grp{{tb.server_input()}, ping, sync};
  kfpga::Link pp;
  kfpga::Group<1, PongActor> pong_grp{{pp.server_input()}, pong};
#endif

  Design()
  {
    ping.pong = grp.ref<PongActor>();
    ping.starter = tb.back_ref(kTestbench);
    sync.starter = tb.back_ref(kTestbench);
    grp.bind(sync.pong);
  }
};

static bool run(const char *mode, bool threaded, uint32_t rounds)
{
  Design d;
  kfpga::SoftCard card;
  card.add(d.grp);
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
  std::printf("%-8s Pong saw %u pings\n", mode, d.pong.pings);
  return ok && d.pong.pings == 2 * rounds && d.grp.unhandled() == 0;
}

int main()
{
  constexpr uint32_t kRounds = 1000;
  bool ok = run("stepped", false, kRounds);
  ok &= run("threads", true, kRounds);
  std::printf("%s\n", ok ? "OK" : "FAILED");
  return ok ? 0 : 1;
}
