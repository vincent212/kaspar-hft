#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * One text line per decoded event, shared by the reference (ref_dump.cpp,
 * CME's real-logic codec) and the HLS testbench, so the two can be compared
 * line for line. Raw wire values throughout.
 *
 *   B seq time price size secid rptseq orders level action type match tradeable
 *   T seq time price size secid rptseq orders aggressor action match tradeid
 *   S seq time secid tradedate status halt event match
 *   R seq time applid match
 *   P seq sendingtime messages events bytes        (end of packet)
 *
 * Packet files: each packet is a 4-byte little-endian length and the UDP
 * payload (the MDP3 packet).
 */

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace mdp3test {

inline void book(FILE *f, uint32_t seq, uint64_t time, int64_t px, int32_t sz, int32_t secid, uint32_t rptseq,
                 int32_t orders, unsigned level, unsigned action, unsigned type, unsigned match, int32_t tradeable)
{
  std::fprintf(f, "B %u %" PRIu64 " %" PRId64 " %d %d %u %d %u %u %u %u %d\n", seq, time, px, sz, secid, rptseq,
               orders, level, action, type, match, tradeable);
}

inline void trade(FILE *f, uint32_t seq, uint64_t time, int64_t px, int32_t sz, int32_t secid, uint32_t rptseq,
                  int32_t orders, unsigned aggressor, unsigned action, unsigned match, uint32_t trade_id)
{
  std::fprintf(f, "T %u %" PRIu64 " %" PRId64 " %d %d %u %d %u %u %u %u\n", seq, time, px, sz, secid, rptseq, orders,
               aggressor, action, match, trade_id);
}

inline void status(FILE *f, uint32_t seq, uint64_t time, int32_t secid, unsigned trade_date, unsigned st,
                   unsigned halt, unsigned event, unsigned match)
{
  std::fprintf(f, "S %u %" PRIu64 " %d %u %u %u %u %u\n", seq, time, secid, trade_date, st, halt, event, match);
}

inline void reset(FILE *f, uint32_t seq, uint64_t time, int appl_id, unsigned match)
{
  std::fprintf(f, "R %u %" PRIu64 " %d %u\n", seq, time, appl_id, match);
}

inline void end_of_packet(FILE *f, uint32_t seq, uint64_t sending, unsigned messages, unsigned events,
                          unsigned bytes)
{
  std::fprintf(f, "P %u %" PRIu64 " %u %u %u\n", seq, sending, messages, events, bytes);
}

// Every packet in a packet file.
inline std::vector<std::vector<uint8_t>> read_packets(const char *path)
{
  std::vector<std::vector<uint8_t>> out;
  FILE *f = std::fopen(path, "rb");
  if (!f)
    return out;
  uint8_t l[4];
  while (std::fread(l, 1, 4, f) == 4)
  {
    const uint32_t n = l[0] | (l[1] << 8) | (l[2] << 16) | (uint32_t(l[3]) << 24);
    std::vector<uint8_t> p(n);
    if (std::fread(p.data(), 1, n, f) != n)
      break;
    out.push_back(std::move(p));
  }
  std::fclose(f);
  return out;
}

} // namespace mdp3test
