#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// A pcap reader for replay: classic pcap (microsecond or nanosecond), Ethernet
// or Linux cooked capture, VLAN tags, IPv4, UDP datagrams and one direction of a
// TCP connection reassembled by sequence number.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jnx_md
{
  struct PcapPacket
  {
    uint64_t ts_ns = 0;          // capture time, UTC epoch ns
    uint8_t proto = 0;           // 17 UDP, 6 TCP
    uint32_t src = 0, dst = 0;   // IPv4, host order
    uint16_t sport = 0, dport = 0;
    uint32_t tcp_seq = 0;
    uint8_t tcp_flags = 0;
    std::string_view payload;    // points into the file's buffer
  };

  class Pcap
  {
  public:
    // Reads the whole file; throws std::runtime_error when it is not a pcap.
    explicit Pcap(const std::string &path);
    const std::vector<PcapPacket> &packets() const { return pkts_; }
    // UDP datagrams, in capture order; port 0 = any destination port.
    std::vector<PcapPacket> udp(uint16_t dport = 0) const;
    // The bytes one side of a TCP connection sent, in sequence order, without
    // retransmitted bytes: the side whose source port is sport.
    std::string tcp_stream(uint16_t sport) const;

  private:
    std::string data_;
    std::vector<PcapPacket> pkts_;
  };
}
