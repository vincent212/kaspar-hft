/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Time FpgaFeed's work for one packet of 37 events, the longest in the 344 session: copy
// them out of 13 lines into the buffer, then apply all 37 to the books. Not the lane
// polling or the decode. Run pinned to one core: make bench.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include "mdp3fpga/books.hpp"
using namespace mdp3fpga;
int main()
{
  const int kEvents = 37, kSec = 6, kRuns = 200000;
  std::mt19937 rng(7);
  std::vector<std::vector<Event>> packets(1024);
  for (auto &p : packets)
    for (int i = 0; i < kEvents; ++i)
    {
      const uint64_t action = rng() % 10 < 6 ? 1 : (rng() % 2 ? 0 : 2);   // mostly change, some new and delete
      const uint64_t lvl = 1 + rng() % 3, sec = rng() % kSec, offer = rng() % 2;
      const uint64_t hi = (uint64_t(1 + rng() % 500)) | (uint64_t(1 + rng() % 40) << 32) | (sec << 48) | (lvl << 56) |
                          (action << 60) | (offer << 63);
      p.push_back(Event{110000000000ull + (rng() % 100) * 7812500ull, hi});
    }
  Books books(kSec);
  Event buf[64];
  volatile long long sink = 0;
  std::vector<double> ns;
  ns.reserve(kRuns);
  for (int r = 0; r < kRuns; ++r)
  {
    const auto &p = packets[r & 1023];
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kEvents; ++i)   // what on_line / on_end do: copy into the buffer
      buf[i] = p[i];
    for (int i = 0; i < kEvents; ++i)   // what packet_end does: apply each
      books.apply(buf[i]);
    sink = sink + books.book(0).bid[0].price;
    const auto t1 = std::chrono::steady_clock::now();
    ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
  }
  std::sort(ns.begin(), ns.end());
  std::printf("37 events: p50 %.0f ns, p99 %.0f ns, max %.0f ns (%.1f ns per event at p50)\n", ns[ns.size() / 2],
              ns[ns.size() * 99 / 100], ns.back(), ns[ns.size() / 2] / kEvents);
}
