/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The group design as hardware: PingActor, PongActor and SyncPing in one
// hls::task. The wiring is group_design.hpp, the same as the software run.

#include <hls_stream.h>
#include <hls_task.h>

#include "group_design.hpp"

using namespace pingpong;
using namespace pingpong::grp;
using kfpga::Fifo;

// Not pipelined: SyncPing's Start handler is a loop of up to 1024 fast_sends,
// and PIPELINE would unroll it into 1024 copies of Pong's handler. A process
// is pipelined only when its handlers are loop-free (or have small fixed
// loops); see HLS.md.
static void group_task(Fifo &tb_in, Fifo &tb_out)
{
  static Grp grp;
  wire();
  kfpga::Port ports[1] = {{tb_in, tb_out}};
  grp.step(ports);
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
void group_top(Fifo &in, Fifo &out, uint32_t n_in, uint32_t n_out)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
#pragma HLS DATAFLOW
  hls_thread_local Fifo tb_fwd("tb_fwd");     // testbench -> the group
  hls_thread_local Fifo tb_back("tb_back");   // the group -> testbench
  hls_thread_local hls::task grp(group_task, tb_fwd, tb_back);
  copy(in, tb_fwd, n_in);
  copy(tb_back, out, n_out);
}
