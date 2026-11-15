/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Several threads send to the card through one link at once, as can happen when
// a CPU actor's handler runs on a caller's thread or its reference is passed on.
// The window is a ring with one writer, so the sends must be serialized: every
// envelope must arrive whole, once, and each thread's in its own order.

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "cpu_actors.hpp"

using namespace pingpong;

namespace {

struct Plain : actors::Actor
{
  Plain()
  {
    std::strncpy(name, "Plain", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
  }
};

} // namespace

int main()
{
  constexpr int kThreads = 4, kEach = 200000;
  kfpga::Codec codec;
  cpu::add_messages(codec);
  kfpga::Link link(64);
  auto *plain = new Plain();
  kfpga::CpuEnd *end = kfpga::CpuEnd::client(link, plain, kCpuPing, kPong, codec);
  actors::ActorRef pong = end->ref();

  std::atomic<bool> go{false};
  std::vector<std::thread> senders;
  for (int t = 0; t < kThreads; ++t)
    senders.emplace_back([&, t] {
      while (!go.load())
      {
      }
      for (int i = 0; i < kEach; ++i)
        pong.send(new cpu::Ping(static_cast<uint32_t>(t) << 24 | static_cast<uint32_t>(i)), plain);
    });

  // The card: reads the window, checks each envelope. The CpuEnd queues what does
  // not fit, and its poll (normally the mailbox thread's) sends it on.
  std::vector<uint32_t> next(kThreads, 0);
  uint64_t got = 0, bad = 0;
  go = true;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (got < uint64_t(kThreads) * kEach && std::chrono::steady_clock::now() < deadline)
  {
    end->poll();
    kfpga::Envelope e;
    while (link.fwd.read_nb(e))
    {
      const Ping p = kfpga::decode<Ping>(e);
      const uint32_t t = p.count >> 24, i = p.count & 0xffffff;
      if (e.msg != Ping::id || e.src != kCpuPing || e.dst != kPong || t >= kThreads || i != next[t])
        ++bad;
      else
        ++next[t];
      ++got;
    }
  }
  for (auto &s : senders)
    s.join();
  const bool ok = got == uint64_t(kThreads) * kEach && bad == 0;
  std::printf("%d threads x %d sends at once: %llu arrived, %llu damaged or out of order  %s\n", kThreads, kEach,
              (unsigned long long)got, (unsigned long long)bad, ok ? "ok" : "WRONG");
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  delete end;
  return ok ? 0 : 1;
}
