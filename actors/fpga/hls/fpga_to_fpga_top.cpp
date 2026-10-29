/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The fpga_to_fpga design as hardware: each process an hls::task (its own
// circuit, always running), each link two hls::streams. The wiring is
// fpga_to_fpga_design.hpp, the same as the software run.

#include <hls_stream.h>
#include <hls_task.h>

#include "fpga_to_fpga_design.hpp"

using namespace pingpong;
using namespace pingpong::f2f;
using kfpga::Fifo;

static void ping_task(Fifo &tb_in, Fifo &tb_out, Fifo &pp_in, Fifo &pp_out)
{
#pragma HLS PIPELINE II=1
  static PingProc proc;
  wire();
  kfpga::Port ports[2] = {{tb_in, tb_out}, {pp_in, pp_out}};
  proc.step(ports);
}

static void pong_task(Fifo &pp_in, Fifo &pp_out)
{
#pragma HLS PIPELINE II=1
  static PongProc proc;
  kfpga::Port ports[1] = {{pp_in, pp_out}};
  proc.step(ports);
}

// The boundary: ordinary dataflow processes that move a given number of
// envelopes between the top-level ports and the design, around the actors'
// free-running tasks. The top returns when the work is done, so the testbench
// never touches a top-level stream while a task is using it; co-simulation
// cannot drive the processes' non-blocking reads on a top-level port
// (COSIM 212-382) and its recording reorders envelopes under that race.
static void copy(Fifo &from, Fifo &to, uint32_t n)
{
  for (uint32_t i = 0; i < n; ++i)
    to.write(from.read());
}

// `in` and `out` are the testbench link: n_in envelopes in, n_out out.
void fpga_to_fpga_top(Fifo &in, Fifo &out, uint32_t n_in, uint32_t n_out)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS DATAFLOW
  hls_thread_local Fifo tb_fwd("tb_fwd");     // testbench -> Ping
  hls_thread_local Fifo tb_back("tb_back");   // Ping -> testbench
  hls_thread_local Fifo pp_fwd("pp_fwd");     // Ping -> Pong
  hls_thread_local Fifo pp_back("pp_back");   // Pong -> Ping
  hls_thread_local hls::task ping(ping_task, tb_fwd, tb_back, pp_back, pp_fwd);
  hls_thread_local hls::task pong(pong_task, pp_fwd, pp_back);
  copy(in, tb_fwd, n_in);
  copy(tb_back, out, n_out);
}
