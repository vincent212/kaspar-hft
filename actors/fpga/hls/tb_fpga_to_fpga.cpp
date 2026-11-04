/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for fpga_to_fpga_top: Start in, Done out. Used for C simulation
// and, unchanged, for RTL co-simulation.

#include <cstdio>
#include <hls_stream.h>

#include "messages.hpp"

using namespace pingpong;

void fpga_to_fpga_top(hls::stream<kfpga::Envelope> &in, hls::stream<kfpga::Envelope> &out, uint32_t n_in,
                      uint32_t n_out);

int main()
{
  constexpr uint32_t kRounds = 1000;
  hls::stream<kfpga::Envelope> in, out;
  Start s;
  s.rounds = kRounds;
  in.write(kfpga::encode(s, kTestbench, kPing));
  fpga_to_fpga_top(in, out, 1, 1);
  const kfpga::Envelope e = out.read();
  const Done d = kfpga::decode<Done>(e);
  std::printf("Done: %u round trips, last Pong(%u)\n", d.rounds, d.final_count);
  const bool ok = e.msg == Done::id && e.src == kPing && d.rounds == kRounds && d.final_count == kRounds;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
