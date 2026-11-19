/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The whole chain on recorded frames: BybitSync -> LevelWindow -> Mbp2Mbo ->
// a real OB. After every book event OB's levels (in chunks) and BBO must
// equal the sync's book.

#include <gtest/gtest.h>

#include <cstdlib>

#include "ChainOB.hpp"
#include "Fixture.hpp"

using namespace bybit;
using namespace bybit::test;

namespace
{
  std::string universe()
  {
    const char *p = std::getenv("KSPRPROJ");
    return std::string(p ? p : "../../../..") + "/unit_test/config/universe.csv";
  }

  void run(const std::string &file, BybitSync::Mode mode, const std::string &sym, double tick,
           double unit, double chunk, size_t window, en::x venue, int32_t sec_id)
  {
    ChainOB chain(universe(), sym + "_CHAIN_" + std::to_string(sec_id), venue, sec_id, 65536);
    sim::Mbp2Mbo m(sim::Mbp2Mbo::Config{sec_id, char(venue), tick, chunk},
                   [&](const bfile::l3_t &r) { chain.feed(r); });
    BybitSync::Config c;
    c.mode = mode;
    c.symbol = sym;
    c.tick = tick;
    c.print_unit = unit;
    c.window = window;
    c.sec_id = sec_id;
    c.venue = char(venue);
    BybitSync s(c, m, [&](const bfile::l3_t &r) { chain.feed(r); });
    size_t checks = 0, bad = 0, levels = 0;
    std::string first;
    s.after_event = [&](int64_t, bool) {
      auto k = chain.check(s, m, window);
      ++checks;
      levels += k.levels;
      if (k.size_mismatch || !k.bbo_ok || k.mbp_mismatch)
      {
        ++bad;
        if (first.empty())
          first = k.first_bad;
      }
    };
    bool synced_once = false;
    for (const auto &f : load(file))
    {
      if (f.kind == 'R')
      {
        if (!synced_once && s.on_snapshot(f.j.at("result")) == BybitSync::SnapResult::Applied)
          synced_once = true;
        continue;
      }
      s.on_message(f.j);
    }
    EXPECT_GT(checks, 10u);
    EXPECT_GT(levels, 100u);
    EXPECT_EQ(bad, 0u) << first;
    EXPECT_GT(chain.records(), 100u);
  }
}

TEST(BybitChainOB, LinearDepth50WithTrades)
{
  run("linear_BTCUSDT_ob50.rec", BybitSync::Mode::DEPTH, "BTCUSDT", 0.1, 0.001, 0.1, 50,
      en::x::BYBITF, 8801);
}

TEST(BybitChainOB, SpotFullWindow200)
{
  run("spot_SOLBTC_full.rec", BybitSync::Mode::FULL, "SOLBTC", 0.0000001, 0.0001, 1.0, 200,
      en::x::BYBIT, 8802);
}
