/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The g++ test of Igmp, on the whole card (mdp3_feed_tick): every frame the card
// transmits is compared byte for byte with one built independently from RFC 2236
// (igmp_frames.hpp), and FeedRx's acceptance is checked by whether a data packet
// gets an End line. Every transmitted frame is also written to a pcap file, for
// tcpdump to check the checksums.
//
//   tb_igmp [out.pcap]

#include <cstdio>
#include <cstring>
#include <vector>

#include "feed_check.hpp"
#include "frames.hpp"
#include "igmp_frames.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"
#include "mdp3fpga/igmp.hpp"

using namespace mdp3fpga;
using namespace mdp3test;

void mdp3_feed_tick(WordStream &frames, WordStream &words, kfpga::Fifo &decoded, kfpga::Fifo &kept, kfpga::Fifo &window,
                    kfpga::Fifo &acks, kfpga::Fifo &out, kfpga::Fifo &join, kfpga::Fifo &joined, kfpga::Fifo &queries,
                    kfpga::Fifo &listen, WordStream &tx);

namespace {

WordStream frames, words, tx;
kfpga::Fifo decoded, kept, window, acks, out, join, joined, queries, listen;
std::vector<std::vector<uint8_t>> sent;   // every frame the card transmitted
std::vector<uint8_t> partial;
uint64_t ends = 0;
int failures = 0;

void tick(int n = 1)
{
  for (int i = 0; i < n; ++i)
  {
    mdp3_feed_tick(frames, words, decoded, kept, window, acks, out, join, joined, queries, listen, tx);
    while (!tx.empty())
    {
      const Word w = tx.read();
      for (int b = 0; b < 8; ++b)
        if (w.keep[b])
          partial.push_back(static_cast<uint8_t>(w.data.range(8 * b + 7, 8 * b)));
      if (w.last)
      {
        sent.push_back(partial);
        partial.clear();
      }
    }
    while (!out.empty())
    {
      const kfpga::Envelope e = out.read();
      if (e.msg >= kFirstEndLine && e.msg <= kLastEndLine)
        ++ends;
    }
  }
}

void check(bool ok, const char *what)
{
  std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

// Send the CPU's message to Igmp and return its ack.
template <class M>
IgmpAck to_igmp(const M &m)
{
  join.write(kfpga::encode(m, kCpuFeed, kIgmp));
  for (int i = 0; i < 100 && joined.empty(); ++i)
    tick();
  if (joined.empty())
    return IgmpAck{};
  return kfpga::decode<IgmpAck>(joined.read());
}

void wire(const std::vector<uint8_t> &f)
{
  frame_words<Word>(f, [](const Word &w) { frames.write(w); });
  tick(64);
}

// A data packet for 344 A: does it come out as an End line?
bool accepted()
{
  const uint64_t before = ends;
  std::vector<uint8_t> p(12, 0);   // an MDP3 packet header and nothing else
  p[0] = 1;
  wire(make_frame(p.data(), p.size(), kGroup344A, kPort344A, false));
  return ends == before + 1;
}

// The frames transmitted since `from`.
std::vector<std::vector<uint8_t>> since(size_t from)
{
  return std::vector<std::vector<uint8_t>>(sent.begin() + from, sent.end());
}

void write_pcap(const char *path)
{
  FILE *f = std::fopen(path, "wb");
  if (!f)
    return;
  const uint32_t hdr[6] = {0xa1b2c3d4, 0x00040002, 0, 0, 65535, 1};
  std::fwrite(hdr, 4, 6, f);
  uint32_t t = 0;
  for (const auto &fr : sent)
  {
    const uint32_t rec[4] = {++t, 0, static_cast<uint32_t>(fr.size()), static_cast<uint32_t>(fr.size())};
    std::fwrite(rec, 4, 4, f);
    std::fwrite(fr.data(), 1, fr.size(), f);
  }
  std::fclose(f);
}

} // namespace

int main(int argc, char **argv)
{
  // Short enough to see the repeats; 10 s on the card.
  kfpga::instance<Igmp>().repeat_clocks = 2000;

  std::printf("before any join\n");
  check(!accepted(), "a packet of 344 A is not accepted");
  check(sent.empty(), "nothing transmitted");

  std::printf("join 224.0.31.68:14344, no VLAN\n");
  size_t at = sent.size();
  IgmpAck a = to_igmp(join_344a(false));
  tick(20);
  check(a.ok == 1 && a.group == kGroup344A, "IgmpAck ok");
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_report(false)}, "one membership report, byte for byte");
  check(accepted(), "a packet of 344 A is accepted");

  std::printf("join again while joined\n");
  at = sent.size();
  a = to_igmp(join_344a(true));
  tick(20);
  check(a.ok == 0, "IgmpAck refuses");
  check(since(at).empty(), "nothing transmitted");

  std::printf("the join's repeats\n");
  at = sent.size();
  tick(5000);
  check(since(at) == std::vector<std::vector<uint8_t>>(2, expected_report(false)), "two more reports, then no more");
  tick(5000);
  check(sent.size() == at + 2, "none after");

  std::printf("queries from the switch\n");
  at = sent.size();
  wire(query(0, true, true));
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_report(false)}, "general query, tagged, Router Alert: a report");
  at = sent.size();
  wire(query(0, false, false));
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_report(false)}, "general query, no option: a report");
  at = sent.size();
  wire(query(kGroup344A, false, true));
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_report(false)}, "query for our group: a report");
  at = sent.size();
  wire(query(kGroup344A + 1, true, true));
  check(since(at).empty(), "query for another group: nothing");
  at = sent.size();
  wire(igmp_frame(0x16, 0, kGroup344A, kGroup344A, kSwitchMac, kSwitchIp, false, true));
  check(since(at).empty(), "another host's report: nothing");
  check(accepted(), "packets still accepted");

  std::printf("leave\n");
  at = sent.size();
  a = to_igmp(LeaveGroup{});
  tick(20);
  check(a.ok == 1 && a.group == kGroup344A, "IgmpAck ok");
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_leave(false)}, "one leave to 224.0.0.2, byte for byte");
  check(!accepted(), "a packet of 344 A is no longer accepted");
  at = sent.size();
  wire(query(0, false, true));
  check(since(at).empty(), "general query after leaving: nothing");
  a = to_igmp(LeaveGroup{});
  check(a.ok == 0, "leave again: refused");

  std::printf("join on VLAN 3008\n");
  at = sent.size();
  a = to_igmp(join_344a(true));
  tick(20);
  check(a.ok == 1, "IgmpAck ok");
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_report(true)}, "one tagged report, byte for byte");
  at = sent.size();
  wire(query(0, true, true));
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_report(true)}, "general query: a tagged report");
  at = sent.size();
  a = to_igmp(LeaveGroup{});
  tick(20);
  check(since(at) == std::vector<std::vector<uint8_t>>{expected_leave(true)}, "leave: a tagged leave");

  const Igmp &g = kfpga::instance<Igmp>();
  std::printf("%zu frames sent: %u reports, %u leaves; %u queries seen\n", sent.size(), g.reports, g.leaves, g.queries);
  if (argc > 1)
    write_pcap(argv[1]);
  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
