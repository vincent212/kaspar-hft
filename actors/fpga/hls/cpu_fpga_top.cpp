/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The FPGA half of the cpu_fpga example as hardware: PingActor and PongActor,
// each an hls::task. The card's PCIe side is one stream each way, as one DMA
// engine serves every window and lane:
//   windows  CPU -> card. On a card each link has its own window, an address
//            range; here the window is named by the sender, the CPU actor id.
//   lanes    card -> CPU. The lane is the envelope's destination, the CPU
//            actor id.
// The wiring is cpu_fpga_design.hpp, the same as the software run.

#include <hls_stream.h>
#include <hls_task.h>

#include "cpu_fpga_design.hpp"

using namespace pingpong;
using namespace pingpong::cpu_fpga;
using kfpga::Fifo;

static void ping_task(Fifo &b_in, Fifo &b_out, Fifo &c_in, Fifo &c_out)
{
  static PingProc proc;
  wire();
  kfpga::Port ports[2] = {{b_in, b_out}, {c_in, c_out}};
  proc.step(ports);
}

static void pong_task(Fifo &a_in, Fifo &a_out)
{
  static PongProc proc;
  kfpga::Port ports[1] = {{a_in, a_out}};
  proc.step(ports);
}

// The PCIe side: ordinary dataflow processes that move a given number of
// envelopes, around the actors' free-running tasks. The top then returns when
// the work is done, so the testbench never touches a top-level stream while a
// task is using it (co-simulation's recording of those streams is not safe
// against that and reorders them). A card's DMA engine runs without counts.

// CPU -> card: each window's envelopes onto its link.
static void dma_in(Fifo &windows, Fifo &a_in, Fifo &b_in, Fifo &c_in, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
  {
    const kfpga::Envelope e = windows.read();
    if (e.src == kCpuPing)
      a_in.write(e);
    else if (e.src == kCpuDriver)
      b_in.write(e);
    else if (e.src == kCpuPong)
      c_in.write(e);
  }
}

// card -> CPU: the links' envelopes into their lanes, as they come.
static void dma_out(Fifo &a_out, Fifo &b_out, Fifo &c_out, Fifo &lanes, uint32_t n)
{
  for (uint32_t i = 0; i < n;)
  {
    kfpga::Envelope e;
    if (a_out.read_nb(e) || b_out.read_nb(e) || c_out.read_nb(e))
    {
      lanes.write(e);
      ++i;
    }
  }
}

//   link A  CpuPing   -> PongActor
//   link B  CpuDriver -> PingActor
//   link C  PingActor -> CpuPong
// n_windows envelopes come in, n_lanes go out.
void cpu_fpga_top(Fifo &windows, Fifo &lanes, uint32_t n_windows, uint32_t n_lanes)
{
#pragma HLS INTERFACE mode=axis port=windows
#pragma HLS INTERFACE mode=axis port=lanes
#pragma HLS DATAFLOW
  hls_thread_local Fifo a_in("a_in"), a_out("a_out");
  hls_thread_local Fifo b_in("b_in"), b_out("b_out");
  hls_thread_local Fifo c_in("c_in"), c_out("c_out");
  hls_thread_local hls::task ping(ping_task, b_in, b_out, c_in, c_out);
  hls_thread_local hls::task pong(pong_task, a_in, a_out);
  dma_in(windows, a_in, b_in, c_in, n_windows);
  dma_out(a_out, b_out, c_out, lanes, n_lanes);
}
