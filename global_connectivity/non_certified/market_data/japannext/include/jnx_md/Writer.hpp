#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Encoders for tests and the replay tool: MoldUDP64 packets, ITCH messages, and
// a GLIMPSE snapshot (as the SoupBinTCP bytes a GLIMPSE server sends) written
// from a handler's state. The generated headers decode only (ITCH and GLIMPSE
// are from_ sections), so these fill the generated structs and copy them out.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "jnx_md/Handler.hpp"

namespace jnx_md
{
  // The bytes of any generated message struct, its type code set.
  template <class M>
  std::string bytes_of(M m)
  {
    m.MessageType = M::type;
    return std::string(reinterpret_cast<const char *>(&m), sizeof(m));
  }

  // A MoldUDP64 downstream packet: header, then each message after its 2-byte
  // length. count 0 is a heartbeat, 0xFFFF End of Session (messages ignored).
  std::string mold_packet(std::string_view session, uint64_t seq,
                          const std::vector<std::string> &messages, uint16_t count = 0);

  // The messages of a MoldUDP64 packet (no checks: for packets already read).
  std::vector<std::string> mold_messages(std::string_view packet);

  // One SoupBinTCP packet: length, type, payload.
  std::string soup_packet(char type, std::string_view payload);

  // A whole GLIMPSE session for the handler's current state, as of the message
  // before `next_seq`: Login Accepted, then Timestamp - Seconds, System Event O,
  // the tick tables, directory, trading states, short-sell restrictions,
  // reference prices, every resting order (per book, bids then asks, best price
  // first, each level in queue order), and End of Snapshot with next_seq.
  std::string glimpse_snapshot(const Handler &h, uint64_t next_seq);
}
