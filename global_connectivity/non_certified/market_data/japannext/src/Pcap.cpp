/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/Pcap.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

namespace jnx_md
{
  namespace
  {
    uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
    uint32_t be32(const uint8_t *p)
    {
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    uint32_t rd32(const uint8_t *p, bool swap)
    {
      uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
      return swap ? __builtin_bswap32(v) : v;
    }
  }

  Pcap::Pcap(const std::string &path)
  {
    std::ifstream f(path, std::ios::binary);
    if (!f)
      throw std::runtime_error("cannot open " + path);
    data_.assign(std::istreambuf_iterator<char>(f), {});
    const auto *b = reinterpret_cast<const uint8_t *>(data_.data());
    if (data_.size() < 24)
      throw std::runtime_error(path + ": too short for a pcap");
    const uint32_t magic = rd32(b, false);
    bool swap = false, nanos = false;
    if (magic == 0xa1b2c3d4) {}
    else if (magic == 0xa1b23c4d) nanos = true;
    else if (magic == 0xd4c3b2a1) swap = true;
    else if (magic == 0x4d3cb2a1) swap = nanos = true;
    else
      throw std::runtime_error(path + ": not a classic pcap (pcapng is not read)");
    const uint32_t link = rd32(b + 20, swap);
    size_t o = 24;
    while (o + 16 <= data_.size())
    {
      const uint32_t sec = rd32(b + o, swap), frac = rd32(b + o + 4, swap);
      const uint32_t cap = rd32(b + o + 8, swap);
      const uint8_t *p = b + o + 16;
      o += 16 + cap;
      if (o > data_.size())
        break;
      size_t l2 = 0;
      uint16_t et = 0;
      if (link == 1)   // Ethernet
      {
        if (cap < 14) continue;
        l2 = 12;
        et = be16(p + l2);
        l2 += 2;
        while ((et == 0x8100 || et == 0x88a8) && l2 + 4 <= cap)
        {
          et = be16(p + l2 + 2);
          l2 += 4;
        }
      }
      else if (link == 113)   // Linux cooked
      {
        if (cap < 16) continue;
        et = be16(p + 14);
        l2 = 16;
      }
      else if (link == 101 || link == 228)   // raw IPv4
      {
        et = 0x0800;
      }
      else
        continue;
      if (et != 0x0800 || l2 + 20 > cap)
        continue;
      const uint8_t *ip = p + l2;
      const size_t ihl = size_t(ip[0] & 15) * 4, tot = be16(ip + 2);
      if (l2 + tot > cap || tot < ihl + 8)
        continue;
      PcapPacket k;
      k.ts_ns = uint64_t(sec) * 1000000000ull + (nanos ? frac : uint64_t(frac) * 1000);
      k.proto = ip[9];
      k.src = be32(ip + 12);
      k.dst = be32(ip + 16);
      const uint8_t *l4 = ip + ihl;
      k.sport = be16(l4);
      k.dport = be16(l4 + 2);
      if (k.proto == 17)
        k.payload = std::string_view(reinterpret_cast<const char *>(l4 + 8), tot - ihl - 8);
      else if (k.proto == 6)
      {
        const size_t doff = size_t(l4[12] >> 4) * 4;
        if (tot < ihl + doff)
          continue;
        k.tcp_seq = be32(l4 + 4);
        k.tcp_flags = l4[13];
        k.payload = std::string_view(reinterpret_cast<const char *>(l4 + doff), tot - ihl - doff);
      }
      else
        continue;
      pkts_.push_back(k);
    }
  }

  std::vector<PcapPacket> Pcap::udp(uint16_t dport) const
  {
    std::vector<PcapPacket> out;
    for (const auto &p : pkts_)
      if (p.proto == 17 && (!dport || p.dport == dport))
        out.push_back(p);
    return out;
  }

  std::string Pcap::tcp_stream(uint16_t sport) const
  {
    // Sequence numbers relative to the first segment seen (SYN or data).
    std::map<uint32_t, std::string_view> segs;
    bool have = false;
    uint32_t isn = 0;
    for (const auto &p : pkts_)
    {
      if (p.proto != 6 || p.sport != sport)
        continue;
      if (!have)
      {
        have = true;
        isn = p.tcp_seq + ((p.tcp_flags & 0x02) ? 1 : 0);
      }
      if (p.payload.empty())
        continue;
      const uint32_t rel = p.tcp_seq - isn;
      auto &s = segs[rel];
      if (p.payload.size() > s.size())
        s = p.payload;
    }
    std::string out;
    for (const auto &[rel, s] : segs)
    {
      if (rel > out.size())
        break;   // a hole: the stream cannot go on
      if (rel + s.size() > out.size())
        out.append(s.substr(out.size() - rel));
    }
    return out;
  }
}
