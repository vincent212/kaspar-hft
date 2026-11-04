/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for mdp3_feed_top, for co-simulation: queues every packet of a
// packet file, then checks each PacketEnd against the software book (ref_book).
//
//   MDP3_PACKETS  the packet file
//   MDP3_BOOK     the reference from ref_book, for contract 0

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../test/dump.hpp"
#include "feed_check.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;

void mdp3_feed_top(InStream &in, kfpga::Fifo &out, uint32_t n_words, uint32_t n_out);

int main()
{
  const char *packets_path = std::getenv("MDP3_PACKETS");
  const char *ref_path = std::getenv("MDP3_BOOK");
  if (!packets_path || !ref_path)
  {
    std::printf("set MDP3_PACKETS and MDP3_BOOK\nFAIL\n");
    return 1;
  }
  const auto packets = mdp3test::read_packets(packets_path);
  std::vector<std::string> ref;
  uint32_t n_out = 0;
  {
    FILE *f = std::fopen(ref_path, "r");
    char line[1024];
    while (f && std::fgets(line, sizeof(line), f))
    {
      unsigned seq, gap, events;
      std::sscanf(line, "Z %u %u %u", &seq, &gap, &events);
      normalise(line);
      ref.push_back(line);
      n_out += (events + kEventsPerLine - 1) / kEventsPerLine;
    }
  }
  // Each sequence gap in the file is one more envelope.
  {
    bool seen = false;
    uint32_t next = 0;
    for (const auto &p : packets)
    {
      if (p.size() < 12)
        continue;
      const uint32_t seq = p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
      if (seen && seq != next)
        ++n_out;
      seen = true;
      next = seq + 1;
    }
  }
  InStream in;
  kfpga::Fifo out;
  uint32_t n_words = 0;
  for (const auto &p : packets)
  {
    const size_t n = p.size();
    for (size_t at = 0; at < n || at == 0; at += 8)
    {
      InWord w;
      w.data = 0;
      w.keep = 0;
      for (int i = 0; i < 8 && at + i < n; ++i)
      {
        w.data.range(8 * i + 7, 8 * i) = p[at + i];
        w.keep[i] = 1;
      }
      w.last = at + 8 >= n;
      in.write(w);
      ++n_words;
      if (n == 0)
        break;
    }
  }
  mdp3_feed_top(in, out, n_words, n_out);

  FeedCheck feed;
  size_t ends = 0, diffs = 0;
  char got[1024];
  while (!out.empty())
  {
    if (!feed.take(out.read(), got, sizeof(got)))
      continue;
    if (ends >= ref.size() || ref[ends] != got)
    {
      if (++diffs <= 3)
        std::printf("end %zu\n  card: %s", ends + 1, got);
    }
    ++ends;
  }
  std::printf("%zu packets, %zu packet ends of %zu, %zu differ, %llu events, %llu lines\n", packets.size(), ends,
              ref.size(), diffs, (unsigned long long)feed.events, (unsigned long long)feed.lines);
  const bool ok = diffs == 0 && ends == ref.size() && ends != 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
