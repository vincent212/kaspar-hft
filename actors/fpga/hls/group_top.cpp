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

static void group_task(Fifo &tb_in, Fifo &tb_out)
{
  static Grp grp;
  wire();
  kfpga::Port ports[1] = {{tb_in, tb_out}};
  grp.step(ports);
}

// The top-level ports are read and written only by these relays, with
// blocking accesses. The processes poll their FIFOs with non-blocking reads,
// which RTL co-simulation cannot drive on a top-level port of a free-running
// design (COSIM 212-382).
static void relay(Fifo &from, Fifo &to)
{
  to.write(from.read());
}

// `in` and `out` are the testbench link.
void group_top(Fifo &in, Fifo &out)
{
#pragma HLS INTERFACE mode=axis port=in
#pragma HLS INTERFACE mode=axis port=out
  hls_thread_local Fifo tb_fwd("tb_fwd");     // testbench -> the group
  hls_thread_local Fifo tb_back("tb_back");   // the group -> testbench
  hls_thread_local hls::task relay_in(relay, in, tb_fwd);
  hls_thread_local hls::task relay_out(relay, tb_back, out);
  hls_thread_local hls::task grp(group_task, tb_fwd, tb_back);
}
