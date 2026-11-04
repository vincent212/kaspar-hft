#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// One decoder event as a dump.hpp line, for the testbenches.

#include <cstdio>

#include "../test/dump.hpp"
#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

inline void print_event(FILE *f, const kfpga::Envelope &e)
{
  switch (e.msg)
  {
  case BookEntry::id:
  {
    const BookEntry m = kfpga::decode<BookEntry>(e);
    mdp3test::book(f, m.seq, m.transact_time, m.price, m.size, m.security_id, m.rpt_seq, m.orders, m.level(),
                   m.action(), m.entry_type(), m.match_event(), m.tradeable_size);
    break;
  }
  case Trade::id:
  {
    const Trade m = kfpga::decode<Trade>(e);
    mdp3test::trade(f, m.seq, m.transact_time, m.price, m.size, m.security_id, m.rpt_seq, m.orders, m.aggressor(),
                    m.action(), m.match_event(), m.trade_id);
    break;
  }
  case SecurityStatus::id:
  {
    const SecurityStatus m = kfpga::decode<SecurityStatus>(e);
    mdp3test::status(f, m.seq, m.transact_time, m.security_id, m.trade_date, m.flags & 0xff, (m.flags >> 8) & 0xff,
                     (m.flags >> 16) & 0xff, m.flags >> 24);
    break;
  }
  case ChannelReset::id:
  {
    const ChannelReset m = kfpga::decode<ChannelReset>(e);
    mdp3test::reset(f, m.seq, m.transact_time, m.appl_id, m.match_event);
    break;
  }
  case EndOfPacket::id:
  {
    const EndOfPacket m = kfpga::decode<EndOfPacket>(e);
    mdp3test::end_of_packet(f, m.seq, m.sending_time, m.messages, m.events, m.bytes);
    break;
  }
  case SeqGap::id:   // not in the reference
    break;
  default:
    std::fprintf(f, "? msg %u\n", e.msg);
  }
}

} // namespace mdp3fpga
