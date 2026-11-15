/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The g++ test of FeedRx: every packet of a recording rebuilt as a frame, every
// seventh with an 802.1Q tag, mixed with frames it must drop (another group,
// another port, not IPv4, not UDP, too short, padded, another host's IGMP report),
// and each payload out compared byte for byte; and IGMP queries, tagged or not, with
// or without Router Alert, each passed to Igmp with its group. Holds one packet.
//
//   MDP3_PACKETS  the packet file

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

#include "frames.hpp"
#include "igmp_frames.hpp"
#include "kfpga/actor.hpp"
#include "mdp3fpga/feed_rx.hpp"

using namespace mdp3fpga;
using namespace mdp3test;

int main()
{
  FILE *packets = std::fopen(std::getenv("MDP3_PACKETS"), "rb");
  if (!packets)
  {
    std::printf("set MDP3_PACKETS\nFAIL\n");
    return 1;
  }
  using RxProc = kfpga::Process<FeedRx>;
  static RxProc proc;
  FeedRx &rx = RxProc::member<FeedRx>();
  rx.igmp = kfpga::ActorRef(0, kIgmp);
  WordStream in, out;
  kfpga::Fifo listen, queries;
  kfpga::OutPort ports[1] = {{queries}};
  // The group and port, as Igmp sets them.
  Listen lsn;
  lsn.group = kGroup344A;
  lsn.port = kPort344A;
  listen.write(kfpga::encode(lsn, kIgmp, kFeedRx));
  uint64_t query_errors = 0, sent_queries = 0;
  std::deque<uint32_t> query_groups;
  auto step = [&]() {
    proc.step_stream<FeedRx>(ports, in, out, listen);
    while (!queries.empty())
    {
      const kfpga::Envelope e = queries.read();
      const IgmpQuery q = kfpga::decode<IgmpQuery>(e);
      if (e.msg != IgmpQuery::id || query_groups.empty() || q.group != query_groups.front() || q.max_resp != 100)
        ++query_errors;
      if (!query_groups.empty())
        query_groups.pop_front();
    }
  };
  std::deque<std::vector<uint8_t>> expected;
  uint64_t n_packets = 0, diffs = 0, sent_noise = 0, frames = 0;
  std::vector<uint8_t> got;

  auto drain = [&]() {
    while (!out.empty())
    {
      const Word w = out.read();
      for (int i = 0; i < 8; ++i)
        if (w.keep[i])
          got.push_back(static_cast<uint8_t>(w.data.range(8 * i + 7, 8 * i)));
      if (w.last)
      {
        if (expected.empty() || got != expected.front())
        {
          if (++diffs <= 3)
            std::printf("packet %llu differs: %zu bytes, want %zu\n", (unsigned long long)n_packets, got.size(),
                        expected.empty() ? 0 : expected.front().size());
        }
        if (!expected.empty())
          expected.pop_front();
        got.clear();
      }
    }
  };
  auto send = [&](const std::vector<uint8_t> &f) {
    ++frames;
    frame_words<Word>(f, [&](const Word &w) {
      in.write(w);
      step();
      drain();
    });
    for (int i = 0; i < 3; ++i)
    {
      step();
      drain();
    }
  };

  std::vector<uint8_t> p;
  uint8_t l[4];
  const uint8_t small[5] = {1, 2, 3, 4, 5};
  while (std::fread(l, 1, 4, packets) == 4)
  {
    const uint32_t n = l[0] | (l[1] << 8) | (l[2] << 16) | (uint32_t(l[3]) << 24);
    p.resize(n);
    if (std::fread(p.data(), 1, n, packets) != n)
      break;
    expected.push_back(p);
    send(make_frame(p.data(), n, kGroup344A, kPort344A, n_packets % 7 == 0));
    ++n_packets;
    if (n_packets % 100 == 0)
    {
      sent_noise += 6;
      send(make_frame(p.data(), n, kGroup344A + 1, kPort344A, false));          // another group
      send(make_frame(p.data(), n, kGroup344A, kPort344A + 1, true));           // another port
      send(make_frame(p.data(), n, kGroup344A, kPort344A, false, 0x0806));      // not IPv4
      send(make_frame(p.data(), n, kGroup344A, kPort344A, false, 0x0800, 6));   // not UDP
      send(std::vector<uint8_t>(30, 0x11));                                     // too short
      send(std::vector<uint8_t>(60, 0x22));
      expected.push_back(std::vector<uint8_t>(small, small + 5));
      send(make_frame(small, 5, kGroup344A, kPort344A, false));                 // padded
      expected.push_back(std::vector<uint8_t>(small, small + 5));
      send(make_frame(small, 5, kGroup344A, kPort344A, true));                  // padded, tagged
      // IGMP: another host's report is dropped; queries go to Igmp.
      ++sent_noise;
      send(igmp_frame(0x16, 0, kGroup344A, kGroup344A, kSwitchMac, kSwitchIp, true, true));
      for (int k = 0; k < 6; ++k)
      {
        const uint32_t g = k % 3 == 0 ? 0 : kGroup344A + k;
        query_groups.push_back(g);
        ++sent_queries;
        send(query(g, k & 1, k < 4));
      }
    }
  }
  for (int i = 0; i < 64; ++i)
  {
    step();
    drain();
  }
  std::printf("%llu packets in %llu frames, %u accepted, %u dropped, %llu differ, %zu not seen\n",
              (unsigned long long)n_packets, (unsigned long long)frames, rx.accepted, rx.dropped,
              (unsigned long long)diffs, expected.size());
  std::printf("%llu IGMP queries sent, %u passed to Igmp, %llu wrong, %zu not passed\n", (unsigned long long)sent_queries,
              rx.queries, (unsigned long long)query_errors, query_groups.size());
  const bool ok = diffs == 0 && expected.empty() && n_packets != 0 && rx.dropped == sent_noise && query_errors == 0 &&
                  query_groups.empty() && rx.queries == sent_queries && sent_queries != 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
