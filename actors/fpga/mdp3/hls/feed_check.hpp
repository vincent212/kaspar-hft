#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The CPU end for the testbenches: what FpgaFeed will do, in a few lines. Takes
// the card's lines, applies the events to a book of contract 0, and at each
// PacketEnd prints the line ref_book prints, for comparing.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "mdp3fpga/books.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

class FeedCheck
{
  Books books_{kContracts};
  uint32_t contract0_ = 0;   // events of contract 0 in this packet

  void apply(uint64_t lo, uint64_t hi)
  {
    const Event e{lo, hi};
    ++events;
    if (e.contract() == 0 || (e.action() == kActionOther && e.level() == kOtherChannelReset))
      ++contract0_;
    if (e.action() == kActionTrade)
      ++trades;
    books_.apply(e);
  }

public:
  uint64_t events = 0, trades = 0, lines = 0;
  bool ended = false;     // the last take() was a packet's End line
  bool gap = false;       // a SeqGap has come

  // Returns true at the End line of a packet that touched contract 0, with the
  // line ref_book prints in `text`. The card sends no sequence number, so that
  // field is 0: see normalise().
  bool take(const kfpga::Envelope &e, char *text, size_t size)
  {
    ended = false;
    if (e.msg == WantAck::id)
      return false;
    if (e.msg == SeqGap::id)
    {
      gap = true;
      ++lines;
      return false;
    }
    ++lines;
    if (e.msg < kFirstLine || e.msg > kLastEndLine)
    {
      std::snprintf(text, size, "? msg %u\n", e.msg);
      return true;
    }
    const int n = (e.msg - kFirstLine) % 3 + 1;
    const EventLine<3, false> m = kfpga::decode<EventLine<3, false>>(e);
    const uint64_t lo[3] = {m.lo0, m.lo1, m.lo2}, hi[3] = {m.hi0, m.hi1, m.hi2};
    for (int i = 0; i < n; ++i)
      apply(lo[i], hi[i]);
    if (e.msg < kFirstEndLine)
      return false;
    ended = true;
    if (contract0_ == 0)
      return false;
    int at = std::snprintf(text, size, "Z 0 %u %u b", gap ? 1 : 0, contract0_);
    contract0_ = 0;
    const Book &b = books_.book(0);
    for (int i = 0; i < kLevels; ++i)
      at += std::snprintf(text + at, size - at, " %lld %d %d", b.bid[i].price, b.bid[i].size, b.bid[i].orders);
    at += std::snprintf(text + at, size - at, " a");
    for (int i = 0; i < kLevels; ++i)
      at += std::snprintf(text + at, size - at, " %lld %d %d", b.ask[i].price, b.ask[i].size, b.ask[i].orders);
    std::snprintf(text + at, size - at, "\n");
    return true;
  }
};

} // namespace mdp3fpga

namespace mdp3fpga {

// The table from MDP3_CONTRACTS, a comma list of security ids, as envelopes from
// the CPU. Contract 0 is the first.
inline std::vector<kfpga::Envelope> table_from_env()
{
  std::vector<kfpga::Envelope> out;
  const char *list = std::getenv("MDP3_CONTRACTS");
  for (uint32_t i = 0; list && *list; ++i)
  {
    WantContract m;
    m.index = i;
    m.security_id = std::atoi(list);
    out.push_back(kfpga::encode(m, kFeed, kFilter));
    list = std::strchr(list, ',');
    list = list ? list + 1 : nullptr;
  }
  return out;
}

// A ref_book line with its sequence number zeroed, as FeedCheck prints it.
inline void normalise(char *line)
{
  char *a = std::strchr(line, ' ');
  char *b = a ? std::strchr(a + 1, ' ') : nullptr;
  if (b)
  {
    a[1] = '0';
    std::memmove(a + 2, b, std::strlen(b) + 1);
  }
}

} // namespace mdp3fpga
