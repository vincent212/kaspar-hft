/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Two members of one group send each other more messages than the group's own
// queue holds: every Ping handled sends four more. In a software build the
// overflow must stop the program with a message, release build (NDEBUG) included;
// a silent drop is a failure. The burst runs in a child process.

#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

#include "kfpga/actor.hpp"
#include "messages.hpp"

using namespace pingpong;

namespace {

constexpr kfpga::ActorId kA = 40, kB = 41;

struct B;

struct A : kfpga::Actor<A>
{
  static constexpr kfpga::ActorId kId = kA;
  kfpga::ActorRef b;
  KFPGA_HANDLERS(KFPGA_ON(Start, on_start) KFPGA_ON(Ping, on_ping))
  void on_start(const Start &) { spray(); }
  void on_ping(const Ping &) { spray(); }
  void spray()
  {
    for (int i = 0; i < kfpga::kOutbox; ++i)
    {
      Ping p;
      b.send(p, this);
    }
  }
};

struct B : kfpga::Actor<B>
{
  static constexpr kfpga::ActorId kId = kB;
  kfpga::ActorRef a;
  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
  void on_ping(const Ping &)
  {
    for (int i = 0; i < kfpga::kOutbox; ++i)
    {
      Ping p;
      a.send(p, this);
    }
  }
};

using Grp = kfpga::Group<A, B>;

int burst()
{
  Grp g;
  Grp::member<A>().b = Grp::ref<B>();
  Grp::member<B>().a = Grp::ref<A>();
  kfpga::Link link(16);
  kfpga::Port ports[1] = {link.server()};
  Start s;
  link.fwd.write(kfpga::encode(s, kTestbench, kA));
  for (int i = 0; i < 1000; ++i)
    g.step(ports);
  std::printf("burst ran to the end; group queue full %u times\n", g.queue_full());
  return 0;
}

} // namespace

int main()
{
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0)
  {
    burst();
    std::fflush(stdout);
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  const bool stopped = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
  std::printf("group queue overflow %s\n", stopped ? "stopped the program" : "did NOT stop the program");
  std::printf("%s\n", stopped ? "PASS" : "FAIL");
  return stopped ? 0 : 1;
}
