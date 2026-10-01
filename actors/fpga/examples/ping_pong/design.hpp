#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The ping-pong design's wiring: the FIFOs between its processes (Links) and the
 * table that says which endpoint holds which actor. On the FPGA each process runs
 * on its own (hls/pingpong_top.cpp); step() runs one step of every process in
 * turn, which is how the unit tests drive it.
 */

#include "actors_fpga/actor.hpp"
#include "actors_fpga/links.hpp"
#include "pingpong.hpp"

namespace pingpong {

struct Design
{
  PingActor ping;
  PongActor<kPong> pong;
  FusedPing fused;

  kfpga::Links<kEndpoints> links;
  hls::stream<kfpga::Envelope> from_host_reply;   // replies to the actors' calls to the CPU
  kfpga::DiscoveryTable rt = discovery();
  kfpga::ActorPorts<kEndpoints> ping_st, pong_st, fused_st;
  int out_last = 0;

  // One step of every process. Returns true if any of them moved a message.
  // (An actor waiting on its own remote fast_send blocks here; designs with
  // such actors run one thread per process, on a SoftCard.)
  bool step(hls::stream<kfpga::Envelope> &from_host, hls::stream<kfpga::Envelope> &to_pcie)
  {
    bool w = false;
    w |= kfpga::host_in_step(from_host, links, rt);
    w |= kfpga::host_in_reply_step(from_host_reply, links, rt);
    w |= kfpga::actor_step<PingActor, kEndpoints, 0>(ping, links, rt, ping_st);
    w |= kfpga::actor_step<PongActor<kPong>, kEndpoints, 1>(pong, links, rt, pong_st);
    w |= kfpga::actor_step<FusedPing, kEndpoints, 2>(fused, links, rt, fused_st);
    w |= kfpga::host_out_step(links, to_pcie, out_last);
    return w;
  }

  // Step until nothing moves. Returns the number of rounds taken.
  int run(hls::stream<kfpga::Envelope> &from_host, hls::stream<kfpga::Envelope> &to_pcie,
          int max_rounds = 100000)
  {
    int r = 0;
    while (r < max_rounds && step(from_host, to_pcie))
      ++r;
    return r;
  }
};

} // namespace pingpong
