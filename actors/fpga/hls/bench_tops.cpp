/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * One top-level function per runtime part, each doing exactly one step. Vitis HLS
 * synthesis (run_hls.tcl) reports each one's latency in clock cycles; those are
 * the numbers the send / fast_send cost model is built from (see
 * FPGA_PROGRAMMERS_GUIDE.md, section 9).
 *
 *   bench_actor_step   Pong takes one message from a FIFO and replies
 *   bench_host_in      the host link takes one message from PCIe
 *   bench_host_out     the host link puts one message out to PCIe
 *   bench_fast_send    an actor fast_sends to an actor in its own process
 */

#include "actors_fpga/actor.hpp"
#include "actors_fpga/links.hpp"
#include "pingpong.hpp"

using kfpga::Envelope;
using namespace pingpong;

using L4 = kfpga::Links<kEndpoints>;

void bench_actor_step(L4 &L)
{
  static PongActor<kPong> pong;
  static kfpga::ActorPorts<kEndpoints> st;
  const kfpga::DiscoveryTable rt = discovery();
  kfpga::actor_step<PongActor<kPong>, kEndpoints, 1>(pong, L, rt, st);
}

void bench_host_in(hls::stream<Envelope> &from_host, L4 &L)
{
#pragma HLS INTERFACE mode = axis port = from_host
  const kfpga::DiscoveryTable rt = discovery();
  kfpga::host_in_step(from_host, L, rt);
}

void bench_host_out(L4 &L, hls::stream<Envelope> &to_pcie)
{
#pragma HLS INTERFACE mode = axis port = to_pcie
  static int last = 0;
  kfpga::host_out_step(L, to_pcie, last);
}

// A caller actor that makes exactly one fast_send to a Pong in its own process.
struct OneFastSend
{
  static constexpr kfpga::ActorId kId = 5;
  PongActor<6> pong;
  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
  template <class Ctx>
  void on_ping(const Ping &m, Ctx &ctx)
  {
    Pong r;
    if (ctx.fast_send(pong, m, r))
      ctx.reply(r);
  }
};

void bench_fast_send(L4 &L)
{
  static OneFastSend caller;
  static kfpga::ActorPorts<kEndpoints> st;
  const kfpga::DiscoveryTable rt = discovery();
  kfpga::actor_step<OneFastSend, kEndpoints, 0>(caller, L, rt, st);
}
