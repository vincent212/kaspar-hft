/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The reference: decodes a packet file with CME's real-logic codec
// (mdp3_sbe/, what msg_decoder uses) and prints every event as dump.hpp lines.
// The packet walk is DataDecoder::mbo_data's.
//
//   ref_dump packets.bin > ref.txt

#include <cstdio>
#include <cstring>

#include "dump.hpp"
#include "mdp3_sbe/ChannelReset4.h"
#include "mdp3_sbe/MDIncrementalRefreshBook46.h"
#include "mdp3_sbe/MDIncrementalRefreshTradeSummary48.h"
#include "mdp3_sbe/MessageHeader.h"
#include "mdp3_sbe/SecurityStatus30.h"

using namespace mdp3test;

static void packet(FILE *f, std::vector<uint8_t> &p)
{
  char *buf = reinterpret_cast<char *>(p.data());
  const size_t len = p.size();
  uint32_t seq = 0;
  uint64_t sending = 0;
  if (len >= 12)
  {
    std::memcpy(&seq, buf, 4);
    std::memcpy(&sending, buf + 4, 8);
  }
  unsigned messages = 0, events = 0;
  size_t at = 12;
  while (at + 10 <= len)
  {
    uint16_t msg_size;
    std::memcpy(&msg_size, buf + at, 2);
    if (msg_size < 10 || at + msg_size > len)
      break;
    sbe::MessageHeader h(buf, at + 2, len, 0);
    const uint16_t block = h.blockLength(), tid = h.templateId(), version = h.version();
    ++messages;
    if (h.schemaId() == 1)
    {
      if (tid == sbe::MDIncrementalRefreshBook46::sbeTemplateId())
      {
        sbe::MDIncrementalRefreshBook46 m;
        m.wrapForDecode(buf, at + 10, block, version, at + msg_size);
        const unsigned match = m.matchEventIndicator().rawValue();
        auto &g = m.noMDEntries();
        while (g.hasNext())
        {
          g.next();
          book(f, seq, m.transactTime(), g.mDEntryPx().mantissa(), g.mDEntrySize(), g.securityID(), g.rptSeq(),
               g.numberOfOrders(), g.mDPriceLevel(), g.mDUpdateActionRaw(),
               static_cast<uint8_t>(g.mDEntryTypeRaw()), match, g.tradeableSize());
          ++events;
        }
      }
      else if (tid == sbe::MDIncrementalRefreshTradeSummary48::sbeTemplateId())
      {
        sbe::MDIncrementalRefreshTradeSummary48 m;
        m.wrapForDecode(buf, at + 10, block, version, at + msg_size);
        const unsigned match = m.matchEventIndicator().rawValue();
        auto &g = m.noMDEntries();
        while (g.hasNext())
        {
          g.next();
          trade(f, seq, m.transactTime(), g.mDEntryPx().mantissa(), g.mDEntrySize(), g.securityID(), g.rptSeq(),
                g.numberOfOrders(), g.aggressorSideRaw(), g.mDUpdateActionRaw(), match, g.mDTradeEntryID());
          ++events;
        }
      }
      else if (tid == sbe::SecurityStatus30::sbeTemplateId())
      {
        sbe::SecurityStatus30 m;
        m.wrapForDecode(buf, at + 10, block, version, at + msg_size);
        status(f, seq, m.transactTime(), m.securityID(), m.tradeDate(), m.securityTradingStatusRaw(),
               m.haltReasonRaw(), m.securityTradingEventRaw(), m.matchEventIndicator().rawValue());
        ++events;
      }
      else if (tid == sbe::ChannelReset4::sbeTemplateId())
      {
        sbe::ChannelReset4 m;
        m.wrapForDecode(buf, at + 10, block, version, at + msg_size);
        const unsigned match = m.matchEventIndicator().rawValue();
        auto &g = m.noMDEntries();
        while (g.hasNext())
        {
          g.next();
          reset(f, seq, m.transactTime(), g.applID(), match);
          ++events;
        }
      }
    }
    at += msg_size;
  }
  end_of_packet(f, seq, sending, messages, events, static_cast<unsigned>(len));
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::fprintf(stderr, "usage: ref_dump packets.bin\n");
    return 2;
  }
  auto packets = read_packets(argv[1]);
  if (packets.empty())
  {
    std::fprintf(stderr, "ref_dump: no packets in %s\n", argv[1]);
    return 1;
  }
  for (auto &p : packets)
    packet(stdout, p);
  return 0;
}
