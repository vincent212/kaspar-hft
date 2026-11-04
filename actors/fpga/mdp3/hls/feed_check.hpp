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
#include <cstring>

#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

class FeedCheck
{
  struct L
  {
    long long px;
    int sz, ord;
  };
  L bid_[10] = {}, ask_[10] = {};
  uint32_t contract0_ = 0;   // events of contract 0 in this packet

  static void clear(L *s) { std::memset(s, 0, 10 * sizeof(L)); }

  void apply(uint64_t lo, uint64_t hi)
  {
    const unsigned action = (hi >> 60) & 7, level = (hi >> 56) & 15, contract = (hi >> 48) & 0xff;
    const bool offer = hi >> 63;
    ++events;
    if (contract == 0 || (action == kActionOther && level == kOtherChannelReset))
      ++contract0_;
    if (action == kActionTrade)
    {
      ++trades;
      return;
    }
    if (action == kActionOther)
    {
      if (level == kOtherChannelReset || (level == kOtherBookReset && contract == 0))
      {
        clear(bid_);
        clear(ask_);
      }
      return;
    }
    if (contract != 0 || level < 1 || level > 10)
      return;
    L *s = offer ? ask_ : bid_;
    const int i = level - 1;
    const L l{static_cast<long long>(lo), static_cast<int32_t>(hi), static_cast<uint16_t>(hi >> 32)};
    switch (action)
    {
    case 0:
      std::memmove(s + i + 1, s + i, (9 - i) * sizeof(L));
      s[i] = l;
      break;
    case 2:
      std::memmove(s + i, s + i + 1, (9 - i) * sizeof(L));
      s[9] = L{0, 0, 0};
      break;
    case 3:
      clear(s);
      break;
    case 4:
      std::memmove(s, s + i + 1, (9 - i) * sizeof(L));
      std::memset(s + 9 - i, 0, (i + 1) * sizeof(L));
      break;
    default:
      s[i] = l;
      break;
    }
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
    for (int i = 0; i < 10; ++i)
      at += std::snprintf(text + at, size - at, " %lld %d %d", bid_[i].px, bid_[i].sz, bid_[i].ord);
    at += std::snprintf(text + at, size - at, " a");
    for (int i = 0; i < 10; ++i)
      at += std::snprintf(text + at, size - at, " %lld %d %d", ask_[i].px, ask_[i].sz, ask_[i].ord);
    std::snprintf(text + at, size - at, "\n");
    return true;
  }
};

} // namespace mdp3fpga

namespace mdp3fpga {

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
