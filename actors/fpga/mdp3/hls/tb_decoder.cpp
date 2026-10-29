/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for mdp3_decoder_top: feeds every packet of a packet file as
// AXI-Stream words, prints the events as dump.hpp lines, and compares them
// line for line with the reference (ref_dump, CME's real-logic codec).
//
//   MDP3_PACKETS  the packet file
//   MDP3_REF      the reference dump
//   MDP3_OUT      where to write this run's dump (optional)

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../test/dump.hpp"
#include "mdp3fpga/decoder.hpp"

using namespace mdp3fpga;

void mdp3_decoder_top(hls::stream<InWord> &in, hls::stream<kfpga::Envelope> &out, uint32_t n_packets);

static std::vector<std::string> lines(const char *path)
{
  std::vector<std::string> out;
  FILE *f = std::fopen(path, "r");
  if (!f)
    return out;
  char buf[512];
  while (std::fgets(buf, sizeof(buf), f))
    out.push_back(buf);
  std::fclose(f);
  return out;
}

int main()
{
  const char *packets_path = std::getenv("MDP3_PACKETS");
  const char *ref_path = std::getenv("MDP3_REF");
  if (!packets_path || !ref_path)
  {
    std::printf("set MDP3_PACKETS and MDP3_REF\nFAIL\n");
    return 1;
  }
  const auto packets = mdp3test::read_packets(packets_path);
  const auto ref = lines(ref_path);

  hls::stream<InWord> in;
  hls::stream<kfpga::Envelope> out;
  for (const auto &p : packets)
  {
    const size_t n = p.size();
    for (size_t at = 0; at < n || at == 0; at += 8)
    {
      InWord w;
      w.data = 0;
      w.keep = 0;
      for (int i = 0; i < 8 && at + i < n; ++i)
      {
        w.data.range(8 * i + 7, 8 * i) = p[at + i];
        w.keep[i] = 1;
      }
      w.last = at + 8 >= n;
      in.write(w);
      if (n == 0)
        break;
    }
  }
  mdp3_decoder_top(in, out, static_cast<uint32_t>(packets.size()));

  // Print every event, as the reference does.
  char *text = nullptr;
  size_t text_len = 0;
  FILE *f = open_memstream(&text, &text_len);
  size_t packets_seen = 0;
  while (packets_seen < packets.size() && !out.empty())
  {
    const kfpga::Envelope e = out.read();
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
      mdp3test::trade(f, m.seq, m.transact_time, m.price, m.size, m.security_id, m.rpt_seq, m.orders,
                      m.aggressor(), m.action(), m.match_event(), m.trade_id);
      break;
    }
    case SecurityStatus::id:
    {
      const SecurityStatus m = kfpga::decode<SecurityStatus>(e);
      mdp3test::status(f, m.seq, m.transact_time, m.security_id, m.trade_date, m.flags & 0xff,
                       (m.flags >> 8) & 0xff, (m.flags >> 16) & 0xff, m.flags >> 24);
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
      ++packets_seen;
      break;
    }
    default:
      std::fprintf(f, "? msg %u\n", e.msg);
    }
  }
  std::fclose(f);

  if (const char *out_path = std::getenv("MDP3_OUT"))
    if (FILE *o = std::fopen(out_path, "w"))
    {
      std::fwrite(text, 1, text_len, o);
      std::fclose(o);
    }

  // Compare with the reference, line by line.
  std::vector<std::string> got;
  {
    std::string all(text, text_len);
    size_t start = 0;
    for (size_t i = 0; i < all.size(); ++i)
      if (all[i] == '\n')
      {
        got.push_back(all.substr(start, i - start + 1));
        start = i + 1;
      }
  }
  std::free(text);
  size_t diffs = 0, first = 0;
  const size_t n = got.size() > ref.size() ? got.size() : ref.size();
  for (size_t i = 0; i < n; ++i)
  {
    const std::string a = i < got.size() ? got[i] : "(missing)\n";
    const std::string b = i < ref.size() ? ref[i] : "(missing)\n";
    if (a != b)
    {
      if (diffs++ == 0)
        first = i;
      if (diffs <= 5)
        std::printf("line %zu\n  card: %s  ref:  %s", i + 1, a.c_str(), b.c_str());
    }
  }
  std::printf("%zu packets, %zu lines, %zu differ\n", packets.size(), got.size(), diffs);
  (void)first;
  const bool ok = diffs == 0 && packets_seen == packets.size() && !ref.empty();
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
