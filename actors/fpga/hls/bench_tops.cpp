/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * One top-level function per runtime part, each doing exactly one step. Vitis HLS
 * synthesis (run_hls.tcl) reports each one's latency in clock cycles; those are
 * the numbers the send / fast_send cost model is built from (see README.md,
 * "Measuring").
 *
 *   bench_actor_step   Pong takes one message from its mailbox and replies
 *   bench_router_step  the router moves one message to a mailbox
 *   bench_host_in      the host link takes one message from PCIe
 *   bench_host_out     the host link puts one message out to PCIe
 *   bench_fast_send    an actor fast_sends to an actor in its own process
 */

#include "actors_fpga/actor.hpp"
#include "actors_fpga/router.hpp"
#include "pingpong.hpp"

using kfpga::Envelope;
using namespace pingpong;

void bench_actor_step(hls::stream<Envelope> &fast_in, hls::stream<Envelope> &mbox,
                      hls::stream<Envelope> &call_reply, hls::stream<Envelope> &to_router,
                      hls::stream<Envelope> &fast_reply)
{
#pragma HLS INTERFACE mode = axis port = fast_in
#pragma HLS INTERFACE mode = axis port = mbox
#pragma HLS INTERFACE mode = axis port = call_reply
#pragma HLS INTERFACE mode = axis port = to_router
#pragma HLS INTERFACE mode = axis port = fast_reply
  static PongActor<kPong> pong;
  kfpga::actor_step(pong, fast_in, mbox, call_reply, to_router, fast_reply);
}

void bench_router_step(hls::stream<Envelope> (&in)[kRouterInputs],
                       hls::stream<Envelope> (&mbox)[kNumActors], hls::stream<Envelope> &to_host)
{
#pragma HLS INTERFACE mode = axis port = in
#pragma HLS INTERFACE mode = axis port = mbox
#pragma HLS INTERFACE mode = axis port = to_host
  static int last = kRouterInputs - 1;
  const kfpga::RouteTable rt = routes();
  kfpga::router_step(in, mbox, to_host, rt, last);
}

void bench_host_in(hls::stream<Envelope> &from_host, hls::stream<Envelope> &host_in,
                   hls::stream<Envelope> (&fast_in)[kNumActors],
                   hls::stream<Envelope> (&call_reply)[kNumActors], hls::stream<Envelope> &host_err)
{
#pragma HLS INTERFACE mode = axis port = from_host
#pragma HLS INTERFACE mode = axis port = host_in
#pragma HLS INTERFACE mode = axis port = fast_in
#pragma HLS INTERFACE mode = axis port = call_reply
#pragma HLS INTERFACE mode = axis port = host_err
  const kfpga::RouteTable rt = routes();
  kfpga::host_in_step(from_host, host_in, fast_in, call_reply, host_err, rt);
}

void bench_host_out(hls::stream<Envelope> &to_host, hls::stream<Envelope> &host_err,
                    hls::stream<Envelope> (&fast_reply)[kNumActors], hls::stream<Envelope> &to_pcie)
{
#pragma HLS INTERFACE mode = axis port = to_host
#pragma HLS INTERFACE mode = axis port = host_err
#pragma HLS INTERFACE mode = axis port = fast_reply
#pragma HLS INTERFACE mode = axis port = to_pcie
  static int last = kNumActors + 1;
  kfpga::host_out_step(to_host, host_err, fast_reply, to_pcie, last);
}

// A caller actor that makes exactly one fast_send to a Pong in its own process.
struct OneFastSend
{
  static constexpr kfpga::ActorId kId = 5;
  PongActor<6> pong;
  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
  void on_ping(const Ping &m, kfpga::Ctx &ctx)
  {
    Pong r;
    if (ctx.fast_send(pong, m, r))
      ctx.reply(r);
  }
};

void bench_fast_send(hls::stream<Envelope> &fast_in, hls::stream<Envelope> &mbox,
                     hls::stream<Envelope> &call_reply, hls::stream<Envelope> &to_router,
                     hls::stream<Envelope> &fast_reply)
{
#pragma HLS INTERFACE mode = axis port = fast_in
#pragma HLS INTERFACE mode = axis port = mbox
#pragma HLS INTERFACE mode = axis port = call_reply
#pragma HLS INTERFACE mode = axis port = to_router
#pragma HLS INTERFACE mode = axis port = fast_reply
  static OneFastSend caller;
  kfpga::actor_step(caller, fast_in, mbox, call_reply, to_router, fast_reply);
}
