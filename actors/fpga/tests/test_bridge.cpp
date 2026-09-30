/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CPU <-> FPGA through FpgaBridge, with the FPGA design on a SoftCard.

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "FpgaBridge.hpp"
#include "SoftCard.hpp"
#include "actors/act/Manager.hpp"
#include "cpu_side.hpp"
#include "fpga_side.hpp"

using namespace std::chrono_literals;

namespace {

struct Rig
{
  fpga_side::Design design;
  std::shared_ptr<kfpga::SoftCard> card;
  std::shared_ptr<kfpga::FpgaBridge> bridge;
  std::vector<std::string> errors;

  Rig()
  {
    card = std::make_shared<kfpga::SoftCard>(design.steps(), design.from_host, design.to_pcie);
    bridge = std::make_shared<kfpga::FpgaBridge>(card);
    bridge->on_error = [this](const std::string &e) { errors.push_back(e); };
    bridge->register_messages<cpu_side::Go, cpu_side::Ping, cpu_side::Pong, cpu_side::Done>();
    card->start();
    bridge->start();
  }
  ~Rig()
  {
    bridge->stop();
    card->stop();
  }
};

} // namespace

// The whole example: the four cases in order, as main.cpp runs them.
TEST(Bridge, FourDirections)
{
  Rig rig;
  std::vector<std::string> lines;
  std::promise<void> done;

  auto *cpu_pong = new cpu_side::CpuPong();
  auto *driver = new cpu_side::CpuDriver(
      rig.bridge->ref(fpga_side::kFpgaPong), rig.bridge->ref(fpga_side::kFpgaCaller),
      [&](const char *s) { lines.push_back(s); }, [&] { done.set_value(); });
  rig.bridge->add_cpu_actor(fpga_side::kCpuPong, cpu_pong);
  rig.bridge->add_cpu_actor(fpga_side::kCpuDriver, driver);

  actors::Manager mgr;
  mgr.add_to_manage_q(cpu_pong);
  mgr.add_to_manage_q(driver);
  mgr.init();
  ASSERT_EQ(done.get_future().wait_for(10s), std::future_status::ready);
  mgr.end();

  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "1 CPU->FPGA fast_send  Ping(1) -> Pong(1001)");
  EXPECT_EQ(lines[1], "2 CPU->FPGA send       Ping(2) -> Pong(2002)");
  EXPECT_EQ(lines[2], "3 FPGA->CPU send       3 round trips, last Pong(3003)");
  EXPECT_EQ(lines[3], "4 FPGA->CPU fast_send  3 round trips, last Pong(6003)");
  EXPECT_EQ(cpu_pong->pings, 6u);
  EXPECT_EQ(rig.design.pong.pings, 2u);
  EXPECT_TRUE(rig.errors.empty());
}

// fast_send from the CPU with no sender registered still gets its reply.
TEST(Bridge, CpuFastSendWithoutSender)
{
  Rig rig;
  auto ref = rig.bridge->ref(fpga_side::kFpgaPong);
  EXPECT_TRUE(ref.is_fpga());
  cpu_side::Ping p(5);
  auto r = ref.fast_send(&p, nullptr);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(static_cast<const cpu_side::Pong *>(r.get())->count, 5u + 1000u);
}

// fast_send to an FPGA actor that does not exist fails loudly, it does not hang.
TEST(Bridge, CpuFastSendToMissingActorThrows)
{
  Rig rig;
  auto ref = rig.bridge->ref(12);
  cpu_side::Ping p(1);
  EXPECT_THROW(ref.fast_send(&p, nullptr), std::runtime_error);
}

// fast_send to an FPGA actor with no handler for it returns no reply, and the
// FPGA reports the missing handler.
TEST(Bridge, CpuFastSendNoHandler)
{
  Rig rig;
  auto ref = rig.bridge->ref(fpga_side::kFpgaPong);
  cpu_side::Go g(1, 0);   // FpgaPong has no handler for Go
  EXPECT_EQ(ref.fast_send(&g, nullptr), nullptr);
  for (int i = 0; i < 1000 && rig.errors.empty(); ++i)
    std::this_thread::sleep_for(1ms);
  ASSERT_EQ(rig.errors.size(), 1u);
  EXPECT_NE(rig.errors[0].find("no handler"), std::string::npos);
}

namespace {
struct Unregistered : public actors::Message_N<399>
{
  uint32_t x = 0;
  KFPGA_FIELDS(x)
};
} // namespace

// A message class the bridge does not know is reported, not sent.
TEST(Bridge, UnregisteredMessageIsReported)
{
  Rig rig;
  auto ref = rig.bridge->ref(fpga_side::kFpgaPong);
  ref.send(new Unregistered(), nullptr);
  ASSERT_EQ(rig.errors.size(), 1u);
  EXPECT_NE(rig.errors[0].find("399"), std::string::npos);
}
