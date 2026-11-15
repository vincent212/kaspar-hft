/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Where a card's lane goes:
//   - an actor without an FpgaLanes mailbox: add_lane must stop with a message,
//     release build included, not crash or lose the lane (run in a child process);
//   - a member of a Group: the lane must go to the group's mailbox, the one its
//     thread polls, whether the CpuEnd is built before or after the actor joins.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "actors/act/Group.hpp"
#include "actors/act/Manager.hpp"
#include "cpu_actors.hpp"

using namespace pingpong;

namespace {

struct Plain : actors::Actor
{
  Plain() { std::strncpy(name, "Plain", sizeof(name) - 1); }
};

struct SecondPong : cpu::CpuPong
{
  SecondPong() { std::strncpy(name, "CpuPong2", sizeof(name) - 1); }
};

struct LaneGroup : actors::Group
{
  LaneGroup() : actors::Group("lanes") { set_mailbox(MailboxKind::FpgaLanes); }
};

void feed(kfpga::Link &link, kfpga::ActorId dst, uint32_t n)
{
  for (uint32_t i = 1; i <= n; ++i)
  {
    Ping p;
    p.count = i;
    link.fwd.write(kfpga::encode(p, kPing, dst));
  }
}

} // namespace

int main()
{
  bool pass = true;
  kfpga::Codec codec;
  cpu::add_messages(codec);

  // An actor whose mailbox has no lanes.
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0)
  {
    kfpga::Link link(16);
    auto *plain = new Plain();
    kfpga::CpuEnd::server(link, plain, kCpuPong, kPing, codec);
    std::printf("add_lane on an actor without FpgaLanes returned\n");
    std::fflush(stdout);
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  const bool stopped = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
  std::printf("add_lane on an actor without FpgaLanes: %s  %s\n",
              stopped ? "stopped with a message" : WIFSIGNALED(status) ? "crashed" : "returned",
              stopped ? "ok" : "WRONG");
  pass &= stopped;

  // Two members of a group: one's lane added after it joins, one's before.
  constexpr uint32_t kN = 100;
  kfpga::Link a(256), b(256);
  LaneGroup grp;
  auto *joined_first = new cpu::CpuPong();
  grp.add(joined_first);
  kfpga::CpuEnd *end_a = kfpga::CpuEnd::server(a, joined_first, kCpuPong, kPing, codec);
  auto *lane_first = new SecondPong();
  kfpga::CpuEnd *end_b = kfpga::CpuEnd::server(b, lane_first, kCpuPong, kPing, codec);
  grp.add(lane_first);
  feed(a, kCpuPong, kN);
  feed(b, kCpuPong, kN);

  actors::Manager mgr;
  mgr.add_to_manage_q(&grp);
  mgr.init();
  for (int i = 0; i < 400 && (joined_first->pings < kN || lane_first->pings < kN); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const bool got = joined_first->pings == kN && lane_first->pings == kN;
  std::printf("group members: lane added after joining got %u of %u, before joining %u of %u  %s\n",
              joined_first->pings, kN, lane_first->pings, kN, got ? "ok" : "WRONG");
  pass &= got;
  if (!got)
  {
    std::printf("FAIL\n");
    std::fflush(stdout);
    std::_Exit(1);   // the group's thread may never stop
  }
  mgr.end();
  delete end_a;
  delete end_b;
  std::printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
