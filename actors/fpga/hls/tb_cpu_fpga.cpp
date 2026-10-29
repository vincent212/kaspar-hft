/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for cpu_fpga_top. It stands in for the three CPU actors, sending
// what they send and checking what they receive, envelope by envelope:
//   CpuPing    Ping(1..1000) on link A; expects Pong(1..1000)
//   CpuDriver  Start(1000) on link B; expects Done
//   CpuPong    Pong(1..1000) on link C; expects Ping(1..1000)
//
// Every CPU message is queued on `windows` before the top runs, and the
// outputs are read after, from `lanes`, sorted by destination id (the CPU
// actor each is for): RTL co-simulation replays the recorded inputs and re-runs this check
// against the recorded outputs, so the testbench cannot react to the card.
// (The software run, cpu_fpga.cpp, has real CPU actors that do react.) Each
// link keeps its order, and PingActor reads Start (port 0) before any Pong
// (port 1), so the queued Pongs answer its Pings in order.

#include <cstdio>
#include <hls_stream.h>

#include "messages.hpp"

using namespace pingpong;
using kfpga::Envelope;

void cpu_fpga_top(hls::stream<Envelope> &windows, hls::stream<Envelope> &lanes, uint32_t n_windows,
                  uint32_t n_lanes);

int main()
{
  constexpr uint32_t kRounds = 1000;
  hls::stream<Envelope> windows, lanes;

  Start s;
  s.rounds = kRounds;
  windows.write(kfpga::encode(s, kCpuDriver, kPing));
  for (uint32_t n = 1; n <= kRounds; ++n)
  {
    Ping p;
    p.count = n;
    windows.write(kfpga::encode(p, kCpuPing, kPong));
    Pong r;
    r.count = n;
    windows.write(kfpga::encode(r, kCpuPong, kPing));
  }

  cpu_fpga_top(windows, lanes, 2 * kRounds + 1, 2 * kRounds + 1);

  // 1000 Pongs for CpuPing, 1000 Pings for CpuPong, one Done for CpuDriver.
  bool ok = true;
  uint32_t next_pong = 1, next_ping = 1, a_rounds = 0, c_pings = 0;
  Done d;
  bool done = false;
  for (uint32_t i = 0; i < 2 * kRounds + 1; ++i)
  {
    const Envelope e = lanes.read();
    if (e.dst == kCpuPing)   // PongActor's answer to CpuPing
    {
      const bool good = e.msg == Pong::id && e.src == kPong && kfpga::decode<Pong>(e).count == next_pong++;
      a_rounds += good;
      ok &= good;
    }
    else if (e.dst == kCpuPong)   // PingActor's Ping to CpuPong
    {
      const bool good = e.msg == Ping::id && e.src == kPing && kfpga::decode<Ping>(e).count == next_ping++;
      c_pings += good;
      ok &= good;
    }
    else if (e.dst == kCpuDriver && e.msg == Done::id && e.src == kPing && !done)
    {
      d = kfpga::decode<Done>(e);
      done = true;
    }
    else
      ok = false;
  }
  ok &= done && d.rounds == kRounds && d.final_count == kRounds;

  std::printf("CPU -> FPGA  CpuPing:  %u of %u Pongs right from FPGA PongActor\n", a_rounds, kRounds);
  std::printf("FPGA -> CPU  PingActor: Done after %u round trips with CPU CpuPong, last Pong(%u); "
              "%u of %u Pings right\n",
              d.rounds, d.final_count, c_pings, kRounds);
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
