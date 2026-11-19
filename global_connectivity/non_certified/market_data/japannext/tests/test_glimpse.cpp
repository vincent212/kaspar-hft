/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The public sample captures, when JNX_DATA names the directory holding them:
// the GLIMPSE capture's directory through the instrument loader, the ITCH
// capture through the handler. Skipped otherwise.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "TestUtil.hpp"
#include "jnx_md/Pcap.hpp"

using namespace jnx_md;
using namespace jnx_md::test;

namespace
{
  std::string data(const char *file)
  {
    const char *d = std::getenv("JNX_DATA");
    if (!d || !*d)
      return {};
    const auto p = std::filesystem::path(d) / file;
    return std::filesystem::exists(p) ? p.string() : std::string();
  }
}

TEST(SampleGlimpse, DirectoryLoads)
{
  const std::string f = data("Japannext_PTS_GLIMPSE_Equities_v1.4.pcap");
  if (f.empty())
    GTEST_SKIP() << "JNX_DATA not set";
  const std::string s = Pcap(f).tcp_stream(15002);
  ASSERT_EQ(s.size(), 282118u);
  RecSink sink;
  Handler h(config(), sink);
  h.preload_directory(s.data(), s.size());
  const Stats &st = h.stats();
  EXPECT_EQ(st.snapshot_by_type.at('R'), 4182u);
  EXPECT_EQ(st.snapshot_by_type.at('H'), 4181u);
  EXPECT_EQ(st.snapshot_by_type.at('L'), 13u);
  EXPECT_EQ(st.snapshot_by_type.at('Y'), 15u);
  EXPECT_EQ(st.snapshot_by_type.at('A'), 295u);
  EXPECT_EQ(st.snapshot_by_type.count('G'), 0u);   // truncated before End of Snapshot
  EXPECT_EQ(st.unknown + st.malformed, 0u);
  EXPECT_EQ(h.instruments().all().size(), 4182u);
  EXPECT_EQ(st.reference_prices, 295u);
  // Every book's limits reach only the tick its unit divides.
  size_t not_in_spin = 0;
  for (const auto &i : h.instruments().all())
  {
    EXPECT_GT(i->unit, 0u);
    EXPECT_EQ(i->upper % i->unit, 0u) << i->sicc;
    not_in_spin += i->trading_state == 0;
  }
  EXPECT_EQ(not_in_spin, 1u);
  // The first entry, as feed.md section 8 records it.
  const Instrument *first = h.instruments().all().front().get();
  EXPECT_EQ(first->sicc, "1301");
  EXPECT_EQ(first->isin, "JP3257200000");
  EXPECT_EQ(first->group, "DAY");
  // A GLIMPSE stream outside a recovery is not a snapshot: nothing reset.
  size_t chr = 0;
  for (const auto &r : sink.recs)
    chr += std::holds_alternative<bfile::l3_chr_v2_t>(r.r);
  EXPECT_EQ(chr, 0u);
}

TEST(SampleItch, ReplayCounts)
{
  const std::string u = data("Japannext_PTS_ITCH_Equities_v1.7.UDP.pcap");
  const std::string g = data("Japannext_PTS_GLIMPSE_Equities_v1.4.pcap");
  if (u.empty() || g.empty())
    GTEST_SKIP() << "JNX_DATA not set";
  RecSink sink;
  auto c = config(false, 0, Handler::Join::FirstPacket);
  Handler h(c, sink);
  const std::string s = Pcap(g).tcp_stream(15002);
  h.preload_directory(s.data(), s.size());
  size_t n = 0;
  for (const auto &p : Pcap(u).udp())
  {
    h.on_packet(p.payload.data(), p.payload.size(), p.ts_ns);
    ++n;
  }
  const Stats &st = h.stats();
  EXPECT_EQ(n, 224754u);
  EXPECT_EQ(st.messages, 222189u);
  EXPECT_EQ(st.gaps, 0u);
  EXPECT_EQ(st.by_type.at('A'), 128366u);
  EXPECT_EQ(st.by_type.at('E'), 67902u);
  EXPECT_EQ(st.by_type.at('D'), 10772u);
  EXPECT_EQ(st.by_type.at('U'), 9287u);
  EXPECT_EQ(st.unknown_exec + st.unknown_delete + st.unknown_replace + st.unknown_book, 0u);
  EXPECT_EQ(st.dup_add + st.over_exec + st.wide_order_number, 0u);
  EXPECT_EQ(st.crossed_at_17, 0u);
  EXPECT_EQ(st.px_off_unit + st.px_off_tick + st.px_out_of_limits, 0u);
  EXPECT_EQ(st.time_backsteps, 0u);
  EXPECT_EQ(h.dispatcher().orders().size(), 50736u);
  EXPECT_EQ(h.next_seq(), 234751u);
}
