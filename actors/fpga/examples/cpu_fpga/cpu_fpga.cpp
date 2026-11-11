/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CPU actors and FPGA actors talking both ways with send.
//
//   CPU -> FPGA  CpuPing plays 1000 round trips against the FPGA PongActor.
//   FPGA -> CPU  CpuDriver sends Start to the FPGA PingActor, which plays
//                1000 round trips against the CPU CpuPong and sends Done.
//
// The FPGA actors are the same code as in fpga_to_fpga; only the wiring
// (cpu_fpga_design.hpp) differs. The FPGA half runs on a SoftCard here (a
// software harness); hls/cpu_fpga_top.cpp is the same half in Vitis HLS.

#include <cstdio>
#include <future>
#include <mutex>

#include "actors/act/Manager.hpp"
#include "cpu_actors.hpp"
#include "cpu_fpga_design.hpp"
#include "kfpga/soft_card.hpp"

using namespace pingpong;
using namespace pingpong::cpu_fpga;

namespace {

// The card: the FPGA processes, the three links and their ports.
struct Card
{
  kfpga::Link a;   // CpuPing   -> PongActor
  kfpga::Link b;   // CpuDriver -> PingActor
  kfpga::Link c;   // PingActor -> CpuPong
  PingProc ping_proc;
  PongProc pong_proc;
  kfpga::Port ping_ports[2] = {b.server(), c.client()};
  kfpga::Port pong_ports[1] = {a.server()};

  Card() { wire(); }
};

// Collects the two results; main waits for both.
class Results
{
  std::mutex mu_;
  int left_ = 2;
  bool ok_ = true;
  std::promise<void> all_;

public:
  void add(const char *line, bool ok)
  {
    std::lock_guard<std::mutex> g(mu_);
    std::printf("%s  %s\n", line, ok ? "ok" : "WRONG");
    ok_ &= ok;
    if (--left_ == 0)
      all_.set_value();
  }
  void wait() { all_.get_future().wait(); }
  bool ok() const { return ok_; }
};

} // namespace

int main()
{
  constexpr uint32_t kRounds = 1000;

  Card card;
  kfpga::SoftCard soft;
  soft.add(card.ping_proc, card.ping_ports);
  soft.add(card.pong_proc, card.pong_ports);

  kfpga::Codec codec;
  cpu::add_messages(codec);

  Results results;
  auto report = [&results](const char *line, bool ok) { results.add(line, ok); };
  auto *cpu_ping = new cpu::CpuPing(kRounds, report);
  auto *driver = new cpu::CpuDriver(kRounds, report);
  auto *cpu_pong = new cpu::CpuPong();

  // The CPU ends of the three links. Each adds its lane to the CPU actor's
  // mailbox, so add them before the actors start.
  kfpga::CpuEnd *end_a = kfpga::CpuEnd::client(card.a, cpu_ping, kCpuPing, kPong, codec);
  kfpga::CpuEnd *end_b = kfpga::CpuEnd::client(card.b, driver, kCpuDriver, kPing, codec);
  kfpga::CpuEnd *end_c = kfpga::CpuEnd::server(card.c, cpu_pong, kCpuPong, kPing, codec);
  cpu_ping->set_pong(end_a->ref());
  driver->set_ping(end_b->ref());

  actors::Manager mgr;
  mgr.add_to_manage_q(cpu_pong);
  mgr.add_to_manage_q(cpu_ping);
  mgr.add_to_manage_q(driver);

  soft.start();
  mgr.init();   // Start -> CpuPing::on_start, CpuDriver::on_start
  results.wait();
  mgr.end();
  soft.stop();

  const uint32_t fpga_pings = PongProc::member<PongActor>().pings;
  const uint32_t errors = end_a->errors() + end_b->errors() + end_c->errors();
  std::printf("FPGA PongActor saw %u pings, CPU CpuPong saw %u; codec errors %u\n", fpga_pings,
              cpu_pong->pings, errors);
  const bool ok = results.ok() && fpga_pings == kRounds && cpu_pong->pings == kRounds && errors == 0 &&
                  card.ping_proc.unhandled() == 0 && card.pong_proc.unhandled() == 0;
  std::printf("%s\n", ok ? "OK" : "FAILED");
  delete end_a;
  delete end_b;
  delete end_c;
  return ok ? 0 : 1;
}
