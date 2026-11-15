/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Packets as a newer schema version could send them: every root block of
// templates 46, 48, 30 and 4 made `root` bytes longer, and every entry of their
// first group `entry` bytes longer, with the block lengths on the wire updated to
// match. A decoder that steps by the wire's lengths, as CME's codec does, reads
// the same fields; one that steps by the schema's does not.
//
//   stretch_packets in.bin out.bin <root extra> <entry extra>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "dump.hpp"

static uint16_t u16(const std::vector<uint8_t> &p, size_t at) { return p[at] | (p[at + 1] << 8); }
static void put16(std::vector<uint8_t> &p, size_t at, uint16_t v)
{
  p[at] = v & 0xff;
  p[at + 1] = v >> 8;
}

int main(int argc, char **argv)
{
  if (argc != 5)
  {
    std::fprintf(stderr, "usage: stretch_packets in.bin out.bin <root extra> <entry extra>\n");
    return 2;
  }
  const size_t root_extra = std::atoi(argv[3]), entry_extra = std::atoi(argv[4]);
  const auto packets = mdp3test::read_packets(argv[1]);
  FILE *out = std::fopen(argv[2], "wb");
  if (!out)
  {
    std::perror(argv[2]);
    return 1;
  }
  size_t stretched = 0;
  for (const auto &p : packets)
  {
    std::vector<uint8_t> q(p.begin(), p.begin() + (p.size() < 12 ? p.size() : 12));
    size_t at = 12;
    while (at + 10 <= p.size())
    {
      const uint16_t size = u16(p, at), block = u16(p, at + 2), tid = u16(p, at + 4);
      if (size < 10 || at + size > p.size())
        break;
      const size_t end = at + size, body = at + 10;
      std::vector<uint8_t> m(p.begin() + at, p.begin() + body);
      const bool ours = tid == 46 || tid == 48 || tid == 30 || tid == 4;
      if (!ours || body + block > end)
      {
        q.insert(q.end(), p.begin() + at, p.begin() + end);
        at = end;
        continue;
      }
      put16(m, 2, static_cast<uint16_t>(block + root_extra));
      m.insert(m.end(), p.begin() + body, p.begin() + body + block);
      m.insert(m.end(), root_extra, 0);
      size_t g = body + block;
      if (tid != 30 && g + 3 <= end)
      {
        const uint16_t bl = u16(p, g);
        const uint8_t count = p[g + 2];
        m.push_back((bl + entry_extra) & 0xff);
        m.push_back((bl + entry_extra) >> 8);
        m.push_back(count);
        g += 3;
        for (int i = 0; i < count && g + bl <= end; ++i, g += bl)
        {
          m.insert(m.end(), p.begin() + g, p.begin() + g + bl);
          m.insert(m.end(), entry_extra, 0);
        }
      }
      m.insert(m.end(), p.begin() + g, p.begin() + end);   // the second group, as it was
      put16(m, 0, static_cast<uint16_t>(m.size()));
      q.insert(q.end(), m.begin(), m.end());
      ++stretched;
      at = end;
    }
    const uint32_t n = q.size();
    std::fwrite(&n, 4, 1, out);
    std::fwrite(q.data(), 1, q.size(), out);
  }
  std::fclose(out);
  std::fprintf(stderr, "%zu packets, %zu messages stretched\n", packets.size(), stretched);
  return 0;
}
