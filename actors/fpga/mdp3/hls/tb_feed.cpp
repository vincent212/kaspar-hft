/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Testbench for mdp3_feed_top, for co-simulation: queues every packet of a
// packet file, then checks each End line against the software book (ref_book).
//
//   MDP3_PACKETS  the packet file
//   MDP3_BOOK     the reference from ref_book, for security index 0
//   MDP3_SECURITIES  the filter's table, a comma list of security ids

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../test/dump.hpp"
#include "feed_check.hpp"
#include "igmp_frames.hpp"
#include "frames.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;

void mdp3_feed_top(InStream &in, kfpga::Fifo &window, kfpga::Fifo &join_in, kfpga::Fifo &out, InStream &tx_port,
                   uint32_t n_words, uint32_t n_want, uint32_t n_out, uint32_t n_tx);

int main()
{
  const char *packets_path = std::getenv("MDP3_PACKETS");
  const char *ref_path = std::getenv("MDP3_BOOK");
  if (!packets_path || !ref_path)
  {
    std::printf("set MDP3_PACKETS and MDP3_BOOK\nFAIL\n");
    return 1;
  }
  const auto packets = mdp3test::read_packets(packets_path);
  std::vector<std::string> ref;
  uint32_t n_out = 0;
  {
    FILE *f = std::fopen(ref_path, "r");
    char line[1024];
    while (f && std::fgets(line, sizeof(line), f))
    {
      unsigned seq, gap, events;
      std::sscanf(line, "Z %u %u %u", &seq, &gap, &events);
      ref.push_back(line);
      n_out += events / kEventsPerLine;   // the full lines; every packet's End line is added below
    }
  }
  n_out += static_cast<uint32_t>(packets.size());
  InStream in;
  kfpga::Fifo out;
  uint32_t n_words = 0;
  for (const auto &p : packets)
    mdp3test::frame_words<InWord>(mdp3test::make_frame(p.data(), p.size(), mdp3test::kGroup344A, mdp3test::kPort344A, false),
                                  [&](const InWord &w) {
                                    in.write(w);
                                    ++n_words;
                                  });
  kfpga::Fifo window;
  const auto table = table_from_env();
  for (const auto &e : table)
    window.write(e);
  kfpga::Fifo join_in;
  join_in.write(kfpga::encode(mdp3test::join_344a(false), kCpuFeed, kIgmp));
  InStream tx_port;
  mdp3_feed_top(in, window, join_in, out, tx_port, n_words, static_cast<uint32_t>(table.size()), n_out,
                mdp3fpga::kIgmpFrameWords);

  // The join's membership report, as the card sent it.
  std::vector<uint8_t> report;
  while (!tx_port.empty())
  {
    const InWord w = tx_port.read();
    for (int i = 0; i < 8; ++i)
      if (w.keep[i])
        report.push_back(static_cast<uint8_t>(w.data.range(8 * i + 7, 8 * i)));
  }
  const bool report_ok = report == mdp3test::expected_report(false);
  std::printf("membership report %s\n", report_ok ? "as expected" : "differs");

  FeedCheck feed;
  size_t ends = 0, diffs = 0;
  char got[1024];
  while (!out.empty())
  {
    if (!feed.take(out.read(), got, sizeof(got)))
      continue;
    if (ends >= ref.size() || ref[ends] != got)
    {
      if (++diffs <= 3)
        std::printf("end %zu\n  card: %s", ends + 1, got);
    }
    ++ends;
  }
  std::printf("%zu packets, %zu packet ends of %zu, %zu differ, %llu events, %llu lines\n", packets.size(), ends,
              ref.size(), diffs, (unsigned long long)feed.events, (unsigned long long)feed.lines);
  const bool ok = diffs == 0 && ends == ref.size() && ends != 0 && report_ok;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
