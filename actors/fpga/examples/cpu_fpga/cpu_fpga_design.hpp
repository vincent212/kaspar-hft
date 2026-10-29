#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The FPGA half of the cpu_fpga example: the same PingActor and PongActor as
 * in fpga_to_fpga, each its own process, but their peers are CPU actors.
 * Shared by the software run (cpu_fpga.cpp) and the HLS top
 * (hls/cpu_fpga_top.cpp), which only plug in the FIFOs.
 *
 * Three links cross PCIe. On each, the CPU writes its window and the card
 * writes a lane in host memory:
 *
 *   link A  CpuPing   (client) -> PongActor (server)   CPU -> FPGA requests
 *   link B  CpuDriver (client) -> PingActor (server)   Start in, Done out
 *   link C  PingActor (client) -> CpuPong   (server)   FPGA -> CPU requests
 *
 *   PongProc  port 0: link A
 *   PingProc  port 0: link B     port 1: link C
 */

#include "fpga_actors.hpp"

namespace pingpong {
namespace cpu_fpga {

using PingProc = kfpga::Process<PingActor>;
using PongProc = kfpga::Process<PongActor>;

inline void wire()
{
  PingActor &ping = PingProc::member<PingActor>();
  ping.starter = kfpga::ActorRef(0, kCpuDriver);
  ping.pong = kfpga::ActorRef(1, kCpuPong);
}

} // namespace cpu_fpga
} // namespace pingpong
