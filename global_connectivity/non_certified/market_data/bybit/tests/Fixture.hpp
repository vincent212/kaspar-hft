#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Reading recorded frames (bybit::bnet::Recorder format) in tests, and a
// BybitSync wired to a record collector.

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "bybit_md/BybitSync.hpp"
#include "enum/e_names.hpp"
#include "sim/Mbp2Mbo.hpp"

#ifndef BYBIT_TEST_DATA
#define BYBIT_TEST_DATA "tests/data"
#endif

namespace bybit::test
{
  struct Frame
  {
    char kind; // 'W' WebSocket frame, 'R' REST body
    uint64_t ns;
    nlohmann::json j;
  };

  inline std::vector<Frame> load(const std::string &name)
  {
    std::ifstream in(std::string(BYBIT_TEST_DATA) + "/" + name);
    std::vector<Frame> v;
    std::string line;
    while (std::getline(in, line))
    {
      if (line.size() < 4)
        continue;
      const auto a = line.find(' '), b = line.find(' ', a + 1);
      v.push_back({line[0], std::stoull(line.substr(a + 1, b - a - 1)),
                   nlohmann::json::parse(line.substr(b + 1))});
    }
    return v;
  }

  // A BybitSync with its Mbp2Mbo, collecting every record.
  struct Rig
  {
    std::vector<bfile::l3_t> recs;
    sim::Mbp2Mbo m;
    BybitSync s;

    static BybitSync::Config cfg(BybitSync::Mode mode, const std::string &sym, double tick,
                                 double unit, size_t window)
    {
      BybitSync::Config c;
      c.mode = mode;
      c.symbol = sym;
      c.tick = tick;
      c.print_unit = unit;
      c.window = window;
      c.sec_id = 77;
      c.venue = char(en::x::BYBITF);
      return c;
    }

    Rig(BybitSync::Mode mode, const std::string &sym, double tick, double unit, double chunk,
        size_t window = 1000)
        : m(sim::Mbp2Mbo::Config{77, char(en::x::BYBITF), tick, chunk},
            [this](const bfile::l3_t &r) { recs.push_back(r); }),
          s(cfg(mode, sym, tick, unit, window), m, [this](const bfile::l3_t &r) { recs.push_back(r); })
    {
    }

    template <class T>
    size_t count() const
    {
      size_t n = 0;
      for (const auto &r : recs)
        n += std::holds_alternative<T>(r);
      return n;
    }
  };
}
