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

// Sends, for each sequence number, one packet: one End line with one trade event, or,
// with `full`, a full EventLine of three trades first and then an End line with none.
Run run(const std::vector<uint32_t> &seqs, bool full = false)
{
  Sink sink;
  kfpga::Link lane{1024};
  kfpga::Codec codec;
  cpu::add_messages(codec);
  auto *feed = new FpgaFeed({42001080}, sink, [] {});
  kfpga::CpuEnd *end = kfpga::CpuEnd::server(lane, feed, kCpuFeed, kBatcher, codec);
  kfpga::Link table{16};   // the table load goes here, and no one answers it
  kfpga::CpuEnd *to_filter = kfpga::CpuEnd::client(table, feed, kCpuFeed, kFilter, codec);
  feed->set_filter(to_filter->ref());
  actors::Manager mgr;
  mgr.add_to_manage_q(feed);
  mgr.init();
  for (uint32_t s : seqs)
  {
    const PackedEvent ev = pack_event(100, 1, 1, 0, 0, kActionTrade, false);
    if (full)
    {
      EventLine l;
      l.lo0 = l.lo1 = l.lo2 = ev.lo;
      l.hi0 = l.hi1 = l.hi2 = ev.hi;
      lane.fwd.write(kfpga::encode(l, kBatcher, kCpuFeed));
      EndLine<0> m;
      m.seq = s;
      lane.fwd.write(kfpga::encode(m, kBatcher, kCpuFeed));
    }
    else
    {
      EndLine<1> m;
      m.lo0 = ev.lo;
      m.hi0 = ev.hi;
      m.seq = s;
      lane.fwd.write(kfpga::encode(m, kBatcher, kCpuFeed));
    }
  }
  const size_t want_lines = seqs.size() * (full ? 2 : 1);
  for (int i = 0; i < 400 && feed->lines < want_lines; ++i)
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
  // A full line is applied when it arrives, before its packet's number is known, so a
  // late packet with one may have changed the books: the gap flag is set.
  pass &= check("late packet with a full line", run({100, 101, 100, 102}, true), 3, true, 0, 1, 0);
  pass &= check("full lines, in order", run({100, 101, 102}, true), 3, false, 0, 0, 0);
  std::printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
