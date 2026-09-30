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
 *   from_host -> host_in -> router_in[3]          (SEND)
 *                        -> fast_in[k]            (FAST, past the mailbox)
 *                        -> call_reply[k]         (FAST_REPLY to actor k's own call)
 *   router_in[0..3] -> router -> mbox[k] / to_host
 *   actor k: fast_in[k], mbox[k], call_reply[k] -> router_in[k], fast_reply[k]
 *   to_host, host_err, fast_reply[*] -> host_out -> to_pcie
 *
 * Not yet synthesized: whether Vitis HLS accepts free-running processes in this
 * DATAFLOW form, or needs hls::task, is to be settled on a machine with Vitis.
 */

#include "actors_fpga/actor.hpp"
#include "actors_fpga/router.hpp"
#include "pingpong.hpp"

using kfpga::Envelope;
using namespace pingpong;

template <class A>
void actor_proc(hls::stream<Envelope> &fast_in, hls::stream<Envelope> &mbox,
                hls::stream<Envelope> &call_reply, hls::stream<Envelope> &to_router,
                hls::stream<Envelope> &fast_reply)
{
  static A actor;   // the actor's state, kept in registers / on-chip RAM
  kfpga::actor_process(actor, fast_in, mbox, call_reply, to_router, fast_reply);
}

static void router_proc(hls::stream<Envelope> (&in)[kRouterInputs],
                        hls::stream<Envelope> (&mbox)[kNumActors], hls::stream<Envelope> &to_host)
{
  const kfpga::RouteTable rt = routes();
  int last = kRouterInputs - 1;
  for (;;)
    kfpga::router_step(in, mbox, to_host, rt, last);
}

static void host_in_proc(hls::stream<Envelope> &from_host, hls::stream<Envelope> &host_in,
                         hls::stream<Envelope> (&fast_in)[kNumActors],
                         hls::stream<Envelope> (&call_reply)[kNumActors],
                         hls::stream<Envelope> &host_err)
{
  const kfpga::RouteTable rt = routes();
  for (;;)
    kfpga::host_in_step(from_host, host_in, fast_in, call_reply, host_err, rt);
}

static void host_out_proc(hls::stream<Envelope> &to_host, hls::stream<Envelope> &host_err,
                          hls::stream<Envelope> (&fast_reply)[kNumActors],
                          hls::stream<Envelope> &to_pcie)
{
  int last = kNumActors + 1;
  for (;;)
    kfpga::host_out_step(to_host, host_err, fast_reply, to_pcie, last);
}

void pingpong_top(hls::stream<Envelope> &from_host, hls::stream<Envelope> &to_pcie)
{
#pragma HLS INTERFACE mode = axis port = from_host
#pragma HLS INTERFACE mode = axis port = to_pcie
#pragma HLS INTERFACE mode = ap_ctrl_none port = return
#pragma HLS DATAFLOW

  static hls::stream<Envelope> fast_in[kNumActors];
  static hls::stream<Envelope> mbox[kNumActors];
  static hls::stream<Envelope> call_reply[kNumActors];
  static hls::stream<Envelope> router_in[kRouterInputs];
  static hls::stream<Envelope> fast_reply[kNumActors];
  static hls::stream<Envelope> to_host;
  static hls::stream<Envelope> host_err;

  host_in_proc(from_host, router_in[3], fast_in, call_reply, host_err);
  router_proc(router_in, mbox, to_host);
  actor_proc<PingActor>(fast_in[0], mbox[0], call_reply[0], router_in[0], fast_reply[0]);
  actor_proc<PongActor<kPong>>(fast_in[1], mbox[1], call_reply[1], router_in[1], fast_reply[1]);
  actor_proc<FusedPing>(fast_in[2], mbox[2], call_reply[2], router_in[2], fast_reply[2]);
  host_out_proc(to_host, host_err, fast_reply, to_pcie);
}
