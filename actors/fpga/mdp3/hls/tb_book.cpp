/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for mdp3_book_top, for co-simulation: queues every packet of a
// packet file, then checks each batch against the software book (ref_book).
//
//   MDP3_PACKETS  the packet file
//   MDP3_BOOK     the reference from ref_book

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../test/dump.hpp"
#include "mdp3fpga/book.hpp"
#include "mdp3fpga/decoder.hpp"

using namespace mdp3fpga;

void mdp3_book_top(InStream &in, kfpga::Fifo &out, uint32_t n_words, uint32_t n_out);

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
      ref.push_back(line);
      n_out += events + kBookParts + 1;
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
  mdp3_book_top(in, out, n_words, n_out);

  BookPart parts[kBookParts];
  size_t batch = 0, diffs = 0;
  uint64_t sum = 0;
  uint32_t worst = 0;
  while (!out.empty())
  {
    const kfpga::Envelope e = out.read();
    if (e.msg == BookPart::id)
    {
      const BookPart m = kfpga::decode<BookPart>(e);
      parts[m.part] = m;
    }
    else if (e.msg == BatchEnd::id)
    {
      const BatchEnd m = kfpga::decode<BatchEnd>(e);
      char got[1024];
      int at = std::snprintf(got, sizeof(got), "Z %u %u %u b", m.seq, m.gap, m.events);
      for (int i = 0; i < kBookParts; ++i)
      {
        if (i == kBookParts / 2)
          at += std::snprintf(got + at, sizeof(got) - at, " a");
        at += std::snprintf(got + at, sizeof(got) - at, " %lld %d %d %lld %d %d", (long long)parts[i].price0,
                            parts[i].size0, parts[i].orders0, (long long)parts[i].price1, parts[i].size1,
                            parts[i].orders1);
      }
      std::strcat(got, "\n");
      if (batch >= ref.size() || ref[batch] != got)
        ++diffs;
      const uint32_t after = m.cycle_out - m.cycle_last;
      sum += after;
      worst = after > worst ? after : worst;
      ++batch;
    }
  }
  std::printf("%zu packets, %zu batches of %zu, %zu differ\n", packets.size(), batch, ref.size(), diffs);
  if (batch)
    std::printf("last word to BatchEnd: mean %.1f, max %u cycles\n", double(sum) / batch, worst);
  const bool ok = diffs == 0 && batch == ref.size() && batch != 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
