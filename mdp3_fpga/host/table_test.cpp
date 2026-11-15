/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The filter table's size limit, from both ends:
//   - Filter answers a WantSecurity it could not store with ok = 0, never ok = 1;
//   - FpgaFeed refuses, when built, more securities than the table holds.
// Filter runs on a SoftCard, a test harness with no timing; the probe is a CPU actor.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

#include "actors/act/Manager.hpp"
#include "fpga_feed.hpp"
#include "kfpga/soft_card.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;
using FilterProc = kfpga::Process<Filter>;

namespace {

// Asks for one index inside the table and one past its end, and records the answers.
class Probe : public actors::Actor
{
  actors::ActorRef filter_;

public:
  std::atomic<int> answers{0}, ok_inside{-1}, ok_past{-1};

  Probe()
  {
    std::strncpy(name, "Probe", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(cpu::Ack, on_ack);
  }
  void set_filter(actors::ActorRef f) { filter_ = f; }

private:
  void on_start(const actors::msg::Start *)
  {
    filter_.send(new cpu::Want(3, 42001080), this);
    filter_.send(new cpu::Want(kSecurities, 42501731), this);
  }
  void on_ack(const cpu::Ack *m)
  {
    (m->index == 3 ? ok_inside : ok_past) = static_cast<int>(m->ok);
    ++answers;
  }
};

struct Sink : PacketSink
{
  void on_packet(const Books &, const Event *, size_t, uint32_t, bool) override {}
};

} // namespace

int main()
{
  bool pass = true;

  // FpgaFeed: one security more than the table holds is refused when built.
  Sink sink;
  std::vector<int32_t> too_many(kSecurities + 1);
  for (size_t i = 0; i < too_many.size(); ++i)
    too_many[i] = 1000 + static_cast<int32_t>(i);
  try
  {
    FpgaFeed feed(too_many, sink, [] {});
    std::printf("FpgaFeed took %zu securities for a table of %d: WRONG\n", too_many.size(), kSecurities);
    pass = false;
  }
  catch (const std::length_error &e)
  {
    std::printf("FpgaFeed refused %zu securities: %s  ok\n", too_many.size(), e.what());
  }

  // Filter: an index past the table is answered ok = 0.
  kfpga::Fifo decoded{16}, kept{16};
  kfpga::Link table{16};
  FilterProc filter;
  kfpga::Port ports[2] = {{decoded, kept}, table.server()};
  kfpga::SoftCard soft;
  soft.add(filter, ports);
  kfpga::Codec codec;
  cpu::add_messages(codec);
  auto *probe = new Probe();
  kfpga::CpuEnd *end = kfpga::CpuEnd::client(table, probe, kCpuFeed, kFilter, codec);
  probe->set_filter(end->ref());
  actors::Manager mgr;
  mgr.add_to_manage_q(probe);
  soft.start();
  mgr.init();
  for (int i = 0; i < 1000 && probe->answers < 2; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  mgr.end();
  soft.stop();
  const bool filter_ok = probe->answers == 2 && probe->ok_inside == 1 && probe->ok_past == 0;
  std::printf("Filter: index 3 answered ok=%d, index %d answered ok=%d  %s\n", probe->ok_inside.load(), kSecurities,
              probe->ok_past.load(), filter_ok ? "ok" : "WRONG");
  pass = pass && filter_ok;
  delete end;
  std::printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
