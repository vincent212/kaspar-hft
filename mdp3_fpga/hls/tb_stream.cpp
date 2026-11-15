/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The g++ test, for whole recordings: one packet at a time through the
// decoder's step, each event compared with the next reference line as it comes
// out. Holds one packet and one line, so memory does not grow with the file.
// Feeds a word whenever the decoder has none waiting, one a step, as the wire
// does at full rate.
//
//   MDP3_PACKETS  the packet file
//   MDP3_REF      the reference dump

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "mdp3fpga/decoder.hpp"
#include "print_event.hpp"

using namespace mdp3fpga;

void mdp3_step(WordStream &words, kfpga::Fifo &out);

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
  const char *ref_path = std::getenv("MDP3_REF");
  FILE *packets = packets_path ? std::fopen(packets_path, "rb") : nullptr;
  FILE *ref = ref_path ? std::fopen(ref_path, "r") : nullptr;
  if (!packets || !ref)
  {
    std::printf("set MDP3_PACKETS and MDP3_REF\nFAIL\n");
    return 1;
  }

  WordStream words;
  kfpga::Fifo out;
  std::vector<uint8_t> p;
  uint64_t n_packets = 0, n_lines = 0, diffs = 0, total_cycles = 0, total_bytes = 0;
  uint32_t max_cycles = 0;
  char got[512], want[512];

  while (read_packet(packets, p))
  {
    const size_t n = p.size();
    size_t at = 0;
    bool fed = false, done = false;
    while (!done)
    {
      if (!fed && words.empty())
      {
        Word w;
        w.data = 0;
        w.keep = 0;
        for (int i = 0; i < 8 && at + i < n; ++i)
        {
          w.data.range(8 * i + 7, 8 * i) = p[at + i];
          w.keep[i] = 1;
        }
        at += 8;
        fed = at >= n;
        w.last = fed;
        words.write(w);
      }
      mdp3_step(words, out);
      while (!out.empty())
      {
        const kfpga::Envelope e = out.read();
        FILE *f = fmemopen(got, sizeof(got), "w");
        print_event(f, e);
        std::fclose(f);
        ++n_lines;
        if (!std::fgets(want, sizeof(want), ref))
          std::strcpy(want, "(missing)\n");
        if (std::strcmp(got, want) != 0 && ++diffs <= 5)
          std::printf("line %llu\n  card: %s  ref:  %s", (unsigned long long)n_lines, got, want);
        if (e.msg == EndOfPacket::id)
        {
          const EndOfPacket m = kfpga::decode<EndOfPacket>(e);
          const uint32_t cycles = m.cycle_out - m.cycle_in;
          total_cycles += cycles;
          total_bytes += m.bytes;
          max_cycles = cycles > max_cycles ? cycles : max_cycles;
          done = true;
        }
      }
    }
    ++n_packets;
  }
  const bool extra = std::fgets(want, sizeof(want), ref) != nullptr;
  std::printf("%llu packets, %llu lines, %llu differ\n", (unsigned long long)n_packets,
              (unsigned long long)n_lines, (unsigned long long)diffs);
  if (n_packets)
    std::printf("first word to EndOfPacket: mean %.1f cycles, max %u; mean packet %.1f bytes (%.1f words)\n",
                double(total_cycles) / n_packets, max_cycles, double(total_bytes) / n_packets,
                double(total_bytes) / n_packets / 8);
  const bool ok = diffs == 0 && !extra && n_packets != 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
