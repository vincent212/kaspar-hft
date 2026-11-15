/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The g++ test of decoder and book, for whole recordings: every batch compared
// with the next line of the software book (test/ref_book), as it comes out.
// Holds one packet and one line.
//
//   MDP3_PACKETS  the packet file
//   MDP3_BOOK     the reference from ref_book

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include "mdp3fpga/book.hpp"
#include "mdp3fpga/decoder.hpp"

using namespace mdp3fpga;

void mdp3_tick(WordStream &words, kfpga::Fifo &link, kfpga::Fifo &out);

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
  kfpga::Fifo link, out;
  std::vector<uint8_t> p;
  std::deque<Word> feed;
  BookPart parts[kBookParts];
  uint64_t n_batches = 0, diffs = 0, sum_after_last = 0, sum_from_first = 0, n_packets = 0;
  uint32_t max_after_last = 0, events_in_batch = 0, evs = 0;
  std::vector<uint64_t> hist(4096, 0);
  char got[1024], want[1024];

  auto drain = [&]() {
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
        if (!std::fgets(want, sizeof(want), ref))
          std::strcpy(want, "(missing)\n");
        if (std::strcmp(got, want) != 0 && ++diffs <= 3)
          std::printf("batch %llu\n  card: %s  ref:  %s", (unsigned long long)n_batches + 1, got, want);
        const uint32_t after = m.cycle_out - m.cycle_last;
        sum_after_last += after;
        sum_from_first += m.cycle_out - m.cycle_in;
        max_after_last = after > max_after_last ? after : max_after_last;
        ++hist[after < hist.size() ? after : hist.size() - 1];
        ++n_batches;
        events_in_batch += m.events;
      }
      else
        ++evs;
    }
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
      feed.push_back(w);
    }
    // One word a clock; the clock runs until the decoder and book are idle, so
    // a batch never waits behind the next packet here.
    while (!feed.empty())
    {
      if (words.empty())
      {
        words.write(feed.front());
        feed.pop_front();
      }
      mdp3_tick(words, link, out);
      drain();
    }
    for (int i = 0; i < 40; ++i)
    {
      mdp3_tick(words, link, out);
      drain();
    }
    while (!link.empty())
    {
      mdp3_tick(words, link, out);
      drain();
    }
    for (int i = 0; i < 80; ++i)
    {
      mdp3_tick(words, link, out);
      drain();
    }
    ++n_packets;
  }
  const bool extra = std::fgets(want, sizeof(want), ref) != nullptr;
  std::printf("%llu packets, %llu batches, %llu differ, %u events in batches, %u forwarded\n",
              (unsigned long long)n_packets, (unsigned long long)n_batches, (unsigned long long)diffs,
              events_in_batch, evs);
  if (n_batches)
  {
    uint64_t c = 0;
    uint32_t p50 = 0, p99 = 0;
    for (uint32_t i = 0; i < hist.size(); ++i)
    {
      c += hist[i];
      if (!p50 && c * 2 >= n_batches)
        p50 = i;
      if (!p99 && c * 100 >= n_batches * 99)
        p99 = i;
    }
    std::printf("last word to BatchEnd: mean %.1f, p50 %u, p99 %u, max %u cycles; first word to BatchEnd: mean %.1f\n",
                double(sum_after_last) / n_batches, p50, p99, max_after_last, double(sum_from_first) / n_batches);
  }
  const bool ok = diffs == 0 && !extra && n_batches != 0 && evs == events_in_batch;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
