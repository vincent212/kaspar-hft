#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The fpga_to_fpga design: Ping and Pong, each its own process, joined by a
 * link (two FIFOs). Shared by the software run (fpga_to_fpga.cpp) and the
 * HLS top (hls/fpga_to_fpga_top.cpp), which only plug in the FIFOs.
 *
 *   testbench --tb fwd-->  Ping  --pp fwd-->  Pong
 *   testbench <--tb back-- Ping  <--pp back-- Pong
 *
 *   PingProc  port 0: the testbench link     port 1: the link to Pong
 *   PongProc  port 0: the link from Ping
 */

#include "fpga_actors.hpp"

namespace pingpong {
namespace f2f {

using PingProc = kfpga::Process<PingActor>;
using PongProc = kfpga::Process<PongActor>;

inline void wire()
{
  PingActor &ping = PingProc::member<PingActor>();
  ping.starter = kfpga::ActorRef(0, kTestbench);
  ping.pong = kfpga::ActorRef(1, kPong);
}

} // namespace f2f
} // namespace pingpong
