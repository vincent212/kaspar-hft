/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FpgaFeed's sequence check, with the card's End lines written straight into its
// lane:
//   duplicate and late packets   counted as stale and ignored, no gap
//   a channel restart            (a drop far below the expected number) resynchronises,
//                                no gap
//   a real gap                   latched, with the number of packets missed

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "actors/act/Manager.hpp"
#include "fpga_feed.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;

namespace {

struct Sink : PacketSink
{
  std::atomic<int> packets{0};
  std::atomic<bool> gap{false};
  void on_packet(const Books &, const Event *, size_t, uint32_t, bool g) override
  {
    gap = g;
    ++packets;
  }
};

struct Run
{
  int packets;
  bool gap;
  uint64_t missing, stale, restarts;
};

// Sends one End line with one trade event for each sequence number, in order.
Run run(const std::vector<uint32_t> &seqs)
{
  Sink sink;
  kfpga::Link lane{1024};
  kfpga::Codec codec;
  cpu::add_messages(codec);
  auto *feed = new FpgaFeed({42001080}, sink, [] {});
  kfpga::CpuEnd *end = kfpga::CpuEnd::server(lane, feed, kFeed, kBatcher, codec);
  kfpga::Link table{16};   // the table load goes here, and no one answers it
  kfpga::CpuEnd *to_filter = kfpga::CpuEnd::client(table, feed, kFeed, kFilter, codec);
  feed->set_filter(to_filter->ref());
  actors::Manager mgr;
  mgr.add_to_manage_q(feed);
  mgr.init();
  for (uint32_t s : seqs)
  {
    const PackedEvent ev = pack_event(100, 1, 1, 0, 0, kActionTrade, false);
    EndLine<1> m;
    m.lo0 = ev.lo;
    m.hi0 = ev.hi;
    m.seq = s;
    lane.fwd.write(kfpga::encode(m, kBatcher, kFeed));
  }
  for (int i = 0; i < 400 && feed->lines < seqs.size(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const Run r{sink.packets.load(), sink.gap.load(), feed->missing, feed->stale, feed->restarts};
  mgr.end();
  delete end;
  delete to_filter;
  return r;
}

bool check(const char *what, const Run &r, int packets, bool gap, uint64_t missing, uint64_t stale, uint64_t restarts)
{
  const bool ok = r.packets == packets && r.gap == gap && r.missing == missing && r.stale == stale && r.restarts == restarts;
  std::printf("%-34s packets %d gap %d missing %llu stale %llu restarts %llu  %s\n", what, r.packets, r.gap,
              (unsigned long long)r.missing, (unsigned long long)r.stale, (unsigned long long)r.restarts,
              ok ? "ok" : "WRONG");
  return ok;
}

} // namespace

int main()
{
  bool pass = true;
  pass &= check("in order", run({100, 101, 102}), 3, false, 0, 0, 0);
  pass &= check("duplicate and late, then in order", run({100, 101, 101, 100, 102}), 3, false, 0, 2, 0);
  pass &= check("channel restart", run({5000, 5001, 1, 2}), 4, false, 0, 0, 1);
  pass &= check("gap of one", run({10, 12}), 2, true, 1, 0, 0);
  std::printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
