/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// A CPU actor sends far more messages in one handler than the link holds, to an
// FPGA actor that answers each. The CPU thread is the only one draining the
// answers, so if a send ever blocked on a full window while the answers filled the
// lane, both would wait forever. Passes if every answer arrives within the time
// limit.
//
//   link_burst [messages] [link depth]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>

#include "actors/act/Manager.hpp"
#include "cpu_actors.hpp"
#include "cpu_fpga_design.hpp"
#include "kfpga/soft_card.hpp"

using namespace pingpong;
using namespace pingpong::cpu_fpga;

namespace {

class Burst : public actors::Actor
{
  actors::ActorRef pong_;
  uint32_t n_;
  std::promise<void> &done_;

public:
  uint32_t replies = 0;

  Burst(uint32_t n, std::promise<void> &done) : n_(n), done_(done)
  {
    std::strncpy(name, "Burst", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(cpu::Pong, on_pong);
  }
  void set_pong(actors::ActorRef pong) { pong_ = pong; }

private:
  void on_start(const actors::msg::Start *)
  {
    for (uint32_t i = 1; i <= n_; ++i)
      pong_.send(new cpu::Ping(i), this);   // all of them, before any answer is read
  }
  void on_pong(const cpu::Pong *)
  {
    if (++replies == n_)
      done_.set_value();
  }
};

} // namespace

int main(int argc, char **argv)
{
  const uint32_t n = argc > 1 ? std::atoi(argv[1]) : 200;
  const size_t depth = argc > 2 ? std::atoi(argv[2]) : 16;

  kfpga::Link link(depth);
  PongProc pong_proc;
  kfpga::Port pong_ports[1] = {link.server()};
  kfpga::SoftCard soft;
  soft.add(pong_proc, pong_ports);

  kfpga::Codec codec;
  cpu::add_messages(codec);
  std::promise<void> done;
  auto *burst = new Burst(n, done);
  kfpga::CpuEnd *end = kfpga::CpuEnd::client(link, burst, kCpuPing, kPong, codec);
  burst->set_pong(end->ref());

  // A stuck link blocks the Start handler, and mgr.init() with it, so the time
  // limit runs on a thread of its own.
  auto finished = done.get_future().share();
  std::thread([finished, burst, n, depth] {
    if (finished.wait_for(std::chrono::seconds(10)) == std::future_status::ready)
      return;
    std::printf("%u messages over a link %zu deep: %u answers within 10 s, so the link is stuck\nFAILED\n", n,
                depth, burst->replies);
    std::fflush(stdout);
    std::_Exit(1);   // the stuck threads would never join
  }).detach();

  actors::Manager mgr;
  mgr.add_to_manage_q(burst);
  soft.start();
  mgr.init();
  finished.wait();
  std::printf("%u messages over a link %zu deep: %u answers\n", n, depth, burst->replies);
  mgr.end();
  soft.stop();
  std::printf("OK\n");
  delete end;
  return 0;
}
