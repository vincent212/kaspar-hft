/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The g++ test of FeedRx, decoder, filter and batcher, for whole recordings, frames
// paced as 10 GbE delivers them to a design clocked at 250 MHz: 156.25 MHz words,
// one per 1.6 clocks, 3 words of gap between frames, nothing idle otherwise, so
// recorded bursts arrive as they would at full line rate. Holds one packet and one
// line. Reports latency from the last byte of a frame reaching the card to its End
// line leaving, queues included, and how many messages the CPU actor gets.
//
//   MDP3_PACKETS  the packet file
//   MDP3_BOOK     the reference from ref_book, for security index 0
//   MDP3_SECURITIES  the filter's table, a comma list of security ids

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

#include "feed_check.hpp"
#include "frames.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;

void mdp3_feed_tick(WordStream &frames, WordStream &words, kfpga::Fifo &decoded, kfpga::Fifo &kept, kfpga::Fifo &window, kfpga::Fifo &acks,
                    kfpga::Fifo &out);

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
  WordStream frames, words;
  kfpga::Fifo decoded, kept, window, acks, out;
  FeedCheck feed;
  std::vector<uint8_t> p;
  std::deque<Word> wire;
  uint64_t n_packets = 0, n_ends = 0, n_compared = 0, diffs = 0, sum_after = 0;
  uint32_t max_after = 0;
  std::vector<uint64_t> hist(1024, 0);
  char got[1024], want[1024];

  std::deque<std::pair<uint32_t, uint64_t>> arrived;   // each frame's sequence number and last word's clock
  uint64_t ticks = 0;
  size_t most_queued = 0;
  auto drain = [&]() {
    while (!out.empty())
    {
      const bool compare = feed.take(out.read(), got, sizeof(got));
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
      while (!arrived.empty() && arrived.front().first != feed.seq)
        arrived.pop_front();
      const uint32_t after = arrived.empty() ? 0 : static_cast<uint32_t>(ticks - arrived.front().second);
      sum_after += after;
      max_after = after > max_after ? after : max_after;
      ++hist[after < hist.size() ? after : hist.size() - 1];
      ++n_ends;
    }
  };
  auto tick = [&]() {
    ++ticks;
    most_queued = words.size() > most_queued ? words.size() : most_queued;
    mdp3_feed_tick(frames, words, decoded, kept, window, acks, out);
    drain();
  };

  // Load the table, as FpgaFeed does: no packets until every entry is acked.
  const auto table = table_from_env();
  for (const auto &e : table)
    window.write(e);
  for (size_t got_acks = 0; got_acks < table.size();)
  {
    tick();
    while (!acks.empty())
    {
      acks.read();
      ++got_acks;
    }
  }

  uint32_t credit = 0;
  while (read_packet(packets, p))
  {
    mdp3test::frame_words<Word>(mdp3test::make_frame(p.data(), p.size(), mdp3test::kGroup344A, mdp3test::kPort344A, false),
                                [&](const Word &w) { wire.push_back(w); });
    const uint32_t seq = p.size() >= 4 ? p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24) : 0;
    while (!wire.empty())
    {
      credit += 5;   // a word per 1.6 clocks
      if (credit >= 8)
      {
        credit -= 8;
        const Word w = wire.front();
        wire.pop_front();
        frames.write(w);
        if (w.last)
          arrived.push_back({seq, ticks});
      }
      tick();
    }
    // Preamble and interframe gap, 20 bytes, 3 words.
    for (int i = 0; i < 5; ++i)
      tick();
    ++n_packets;
  }
  while (!frames.empty() || !words.empty() || !decoded.empty() || !kept.empty())
    tick();
  for (int i = 0; i < 256; ++i)
    tick();
  const bool extra = std::fgets(want, sizeof(want), ref) != nullptr;
  std::printf("%llu packets, %llu packet ends, %llu compared, %llu differ\n", (unsigned long long)n_packets,
              (unsigned long long)n_ends, (unsigned long long)n_compared, (unsigned long long)diffs);
  std::printf("CPU gets %llu events in %llu lines\n", (unsigned long long)feed.events,
              (unsigned long long)feed.lines);
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
    std::printf("last byte in to End line: mean %.1f, p50 %u, p99 %u, max %u clocks; most words queued %zu\n",
                double(sum_after) / n_ends, p50, p99, max_after, most_queued);
  }
  const bool ok = diffs == 0 && !extra && n_ends != 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
