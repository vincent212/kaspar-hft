#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The CPU end for the testbenches: what FpgaFeed will do, in a few lines. Takes
// the card's lines, applies the events to a book of security index 0, and at each
// End line prints the line ref_book prints, for comparing.

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
  Books books_{kSecurities};
  uint32_t security0_ = 0;   // events of security index 0 in this packet

  void apply(uint64_t lo, uint64_t hi)
  {
    const Event e{lo, hi};
    ++events;
    if (e.security_index() == 0 || e.is_status() || (e.action() == kActionOther && e.level() == kOtherChannelReset))
      ++security0_;
    if (e.action() == kActionTrade)
      ++trades;
    books_.apply(e);
  }

public:
  uint64_t events = 0, trades = 0, lines = 0;
  bool ended = false;     // the last take() was an End line
  bool gap = false;       // the sequence check has seen a gap
  uint32_t seq = 0;       // the last End line's sequence number
  bool seen_ = false;
  uint32_t next_ = 0;

  // Returns true at the End line of a packet that touched security index 0, with the
  // line ref_book prints in `text`.
  bool take(const kfpga::Envelope &e, char *text, size_t size)
  {
    ended = false;
    if (e.msg == WantAck::id)
      return false;
    ++lines;
    if (e.msg == EventLine::id)
    {
      const EventLine m = kfpga::decode<EventLine>(e);
      apply(m.lo0, m.hi0);
      apply(m.lo1, m.hi1);
      apply(m.lo2, m.hi2);
      return false;
    }
    if (e.msg < kFirstEndLine || e.msg > kLastEndLine)
    {
      std::snprintf(text, size, "? msg %u\n", e.msg);
      return true;
    }
    const int n = e.msg - kFirstEndLine;
    const EndLine<2> m = kfpga::decode<EndLine<2>>(e);
    if (n > 0)
      apply(m.lo0, m.hi0);
    if (n > 1)
      apply(m.lo1, m.hi1);
    ended = true;
    seq = m.seq;
    if (seen_ && m.seq != next_)
      gap = true;
    seen_ = true;
    next_ = m.seq + 1;
    if (security0_ == 0)
      return false;
    int at = std::snprintf(text, size, "Z %u %u %u b", m.seq, gap ? 1 : 0, security0_);
    security0_ = 0;
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

// The table from MDP3_SECURITIES, a comma list of security ids, as envelopes from
// the CPU. Security 0 is the first.
inline std::vector<kfpga::Envelope> table_from_env()
{
  std::vector<kfpga::Envelope> out;
  const char *list = std::getenv("MDP3_SECURITIES");
  for (uint32_t i = 0; list && *list; ++i)
  {
    WantSecurity m;
    m.index = i;
    m.security_id = std::atoi(list);
    out.push_back(kfpga::encode(m, kCpuFeed, kFilter));
    list = std::strchr(list, ',');
    list = list ? list + 1 : nullptr;
  }
  return out;
}

} // namespace mdp3fpga
