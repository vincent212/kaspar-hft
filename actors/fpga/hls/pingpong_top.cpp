/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The ping-pong design as hardware: every process runs on its own, forever,
 * connected by FIFOs. from_host / to_pcie are AXI-Stream ports; how they reach
 * PCIe depends on the card's shell and is not part of this file.
 *
 *   from_host -> host_in -> the FIFO from the host to each actor
 *   actor s -> the FIFO from s to actor r -> actor r      (messages, fast_send
 *                                                          requests, replies)
 *   each actor's FIFO to the host -> host_out -> to_pcie
 *
 * Not yet synthesized: whether Vitis HLS accepts free-running processes in this
 * DATAFLOW form, or needs hls::task, is to be settled on a machine with Vitis.
 */

#include "actors_fpga/actor.hpp"
#include "actors_fpga/links.hpp"
#include "pingpong.hpp"

using kfpga::Envelope;
using namespace pingpong;

using L4 = kfpga::Links<kEndpoints>;

template <class A, int K>
void actor_proc(L4 &L)
{
  static A actor;   // the actor's state, kept in registers / on-chip RAM
  const kfpga::DiscoveryTable rt = discovery();
  kfpga::actor_process<A, kEndpoints, K>(actor, L, rt);
}

static void host_in_proc(hls::stream<Envelope> &from_host, L4 &L)
{
  const kfpga::DiscoveryTable rt = discovery();
  for (;;)
    kfpga::host_in_step(from_host, L, rt);
}

static void host_out_proc(L4 &L, hls::stream<Envelope> &to_pcie)
{
  int last = 0;
  for (;;)
    kfpga::host_out_step(L, to_pcie, last);
}

void pingpong_top(hls::stream<Envelope> &from_host, hls::stream<Envelope> &to_pcie)
{
#pragma HLS INTERFACE mode = axis port = from_host
#pragma HLS INTERFACE mode = axis port = to_pcie
#pragma HLS INTERFACE mode = ap_ctrl_none port = return
#pragma HLS DATAFLOW

  static L4 links;

  host_in_proc(from_host, links);
  actor_proc<PingActor, 0>(links);
  actor_proc<PongActor<kPong>, 1>(links);
  actor_proc<FusedPing, 2>(links);
  host_out_proc(links, to_pcie);
}
