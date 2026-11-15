#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Test frames for Igmp: the JoinGroup the CPU sends, the membership queries a
// switch sends, and the frames the card must send, each built byte by byte here,
// independently of igmp.hpp, from RFC 2236 and RFC 2113.

#include <cstdint>
#include <vector>

#include "frames.hpp"
#include "mdp3fpga/igmp.hpp"

namespace mdp3test {

constexpr uint64_t kCardMac = 0x02000000AB01ull;   // locally administered
constexpr uint32_t kCardIp = 0x0A0B0C0D;           // 10.11.12.13
constexpr uint16_t kVlan = 3008;

inline mdp3fpga::JoinGroup join_344a(bool tag, uint16_t vlan = kVlan)
{
  mdp3fpga::JoinGroup j;
  j.group = kGroup344A;
  j.port = kPort344A;
  j.mac = kCardMac;
  j.ip = kCardIp;
  j.vlan = tag ? vlan : 0;
  return j;
}

inline uint16_t checksum(const std::vector<uint8_t> &b, size_t at, size_t n)
{
  uint32_t s = 0;
  for (size_t i = 0; i < n; i += 2)
    s += (b[at + i] << 8) | b[at + i + 1];
  while (s >> 16)
    s = (s & 0xffff) + (s >> 16);
  return static_cast<uint16_t>(~s);
}

// An IGMP frame: `type` about `group`, to `dst`, from `src_mac`/`src_ip`, on `vlan`
// if `tag`. With the Router Alert option if `ra`, as RFC 2236 asks (some queriers
// leave it out), and `nops` more 4-byte option words of no-operations after it.
inline std::vector<uint8_t> igmp_frame(uint8_t type, uint8_t max_resp, uint32_t group, uint32_t dst, uint64_t src_mac,
                                       uint32_t src_ip, bool tag, bool ra, uint8_t tos = 0xc0, uint16_t vlan = kVlan,
                                       int nops = 0)
{
  std::vector<uint8_t> f;
  auto u8 = [&](unsigned v) { f.push_back(static_cast<uint8_t>(v)); };
  auto u16 = [&](unsigned v) { u8(v >> 8); u8(v); };
  auto u32 = [&](uint32_t v) { u16(v >> 16); u16(v & 0xffff); };
  u8(0x01); u8(0x00); u8(0x5e); u8((dst >> 16) & 0x7f); u8(dst >> 8); u8(dst);
  for (int i = 5; i >= 0; --i)
    u8(static_cast<unsigned>(src_mac >> (8 * i)));
  if (tag)
  {
    u16(0x8100);
    u16(vlan);
  }
  u16(0x0800);
  const size_t ip = f.size();
  const unsigned hl = 20 + (ra ? 4 : 0) + 4 * nops;
  u8(0x40 | (hl / 4)); u8(tos); u16(hl + 8); u16(0); u16(0x4000); u8(1); u8(2); u16(0);
  u32(src_ip);
  u32(dst);
  if (ra)
    u32(0x94040000);
  for (int i = 0; i < nops; ++i)
    u32(0x01010101);
  const uint16_t ic = checksum(f, ip, hl);
  f[ip + 10] = ic >> 8;
  f[ip + 11] = ic & 0xff;
  const size_t ig = f.size();
  u8(type); u8(max_resp); u16(0); u32(group);
  const uint16_t gc = checksum(f, ig, 8);
  f[ig + 2] = gc >> 8;
  f[ig + 3] = gc & 0xff;
  while (f.size() < 60)
    f.push_back(0);
  return f;
}

// What the card must send.
inline std::vector<uint8_t> expected_report(bool tag, uint16_t vlan = kVlan)
{
  return igmp_frame(0x16, 0, kGroup344A, kGroup344A, kCardMac, kCardIp, tag, true, 0xc0, vlan);
}
inline std::vector<uint8_t> expected_leave(bool tag, uint16_t vlan = kVlan)
{
  return igmp_frame(0x17, 0, kGroup344A, 0xE0000002, kCardMac, kCardIp, tag, true, 0xc0, vlan);
}

// What a switch sends: a general query to 224.0.0.1, or one for a group.
constexpr uint64_t kSwitchMac = 0x001122334455ull;
constexpr uint32_t kSwitchIp = 0x0A0B0C01;
inline std::vector<uint8_t> query(uint32_t group, bool tag, bool ra, uint16_t vlan = kVlan, int nops = 0)
{
  return igmp_frame(0x11, 100, group, group ? group : 0xE0000001, kSwitchMac, kSwitchIp, tag, ra, 0, vlan, nops);
}

} // namespace mdp3test
