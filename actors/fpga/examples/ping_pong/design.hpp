#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The ping-pong design's wiring: which stream connects which process. On the
 * FPGA each process runs on its own (hls/pingpong_top.cpp); step() runs one step
 * of every process in turn, which is how the unit tests drive it.
 */

#include "actors_fpga/actor.hpp"
#include "actors_fpga/router.hpp"
#include "pingpong.hpp"

namespace pingpong {

struct Design
{
  PingActor ping;
  PongActor<kPong> pong;
  FusedPing fused;

  hls::stream<kfpga::Envelope> fast_in[kNumActors];
  hls::stream<kfpga::Envelope> mbox[kNumActors];
  hls::stream<kfpga::Envelope> call_reply[kNumActors];
  hls::stream<kfpga::Envelope> router_in[kRouterInputs];   // 0..2 actors, 3 host_in
  hls::stream<kfpga::Envelope> fast_reply[kNumActors];
  hls::stream<kfpga::Envelope> to_host;
  hls::stream<kfpga::Envelope> host_err;

  kfpga::RouteTable rt = routes();
  int router_last = kRouterInputs - 1;
  int out_last = kNumActors + 1;

  // One step of every process. Returns true if any of them moved a message.
  bool step(hls::stream<kfpga::Envelope> &from_host, hls::stream<kfpga::Envelope> &to_pcie)
  {
    bool w = false;
    w |= kfpga::host_in_step(from_host, router_in[3], fast_in, call_reply, host_err, rt);
    w |= kfpga::router_step(router_in, mbox, to_host, rt, router_last);
    w |= kfpga::actor_step(ping, fast_in[0], mbox[0], call_reply[0], router_in[0], fast_reply[0]);
    w |= kfpga::actor_step(pong, fast_in[1], mbox[1], call_reply[1], router_in[1], fast_reply[1]);
    w |= kfpga::actor_step(fused, fast_in[2], mbox[2], call_reply[2], router_in[2], fast_reply[2]);
    w |= kfpga::host_out_step(to_host, host_err, fast_reply, to_pcie, out_last);
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
