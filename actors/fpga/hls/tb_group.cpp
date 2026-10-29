/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for group_top: Start to PingActor and to SyncPing, two Dones out.
// Used for C simulation and, unchanged, for RTL co-simulation.

#include <cstdio>
#include <hls_stream.h>

#include "messages.hpp"

using namespace pingpong;

void group_top(hls::stream<kfpga::Envelope> &in, hls::stream<kfpga::Envelope> &out, uint32_t n_in,
               uint32_t n_out);

int main()
{
  constexpr uint32_t kRounds = 1000;
  hls::stream<kfpga::Envelope> in, out;
  Start s;
  s.rounds = kRounds;
  in.write(kfpga::encode(s, kTestbench, kPing));
  in.write(kfpga::encode(s, kTestbench, kSyncPing));
  group_top(in, out, 2, 2);
  bool ok = true;
  bool seen_ping = false, seen_sync = false;
  for (int k = 0; k < 2; ++k)
  {
    const kfpga::Envelope e = out.read();
    const Done d = kfpga::decode<Done>(e);
    std::printf("%-9s Done: %u round trips, last Pong(%u)\n", e.src == kPing ? "PingActor" : "SyncPing",
                d.rounds, d.final_count);
    ok &= e.msg == Done::id && d.rounds == kRounds && d.final_count == kRounds;
    seen_ping |= e.src == kPing;
    seen_sync |= e.src == kSyncPing;
  }
  ok &= seen_ping && seen_sync;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
