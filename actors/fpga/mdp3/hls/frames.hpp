#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Test frames: an MDP3 packet from a recording rebuilt as the Ethernet frame the
// MAC would hand over, 8 bytes a word, without the FCS.

#include <cstdint>
#include <cstring>
#include <vector>

namespace mdp3test {

constexpr uint32_t kGroup344A = 0xE0001F44;   // 224.0.31.68, channel 344 feed A
constexpr uint16_t kPort344A = 14344;

template <class W, class Sink>
void frame_words(const std::vector<uint8_t> &b, Sink &&sink)
{
  const size_t n = b.size();
  for (size_t at = 0; at < n; at += 8)
  {
    W w;
    w.data = 0;
    w.keep = 0;
    for (int i = 0; i < 8 && at + i < n; ++i)
    {
      w.data.range(8 * i + 7, 8 * i) = b[at + i];
      w.keep[i] = 1;
    }
    w.last = at + 8 >= n;
    sink(w);
  }
}

inline std::vector<uint8_t> make_frame(const uint8_t *payload, size_t n, uint32_t group, uint16_t port, bool tag,
                                       uint16_t ether = 0x0800, uint8_t proto = 17, size_t min_frame = 60)
{
  std::vector<uint8_t> f;
  auto u8 = [&](unsigned v) { f.push_back(static_cast<uint8_t>(v)); };
  auto u16 = [&](unsigned v) { u8(v >> 8); u8(v); };
  auto u32 = [&](uint32_t v) { u16(v >> 16); u16(v & 0xffff); };
  u8(0x01); u8(0x00); u8(0x5e); u8((group >> 16) & 0x7f); u8(group >> 8); u8(group);   // dst MAC
  u8(0x02); u8(0); u8(0); u8(0); u8(0); u8(1);                                          // src MAC
  if (tag)
  {
    u16(0x8100);
    u16(3008);
  }
  u16(ether);
  const unsigned udp = 8 + static_cast<unsigned>(n);
  u8(0x45); u8(0); u16(20 + udp); u16(0); u16(0x4000); u8(32); u8(proto); u16(0);      // IPv4
  u32(0xCDD1DD51);                                                                      // source
  u32(group);
  u16(port); u16(port); u16(udp); u16(0);                                               // UDP
  f.insert(f.end(), payload, payload + n);
  while (f.size() < min_frame)
    f.push_back(0xee);                                                                  // padding
  return f;
}

} // namespace mdp3test
