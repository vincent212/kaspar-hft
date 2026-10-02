/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CPU <-> FPGA through FpgaBridge, with the FPGA design on a SoftCard.

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
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
    card = std::make_shared<kfpga::SoftCard>(design.steps(), design.from_host,
                                               design.from_host_reply, design.to_pcie);
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

// fast_send to an FPGA actor with no handler for it fails: it throws, naming the
// missing handler, rather than looking like a handler that chose not to reply.
TEST(Bridge, CpuFastSendNoHandlerThrows)
{
  Rig rig;
  auto ref = rig.bridge->ref(fpga_side::kFpgaPong);
  cpu_side::Go g(1, 0);   // FpgaPong has no handler for Go
  try
  {
    ref.fast_send(&g, nullptr);
    FAIL() << "expected a throw";
  }
  catch (const std::runtime_error &e)
  {
    EXPECT_NE(std::string(e.what()).find("no handler"), std::string::npos);
  }
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
  kfpga::SoftCard card(design.steps(), design.from_host, design.from_host_reply, design.to_pcie);
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
    if (r)
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

// Id 0 is the host itself.
TEST(Bridge, ActorIdZeroIsRejected)
{
  Rig rig;
  cpu_side::CpuPong a;
  EXPECT_THROW(rig.bridge->add_cpu_actor(kfpga::kHost, &a), std::invalid_argument);
}

// Many CPU threads fast_send the same FPGA actor at once; each gets its own reply.
TEST(Bridge, ConcurrentCpuFastSendsGetTheirOwnReplies)
{
  Rig rig;
  auto ref = rig.bridge->ref(fpga_side::kFpgaPong);
  constexpr int kThreads = 8, kCalls = 50;
  std::atomic<int> wrong{0}, done{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t)
    ts.emplace_back([&, t] {
      for (int i = 0; i < kCalls; ++i)
      {
        const uint32_t c = static_cast<uint32_t>(t * 1000000 + i);
        cpu_side::Ping p(c);
        auto r = ref.fast_send(&p, nullptr);
        // FpgaPong answers count + 1000 * pings; pings < 1000 here, so the low
        // part identifies the request.
        if (!r || static_cast<const cpu_side::Pong *>(r.get())->count / 1000000 !=
                      static_cast<uint32_t>(t))
          ++wrong;
        ++done;
      }
    });
  for (auto &t : ts)
    t.join();
  EXPECT_EQ(done.load(), kThreads * kCalls);
  EXPECT_EQ(wrong.load(), 0);
  EXPECT_TRUE(rig.errors().empty());
}

// stop() releases a CPU caller still waiting, and reports it.
TEST(Bridge, StopReleasesWaitingCaller)
{
  Rig rig;
  rig.card->stop();   // the FPGA never answers
  auto ref = rig.bridge->ref(fpga_side::kFpgaPong);
  std::promise<bool> got_null;
  std::thread caller([&] {
    cpu_side::Ping p(1);
    got_null.set_value(ref.fast_send(&p, nullptr) == nullptr);
  });
  // The caller is waiting once its request has reached the (stopped) card.
  for (int i = 0; i < 5000 && rig.design.from_host.empty(); ++i)
    std::this_thread::sleep_for(1ms);
  ASSERT_FALSE(rig.design.from_host.empty());
  rig.bridge->stop();
  auto f = got_null.get_future();
  ASSERT_EQ(f.wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(f.get());
  caller.join();
  const auto errs = rig.errors();
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_NE(errs[0].find("bridge stopped"), std::string::npos);
}

namespace {

// Answers an FPGA actor's Ping by fast_sending a Pong back to that actor, through
// the sender it was given (the bridge's stand-in for the FPGA actor).
class AnswersThroughSender : public actors::Actor
{
public:
  AnswersThroughSender()
  {
    std::strncpy(name, "AnswersThroughSender", sizeof(name) - 1);
    MESSAGE_HANDLER(cpu_side::Ping, on_ping);
  }

private:
  void on_ping(const cpu_side::Ping *m)
  {
    cpu_side::Pong p(77);
    (void)m->sender->fast_send(&p, this);
  }
};

} // namespace

// A CPU fast_send to the stand-in for an FPGA actor reaches the FPGA actor.
TEST(Bridge, FastSendToFpgaSenderReachesFpga)
{
  Rig rig;
  auto *answer = new AnswersThroughSender();
  auto *sink = new DoneSink();
  rig.bridge->add_cpu_actor(fpga_side::kCpuPong, answer);
  rig.bridge->add_cpu_actor(fpga_side::kCpuDriver, sink);
  actors::Manager mgr;
  mgr.add_to_manage_q(answer);
  mgr.add_to_manage_q(sink);
  mgr.init();

  auto fut = sink->done.get_future();
  // FpgaCaller sends Ping(1) to kCpuPong; the answer comes back as a fast_send of
  // Pong(77) to FpgaCaller, which then reports Done(last = 77).
  rig.bridge->ref(fpga_side::kFpgaCaller).send(new cpu_side::Go(1, 0), nullptr);
  ASSERT_EQ(fut.wait_for(10s), std::future_status::ready);
  const cpu_side::Done d = fut.get();
  mgr.end();
  EXPECT_EQ(d.last, 77u);
  EXPECT_TRUE(rig.errors().empty());
}

// A failed FPGA -> CPU fast_send is reported once.
TEST(Bridge, FailedFpgaToCpuFastSendReportedOnce)
{
  Rig rig;
  auto *sink = new DoneSink();
  rig.bridge->add_cpu_actor(fpga_side::kCpuDriver, sink);   // kCpuPong not registered
  actors::Manager mgr;
  mgr.add_to_manage_q(sink);
  mgr.init();

  auto fut = sink->done.get_future();
  rig.bridge->ref(fpga_side::kFpgaCaller).send(new cpu_side::Go(1, 1), nullptr);
  ASSERT_EQ(fut.wait_for(10s), std::future_status::ready);
  EXPECT_EQ(fut.get().rounds, 0u);
  mgr.end();
  std::this_thread::sleep_for(20ms);   // anything else that was going to be reported
  EXPECT_EQ(rig.errors().size(), 1u);
}

namespace {

// A transport whose first write throws; envelopes put in `incoming` are read back.
class ThrowingCard : public kfpga::CardTransport
{
public:
  void write(const kfpga::Envelope &) override
  {
    if (!thrown_.exchange(true))
      throw std::runtime_error("card write failed");
  }
  bool read(kfpga::Envelope &e) override
  {
    std::lock_guard<std::mutex> lk(mu);
    if (incoming.empty())
      return false;
    e = incoming.front();
    incoming.pop_front();
    return true;
  }
  std::mutex mu;
  std::deque<kfpga::Envelope> incoming;

private:
  std::atomic<bool> thrown_{false};
};

} // namespace

// A card write that throws leaves no waiter behind: a reply that arrives later
// for that request is reported, not delivered to freed memory.
TEST(Bridge, ThrowingWriteLeavesNoWaiter)
{
  auto card = std::make_shared<ThrowingCard>();
  auto bridge = std::make_shared<kfpga::FpgaBridge>(card);
  std::mutex mu;
  std::vector<std::string> errs;
  bridge->on_error = [&](const std::string &e) {
    std::lock_guard<std::mutex> lk(mu);
    errs.push_back(e);
  };
  bridge->register_messages<cpu_side::Ping, cpu_side::Pong>();
  bridge->start();

  cpu_side::Ping p(1);
  EXPECT_THROW(bridge->ref(fpga_side::kFpgaPong).fast_send(&p, nullptr), std::runtime_error);

  cpu_side::Pong late(5);
  kfpga::Envelope e;
  ASSERT_TRUE(kfpga::pack(late, kfpga::kHost, fpga_side::kFpgaPong, kfpga::FAST_REPLY, e));
  e.tag = 1;   // the first tag the bridge hands out
  {
    std::lock_guard<std::mutex> lk(card->mu);
    card->incoming.push_back(e);
  }
  for (int i = 0; i < 1000; ++i)
  {
    {
      std::lock_guard<std::mutex> lk(mu);
      if (!errs.empty())
        break;
    }
    std::this_thread::sleep_for(1ms);
  }
  bridge->stop();
  std::lock_guard<std::mutex> lk(mu);
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_NE(errs[0].find("no caller waiting"), std::string::npos);
}

// start() twice is refused.
TEST(Bridge, StartTwiceThrows)
{
  Rig rig;
  EXPECT_THROW(rig.bridge->start(), std::logic_error);
}

// A send while the bridge is not running is reported, not written to a card
// nobody reads.
TEST(Bridge, SendAfterStopIsReported)
{
  Rig rig;
  rig.bridge->stop();
  rig.bridge->ref(fpga_side::kFpgaPong).send(new cpu_side::Ping(1), nullptr);
  const auto errs = rig.errors();
  ASSERT_EQ(errs.size(), 1u);
  EXPECT_NE(errs[0].find("not running"), std::string::npos);
  EXPECT_TRUE(rig.design.from_host.empty());
}

namespace {
// A CPU actor that, when an FPGA actor fast_sends it, calls back into the FPGA
// after a pause, so that stop() can arrive while the handler is running.
class SlowRelay : public actors::Actor
{
public:
  explicit SlowRelay(actors::ActorRef fpga_pong) : fpga_pong_(fpga_pong)
  {
    std::strncpy(name, "SlowRelay", sizeof(name) - 1);
    MESSAGE_HANDLER(cpu_side::Ping, on_ping);
  }
  std::atomic<bool> entered{false};
  std::atomic<bool> finished{false};

private:
  void on_ping(const cpu_side::Ping *m)
  {
    entered = true;
    std::this_thread::sleep_for(50ms);
    cpu_side::Ping p(m->count);
    auto r = fpga_pong_.fast_send(&p, this);
    if (r)
      reply(new cpu_side::Pong(static_cast<const cpu_side::Pong *>(r.get())->count));
    finished = true;
  }
  actors::ActorRef fpga_pong_;
};
} // namespace

// stop() while a CPU handler run for an FPGA actor is still calling the FPGA:
// the handler finishes normally, and shutdown completes.
TEST(Bridge, StopWaitsForRunningCpuHandlers)
{
  Rig rig;
  auto *relay = new SlowRelay(rig.bridge->ref(fpga_side::kFpgaPong));
  auto *sink = new DoneSink();
  rig.bridge->add_cpu_actor(fpga_side::kCpuPong, relay);
  rig.bridge->add_cpu_actor(fpga_side::kCpuDriver, sink);
  actors::Manager mgr;
  mgr.add_to_manage_q(relay);
  mgr.add_to_manage_q(sink);
  mgr.init();

  rig.bridge->ref(fpga_side::kFpgaCaller).send(new cpu_side::Go(1, 1), nullptr);
  for (int i = 0; i < 5000 && !relay->entered; ++i)
    std::this_thread::sleep_for(1ms);
  ASSERT_TRUE(relay->entered.load());
  rig.bridge->stop();   // the handler is mid-pause, about to call the FPGA
  EXPECT_TRUE(relay->finished.load());
  mgr.end();
}
