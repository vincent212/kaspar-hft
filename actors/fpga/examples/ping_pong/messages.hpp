#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The ping-pong messages and actor ids, declared once. This header is compiled
 * into the FPGA design and into the CPU program; the CPU wraps each message in
 * kfpga::CpuMsg (cpu_link.hpp).
 */

#include "kfpga/envelope.hpp"

namespace pingpong {

using kfpga::ActorId;

struct Start : kfpga::Message<300>
{
  uint32_t rounds = 0;
  KFPGA_FIELDS(rounds)
};

struct Ping : kfpga::Message<301>
{
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};

struct Pong : kfpga::Message<302>
{
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};

struct Done : kfpga::Message<303>
{
  uint32_t final_count = 0;   // count carried by the last Pong
  uint32_t rounds = 0;   // round trips completed
  KFPGA_FIELDS(final_count, rounds)
};

// Actor ids, shared by both sides.
constexpr ActorId kTestbench = 0;
constexpr ActorId kPing = 1;
constexpr ActorId kPong = 2;
constexpr ActorId kCpuPing = 8;
constexpr ActorId kCpuPong = 9;
constexpr ActorId kCpuDriver = 10;

} // namespace pingpong
