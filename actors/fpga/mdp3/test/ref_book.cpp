/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The reference for BookBank: a software MBP book, written apart from it on
// plain vectors, fed from the event dump of CME's codec (ref_dump). One line
// per packet that touched the book, and it counts the times the book crosses.
//
//   ref_book in.ref out.book <security id>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct L
{
  long long px;
  int sz, ord;
};
using Side = std::vector<L>;

static void put(FILE *f, const Side &s)
{
  for (int i = 0; i < 10; ++i)
    if (i < static_cast<int>(s.size()))
      std::fprintf(f, " %lld %d %d", s[i].px, s[i].sz, s[i].ord);
    else
      std::fprintf(f, " 0 0 0");
}

// A side as levels 1 to 10: an empty level holds its place.
static void set(Side &s, size_t at, const L &l)
{
  if (s.size() <= at)
    s.resize(at + 1, L{0, 0, 0});
  s[at] = l;
}

int main(int argc, char **argv)
{
  if (argc != 4)
  {
    std::fprintf(stderr, "usage: ref_book in.ref out.book <security id>\n");
    return 2;
  }
  FILE *in = std::fopen(argv[1], "r");
  FILE *out = std::fopen(argv[2], "w");
  const long sec = std::atol(argv[3]);
  if (!in || !out)
  {
    std::perror("open");
    return 1;
  }
  Side bid, ask;
  bool touched = false, gap = false, seen = false;
  unsigned long long last = 0, events = 0, batches = 0, crossed = 0, unordered = 0, entries = 0;
  char line[512];
  while (std::fgets(line, sizeof(line), in))
  {
    if (line[0] == 'B')
    {
      unsigned long long seq, time;
      long long px;
      int sz, ord;
      long id;
      unsigned long rpt;
      unsigned lvl, act, type, match;
      int tradeable;
      std::sscanf(line, "B %llu %llu %lld %d %ld %lu %d %u %u %u %u %d", &seq, &time, &px, &sz, &id, &rpt, &ord, &lvl,
                  &act, &type, &match, &tradeable);
      if (id != sec)
        continue;
      if (type == 'J')
      {
        bid.clear();
        ask.clear();
        ++events;
        touched = true;
        continue;
      }
      if (type != '0' && type != '1')
        continue;
      ++events;
      ++entries;
      touched = true;
      if (lvl < 1 || lvl > 10)
        continue;
      Side &s = type == '0' ? bid : ask;
      const size_t at = lvl - 1;
      const L l{px, sz, ord};
      if (act == 0)
      {
        if (s.size() < at)
          s.resize(at, L{0, 0, 0});
        s.insert(s.begin() + at, l);
      }
      else if (act == 2)
      {
        if (at < s.size())
          s.erase(s.begin() + at);
      }
      else if (act == 3)
        s.clear();
      else if (act == 4)
        s.erase(s.begin(), s.begin() + std::min<size_t>(lvl, s.size()));
      else
        set(s, at, l);
      if (s.size() > 10)
        s.resize(10);
    }
    else if (line[0] == 'T' || line[0] == 'S')
    {
      unsigned long long seq, time;
      long id;
      long long px;
      int sz;
      if (line[0] == 'T')
      {
        std::sscanf(line, "T %llu %llu %lld %d %ld", &seq, &time, &px, &sz, &id);
      }
      else
        std::sscanf(line, "S %llu %llu %ld", &seq, &time, &id);
      if (id == sec)
      {
        ++events;
        touched = true;
      }
    }
    else if (line[0] == 'R')
    {
      bid.clear();
      ask.clear();
      ++events;
      touched = true;
    }
    else if (line[0] == 'P')
    {
      unsigned long long seq;
      std::sscanf(line, "P %llu", &seq);
      if (seen && seq != last + 1)
        gap = true;
      seen = true;
      last = seq;
      if (touched)
      {
        std::fprintf(out, "Z %llu %d %llu b", seq, gap ? 1 : 0, events);
        put(out, bid);
        std::fprintf(out, " a");
        put(out, ask);
        std::fprintf(out, "\n");
        ++batches;
        if (!bid.empty() && !ask.empty() && bid[0].sz && ask[0].sz && bid[0].px >= ask[0].px)
          ++crossed;
        for (size_t i = 1; i < bid.size(); ++i)
          if (bid[i].sz && bid[i - 1].sz && bid[i].px >= bid[i - 1].px)
          {
            ++unordered;
            break;
          }
        for (size_t i = 1; i < ask.size(); ++i)
          if (ask[i].sz && ask[i - 1].sz && ask[i].px <= ask[i - 1].px)
          {
            ++unordered;
            break;
          }
        events = 0;
        touched = false;
      }
    }
  }
  std::fprintf(stderr, "reference: %llu entries, %llu batches, %llu crossed, %llu out of order, gap %d\n", entries,
               batches, crossed, unordered, gap ? 1 : 0);
  std::fclose(out);
  return 0;
}
