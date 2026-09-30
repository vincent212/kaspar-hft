/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CPU actors and FPGA actors talking both ways with send and fast_send.
// The FPGA design runs on a SoftCard (software threads) until there is a card.

#include <cstdio>
#include <future>
#include <memory>

#include "actors/act/Manager.hpp"
#include "FpgaBridge.hpp"
#include "SoftCard.hpp"
#include "cpu_side.hpp"
#include "fpga_side.hpp"

int main()
{
  // The FPGA side.
  fpga_side::Design design;
  auto card = std::make_shared<kfpga::SoftCard>(design.steps(), design.from_host, design.to_pcie);

  // The bridge between the two runtimes.
  auto bridge = std::make_shared<kfpga::FpgaBridge>(card);
  bridge->register_messages<cpu_side::Go, cpu_side::Ping, cpu_side::Pong, cpu_side::Done>();

  // The CPU side: ordinary actors under a Manager.
  std::promise<void> finished;
  auto *cpu_pong = new cpu_side::CpuPong();
  auto *driver = new cpu_side::CpuDriver(
      bridge->ref(fpga_side::kFpgaPong), bridge->ref(fpga_side::kFpgaCaller),
      [](const char *s) { std::printf("%s\n", s); }, [&] { finished.set_value(); });
  bridge->add_cpu_actor(fpga_side::kCpuPong, cpu_pong);
  bridge->add_cpu_actor(fpga_side::kCpuDriver, driver);

  actors::Manager mgr;
  mgr.add_to_manage_q(cpu_pong);
  mgr.add_to_manage_q(driver);

  card->start();
  bridge->start();
  mgr.init();                  // Start -> CpuDriver::on_start
  finished.get_future().wait();
  mgr.end();
  bridge->stop();
  card->stop();
  return bridge->errors() == 0 ? 0 : 1;
}
