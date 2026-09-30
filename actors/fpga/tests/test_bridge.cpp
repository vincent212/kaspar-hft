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
#include <mutex>
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

  // on_error runs on the bridge's threads; the test reads through errors().
  std::mutex errors_mu;
  std::vector<std::string> errors_;
  std::vector<std::string> errors()
  {
    std::lock_guard<std::mutex> lk(errors_mu);
    return errors_;
  }
  bool wait_for_error()
  {
    for (int i = 0; i < 1000 && errors().empty(); ++i)
      std::this_thread::sleep_for(1ms);
    return !errors().empty();
  }

  Rig()
  {
    card = std::make_shared<kfpga::SoftCard>(design.steps(), design.from_host, design.to_pcie);
    bridge = std::make_shared<kfpga::FpgaBridge>(card);
    bridge->on_error = [this](const std::string &e)
    {
      std::lock_guard<std::mutex> lk(errors_mu);
      errors_.push_back(e);
    };
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
  EXPECT_TRUE(rig.errors().empty());
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
  ASSERT_TRUE(rig.wait_for_error());
  const auto errs = rig.errors();
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_NE(errs[0].find("no handler"), std::string::npos);
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
  const auto errs = rig.errors();
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_NE(errs[0].find("399"), std::string::npos);
}

// fast_send between two FPGA actors in different processes stays on the chip:
// no bridge is running, and nothing but the final Done reaches the host.
TEST(Fpga, FastSendBetweenFpgaActorsStaysOnChip)
{
  fpga_side::Design design;
  kfpga::SoftCard card(design.steps(), design.from_host, design.to_pcie);
  card.start();

  fpga_side::Go go;
  go.rounds = 3;
  go.mode = 2;   // FpgaCaller fast_sends FpgaPong
  kfpga::Envelope e;
  ASSERT_TRUE(kfpga::pack(go, fpga_side::kFpgaCaller, kfpga::kHost, kfpga::SEND, e));
  card.write(e);

  std::vector<kfpga::Envelope> out;
  for (int i = 0; i < 2000 && out.empty(); ++i)
  {
    kfpga::Envelope r;
    if (card.read(r))
      out.push_back(r);
    else
      std::this_thread::sleep_for(1ms);
  }
  std::this_thread::sleep_for(20ms);   // anything else that was going to arrive
  kfpga::Envelope r;
  while (card.read(r))
    out.push_back(r);
  card.stop();

  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].id, fpga_side::Done::id);
  fpga_side::Done d;
  kfpga::unpack(out[0], d);
  EXPECT_EQ(d.rounds, 3u);
  EXPECT_EQ(d.last, 3u + 1000u * 3u);
  EXPECT_EQ(design.pong.pings, 3u);
}

namespace {

// A CPU actor that, when an FPGA actor fast_sends it a Ping, itself fast_sends
// FpgaPong and answers with FpgaPong's reply. Its handler runs on the bridge's
// worker for the calling FPGA actor, while the reader thread delivers the reply
// to its own call.
class CpuRelay : public actors::Actor
{
public:
  explicit CpuRelay(actors::ActorRef fpga_pong) : fpga_pong_(fpga_pong)
  {
    std::strncpy(name, "CpuRelay", sizeof(name) - 1);
    MESSAGE_HANDLER(cpu_side::Ping, on_ping);
  }

private:
  void on_ping(const cpu_side::Ping *m)
  {
    cpu_side::Ping p(m->count);
    auto r = fpga_pong_.fast_send(&p, this);
    reply(new cpu_side::Pong(static_cast<const cpu_side::Pong *>(r.get())->count));
  }
  actors::ActorRef fpga_pong_;
};

class DoneSink : public actors::Actor
{
public:
  DoneSink()
  {
    std::strncpy(name, "DoneSink", sizeof(name) - 1);
    MESSAGE_HANDLER(cpu_side::Done, on_done);
  }
  std::promise<cpu_side::Done> done;

private:
  void on_done(const cpu_side::Done *m) { done.set_value(*m); }
};

} // namespace

// FPGA -> CPU fast_send whose CPU handler calls back into the FPGA.
TEST(Bridge, CpuHandlerCalledByFpgaCallsFpga)
{
  Rig rig;
  auto *relay = new CpuRelay(rig.bridge->ref(fpga_side::kFpgaPong));
  auto *sink = new DoneSink();
  rig.bridge->add_cpu_actor(fpga_side::kCpuPong, relay);
  rig.bridge->add_cpu_actor(fpga_side::kCpuDriver, sink);
  actors::Manager mgr;
  mgr.add_to_manage_q(relay);
  mgr.add_to_manage_q(sink);
  mgr.init();

  auto fut = sink->done.get_future();
  rig.bridge->ref(fpga_side::kFpgaCaller).send(new cpu_side::Go(3, 1), nullptr);
  ASSERT_EQ(fut.wait_for(10s), std::future_status::ready);
  const cpu_side::Done d = fut.get();
  mgr.end();

  EXPECT_EQ(d.rounds, 3u);
  EXPECT_EQ(d.last, 3u + 1000u * 3u);
  EXPECT_EQ(rig.design.pong.pings, 3u);
  EXPECT_TRUE(rig.errors().empty());
}
