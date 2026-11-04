#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The group design: PingActor, PongActor and SyncPing as one process.
 * Shared by the software run (group.cpp) and the HLS top (hls/group_top.cpp).
 *
 *   PingActor -> PongActor  send, through the group's own queue: each Ping is
 *                           handled on a later step, as in a CPU Group
 *   SyncPing  -> PongActor  fast_send: an inline call, no FIFO at all
 *
 *   Grp  port 0: the testbench link
 *
 * Build with -DPONG_IN_OTHER_GROUP to see the build-time check: Pong moves to
 * a group of its own, and binding SyncPing's fast_send reference to it fails
 * to compile (make check_group_fast_send).
 */

#include "fpga_actors.hpp"

namespace pingpong {
namespace grp {

#ifndef PONG_IN_OTHER_GROUP
using Grp = kfpga::Group<PingActor, PongActor, SyncPing>;
#else
using Grp = kfpga::Group<PingActor, SyncPing>;
using PongGrp = kfpga::Group<PongActor>;
#endif

inline void wire()
{
  PingActor &ping = Grp::member<PingActor>();
  SyncPing &sync = Grp::member<SyncPing>();
  ping.pong = Grp::ref<PongActor>();
  ping.starter = kfpga::ActorRef(0, kTestbench);
  sync.starter = kfpga::ActorRef(0, kTestbench);
  Grp::bind(sync.pong);
}

} // namespace grp
} // namespace pingpong
