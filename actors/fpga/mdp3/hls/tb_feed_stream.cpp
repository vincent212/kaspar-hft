/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The g++ test of decoder, filter and batcher, for whole recordings. Holds one
// packet and one line. Reports the card's latency and how many messages the
// CPU actor gets.
//
//   MDP3_PACKETS  the packet file
//   MDP3_BOOK     the reference from ref_book, for contract 0

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include "feed_check.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;

void mdp3_feed_tick(WordStream &words, kfpga::Fifo &decoded, kfpga::Fifo &kept, kfpga::Fifo &out);

static bool read_packet(FILE *f, std::vector<uint8_t> &p)
{
  uint8_t l[4];
  if (std::fread(l, 1, 4, f) != 4)
    return false;
  const uint32_t n = l[0] | (l[1] << 8) | (l[2] << 16) | (uint32_t(l[3]) << 24);
  p.resize(n);
  return std::fread(p.data(), 1, n, f) == n;
}

int main()
{
  const char *packets_path = std::getenv("MDP3_PACKETS");
  const char *ref_path = std::getenv("MDP3_BOOK");
  FILE *packets = packets_path ? std::fopen(packets_path, "rb") : nullptr;
  FILE *ref = ref_path ? std::fopen(ref_path, "r") : nullptr;
  if (!packets || !ref)
  {
    std::printf("set MDP3_PACKETS and MDP3_BOOK\nFAIL\n");
    return 1;
  }
  WordStream words;
  kfpga::Fifo decoded, kept, out;
  FeedCheck feed;
  std::vector<uint8_t> p;
  std::deque<Word> wire;
  uint64_t n_packets = 0, n_ends = 0, n_compared = 0, diffs = 0, sum_after = 0, max_events = 0;
  uint32_t max_after = 0;
  std::vector<uint64_t> hist(1024, 0);
  char got[1024], want[1024];

  auto drain = [&]() {
    while (!out.empty())
    {
      PacketEnd end;
      const bool compare = feed.take(out.read(), end, got, sizeof(got));
      if (compare)
      {
        if (!std::fgets(want, sizeof(want), ref))
          std::strcpy(want, "(missing)\n");
        if (std::strcmp(got, want) != 0 && ++diffs <= 3)
          std::printf("end %llu\n  card: %s  ref:  %s", (unsigned long long)n_ends + 1, got, want);
        ++n_compared;
      }
      if (!feed.ended)
        continue;
      const uint32_t after = end.cycle_out - end.cycle_last;
      sum_after += after;
      max_after = after > max_after ? after : max_after;
      ++hist[after < hist.size() ? after : hist.size() - 1];
      max_events = end.events > max_events ? end.events : max_events;
      ++n_ends;
    }
  };
  auto tick = [&]() {
    mdp3_feed_tick(words, decoded, kept, out);
    drain();
  };

  while (read_packet(packets, p))
  {
    const size_t n = p.size();
    for (size_t at = 0; at < n || at == 0; at += 8)
    {
      Word w;
      w.data = 0;
      w.keep = 0;
      for (int i = 0; i < 8 && at + i < n; ++i)
      {
        w.data.range(8 * i + 7, 8 * i) = p[at + i];
        w.keep[i] = 1;
      }
      w.last = at + 8 >= n;
      wire.push_back(w);
    }
    while (!wire.empty())
    {
      if (words.empty())
      {
        words.write(wire.front());
        wire.pop_front();
      }
      tick();
    }
    // The gap between packets on the wire: 38 bytes or more of framing and
    // headers, 5 words. No more, so bursts arrive as they do on a 10 GbE link
    // running flat out, and this measures the card under them.
    for (int i = 0; i < 5; ++i)
      tick();
    ++n_packets;
  }
  for (int i = 0; i < 4096; ++i)
    tick();
  const bool extra = std::fgets(want, sizeof(want), ref) != nullptr;
  std::printf("%llu packets, %llu packet ends, %llu compared, %llu differ\n", (unsigned long long)n_packets,
              (unsigned long long)n_ends, (unsigned long long)n_compared, (unsigned long long)diffs);
  std::printf("CPU gets %llu events in %llu lines; most events in a packet %llu\n", (unsigned long long)feed.events,
              (unsigned long long)feed.lines, (unsigned long long)max_events);
  if (n_ends)
  {
    uint64_t c = 0;
    uint32_t p50 = 0, p99 = 0;
    for (uint32_t i = 0; i < hist.size(); ++i)
    {
      c += hist[i];
      if (!p50 && c * 2 >= n_ends)
        p50 = i;
      if (!p99 && c * 100 >= n_ends * 99)
        p99 = i;
    }
    std::printf("last word to PacketEnd: mean %.1f, p50 %u, p99 %u, max %u cycles\n", double(sum_after) / n_ends, p50,
                p99, max_after);
  }
  const bool ok = diffs == 0 && !extra && n_ends != 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
