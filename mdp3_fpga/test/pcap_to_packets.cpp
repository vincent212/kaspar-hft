/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Recorded packets for the decoder: the UDP payloads (MDP3 packets) of a
// classic pcap file (micro- or nanosecond), as a packet file (dump.hpp).
// Ethernet II, optional 802.1Q tags, IPv4, UDP; other frames are skipped.
// Keeps only one destination group and port when given, e.g. channel 310's
// incremental feed A.
//
//   pcap_to_packets in.pcap packets.bin [group port]

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
  if (argc != 3 && argc != 5)
  {
    std::fprintf(stderr, "usage: pcap_to_packets in.pcap packets.bin [group port]\n");
    return 2;
  }
  uint32_t want_ip = 0;
  uint16_t want_port = 0;
  if (argc == 5)
  {
    in_addr a;
    if (inet_pton(AF_INET, argv[3], &a) != 1)
    {
      std::fprintf(stderr, "bad group %s\n", argv[3]);
      return 2;
    }
    want_ip = ntohl(a.s_addr);
    want_port = static_cast<uint16_t>(std::atoi(argv[4]));
  }
  FILE *in = std::fopen(argv[1], "rb");
  FILE *out = std::fopen(argv[2], "wb");
  if (!in || !out)
  {
    std::perror("open");
    return 1;
  }
  uint8_t gh[24];
  if (std::fread(gh, 1, 24, in) != 24)
  {
    std::fprintf(stderr, "not a pcap file\n");
    return 1;
  }
  uint32_t magic;
  std::memcpy(&magic, gh, 4);
  if (magic != 0xa1b2c3d4 && magic != 0xa1b23c4d)
  {
    std::fprintf(stderr, "unsupported pcap (magic %08x; pcapng and big-endian files are not read)\n", magic);
    return 1;
  }
  long frames = 0, kept = 0;
  uint8_t rh[16];
  std::vector<uint8_t> f;
  while (std::fread(rh, 1, 16, in) == 16)
  {
    uint32_t caplen;
    std::memcpy(&caplen, rh + 8, 4);
    f.resize(caplen);
    if (std::fread(f.data(), 1, caplen, in) != caplen)
      break;
    ++frames;
    size_t at = 12;
    if (caplen < 14)
      continue;
    uint16_t type = static_cast<uint16_t>(f[at] << 8 | f[at + 1]);
    at += 2;
    while (type == 0x8100 && at + 4 <= caplen)   // 802.1Q
    {
      type = static_cast<uint16_t>(f[at + 2] << 8 | f[at + 3]);
      at += 4;
    }
    if (type != 0x0800 || at + 20 > caplen || (f[at] >> 4) != 4 || f[at + 9] != 17)
      continue;
    const size_t ihl = (f[at] & 0x0f) * 4u;
    const uint32_t dst = static_cast<uint32_t>(f[at + 16]) << 24 | f[at + 17] << 16 | f[at + 18] << 8 | f[at + 19];
    const size_t udp = at + ihl;
    if (udp + 8 > caplen)
      continue;
    const uint16_t dport = static_cast<uint16_t>(f[udp + 2] << 8 | f[udp + 3]);
    const uint16_t ulen = static_cast<uint16_t>(f[udp + 4] << 8 | f[udp + 5]);
    if (want_port && (dst != want_ip || dport != want_port))
      continue;
    if (ulen < 8 || udp + ulen > caplen)
      continue;
    const uint32_t n = ulen - 8u;
    const uint8_t l[4] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
    std::fwrite(l, 1, 4, out);
    std::fwrite(f.data() + udp + 8, 1, n, out);
    ++kept;
  }
  std::fclose(in);
  std::fclose(out);
  std::fprintf(stderr, "%ld frames, %ld packets kept\n", frames, kept);
  return kept ? 0 : 1;
}
