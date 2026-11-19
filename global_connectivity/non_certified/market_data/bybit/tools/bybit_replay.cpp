/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// bybit_replay: replay Bybit's free public order-book archive (the recorded
// orderbook.200 stream, one JSON message per line, from
// quote-saver.bycsi.com/orderbook/{spot|linear}/{SYMBOL}/{DATE}_{SYMBOL}_ob200.data.zip)
// merged by time with the day's public trade file (public.bybit.com), through
// BybitSync (DEPTH mode) -> Mbp2Mbo -> OB, and check:
//
//   - u continuity (a jump is a gap; the archive's next snapshot resyncs);
//   - no crossed book after any book event;
//   - at most 200 levels per side;
//   - seq never decreasing;
//   - with --ob-every K, OB's levels (chunks) and BBO against the sync's book
//     every K book events.
//
//   bybit_replay --book 2026-11-17_BTCUSDT_ob200.data.zip \
//                --trades BTCUSDT_2026-11-17.csv.gz --category spot \
//                --tick 0.1 --unit 0.000001 --chunk 0.01 --lines 2000000 --ob-every 1000
//
// Trade files: spot "id,timestamp(ms),price,volume,side,rpi"; linear
// "timestamp(s),symbol,side,size,price,tickDirection,trdMatchID,...,RPI".
// side is the taker's, as on the WebSocket (assumed: Bybit does not document
// the files).

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "bybit_md/BybitSync.hpp"
#include "ChainOB.hpp"

using namespace bybit;
using json = nlohmann::json;

namespace
{
  struct Pipe
  {
    FILE *f = nullptr;
    explicit Pipe(const std::string &cmd) : f(popen(cmd.c_str(), "r")) {}
    ~Pipe()
    {
      if (f)
        pclose(f);
    }
    bool line(std::string &out)
    {
      out.clear();
      char buf[1 << 16];
      while (fgets(buf, sizeof(buf), f))
      {
        out += buf;
        if (!out.empty() && out.back() == '\n')
        {
          out.pop_back();
          return true;
        }
      }
      return !out.empty();
    }
  };

  std::vector<std::string> split(const std::string &s)
  {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ','))
      v.push_back(x);
    return v;
  }
}

int main(int argc, char **argv)
{
  std::string book, trades, category = "spot", symbol = "BTCUSDT", universe;
  double tick = 0.1, unit = 0.000001, chunk = 0.01;
  uint64_t max_lines = 0, ob_every = 0;
  size_t window = 200;
  for (int i = 1; i < argc; ++i)
  {
    std::string k = argv[i];
    auto v = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--book") book = v();
    else if (k == "--trades") trades = v();
    else if (k == "--category") category = v();
    else if (k == "--symbol") symbol = v();
    else if (k == "--tick") tick = std::stod(v());
    else if (k == "--unit") unit = std::stod(v());
    else if (k == "--chunk") chunk = std::stod(v());
    else if (k == "--window") window = std::stoul(v());
    else if (k == "--lines") max_lines = std::stoull(v());
    else if (k == "--ob-every") ob_every = std::stoull(v());
    else if (k == "--universe") universe = v();
    else
    {
      std::cerr << "unknown argument " << k << std::endl;
      return 2;
    }
  }
  if (book.empty())
  {
    std::cerr << "--book is required" << std::endl;
    return 2;
  }
  if (universe.empty())
  {
    const char *p = std::getenv("KSPRPROJ");
    universe = std::string(p ? p : ".") + "/unit_test/config/universe.csv";
  }
  const bool spot = category == "spot";
  const en::x venue = spot ? en::x::BYBIT : en::x::BYBITF;
  const int32_t sec_id = spot ? 7801 : 7802;

  std::unique_ptr<ChainOB> chain;
  if (ob_every)
    chain = std::make_unique<ChainOB>(universe, "BTCUSDT_replay_" + category, venue, sec_id, 65536);
  uint64_t records = 0;
  auto emit = [&](const bfile::l3_t &r) {
    ++records;
    if (chain)
      chain->feed(r);
  };
  sim::Mbp2Mbo m(sim::Mbp2Mbo::Config{sec_id, char(venue), tick, chunk}, emit);
  BybitSync::Config c;
  c.mode = BybitSync::Mode::DEPTH;
  c.symbol = symbol;
  c.tick = tick;
  c.print_unit = unit;
  c.window = window;
  c.sec_id = sec_id;
  c.venue = char(venue);
  BybitSync s(c, m, emit);

  uint64_t book_events = 0, ob_checks = 0, ob_bad = 0, ob_levels = 0, mid_snapshots = 0,
           u1_snapshots = 0;
  std::string first_bad;
  s.after_event = [&](int64_t u, bool snapshot) {
    ++book_events;
    if (snapshot && book_events > 1)
    {
      ++mid_snapshots;
      if (u == 1)
        ++u1_snapshots;
    }
    if (chain && book_events % ob_every == 0)
    {
      auto k = chain->check(s, m, window);
      ++ob_checks;
      ob_levels += k.levels;
      if (k.size_mismatch || k.mbp_mismatch || !k.bbo_ok)
      {
        ++ob_bad;
        if (first_bad.empty())
          first_bad = "u=" + std::to_string(u) + " " + k.first_bad;
      }
    }
  };

  Pipe bp("unzip -p '" + book + "'");
  std::unique_ptr<Pipe> tp;
  if (!trades.empty())
    tp = std::make_unique<Pipe>("gzip -dc '" + trades + "'");
  std::string bl, tl;
  bool have_b = bp.line(bl), have_t = false;
  json bj, tj;
  uint64_t bt_ms = 0, tt_ms = 0;
  auto next_trade = [&]() {
    have_t = false;
    while (tp && tp->line(tl))
    {
      auto f = split(tl);
      if (f.empty() || f[0] == "id" || f[0] == "timestamp")
        continue;
      std::string side, px, qty;
      bool rpi = false;
      if (spot)
      {
        tt_ms = std::stoull(f[1]);
        px = f[2];
        qty = f[3];
        side = f[4] == "buy" ? "Buy" : "Sell";
        rpi = f.size() > 5 && f[5] == "1";
      }
      else
      {
        tt_ms = uint64_t(std::llround(std::stod(f[0]) * 1000.0));
        side = f[2];
        qty = f[3];
        px = f[4];
        rpi = f.size() > 10 && f[10] == "1";
      }
      tj = {{"topic", "publicTrade." + symbol},
            {"type", "snapshot"},
            {"data", json::array({{{"T", tt_ms}, {"s", symbol}, {"S", side}, {"v", qty},
                                   {"p", px}, {"BT", false}, {"RPI", rpi}}})}};
      have_t = true;
      return;
    }
  };
  auto parse_book = [&]() {
    bj = json::parse(bl);
    bt_ms = bj.value("cts", bj.value("ts", uint64_t(0)));
  };
  if (have_b)
    parse_book();
  next_trade();

  uint64_t lines = 0, trades_fed = 0, gap_returns = 0, first_ms = bt_ms, last_ms = 0;
  while (have_b && (!max_lines || lines < max_lines))
  {
    if (have_t && tt_ms < bt_ms)
    {
      s.on_message(tj);
      ++trades_fed;
      next_trade();
      continue;
    }
    if (!s.on_message(bj))
      ++gap_returns;
    last_ms = bt_ms;
    ++lines;
    have_b = bp.line(bl);
    if (have_b)
      parse_book();
  }

  const auto &st = s.stats();
  json out = {{"category", category}, {"book_lines", lines}, {"first_cts_ms", first_ms},
              {"last_cts_ms", last_ms}, {"hours", double(last_ms - first_ms) / 3.6e6},
              {"book_events", book_events}, {"deltas", st.deltas}, {"snapshots", st.snapshots},
              {"mid_file_snapshots", mid_snapshots}, {"u1_snapshots", u1_snapshots},
              {"gaps", st.gaps}, {"seq_back", st.seq_back}, {"crossed_events", st.crossed},
              {"max_levels_bid", st.max_levels_bid}, {"max_levels_ask", st.max_levels_ask},
              {"trades_fed", trades_fed}, {"trades_applied", st.trades},
              {"prints_only_rpi", st.prints_only}, {"trades_unsynced", st.trades_unsynced},
              {"records", records}, {"ob_checks", ob_checks}, {"ob_checks_bad", ob_bad},
              {"ob_levels_checked", ob_levels}, {"first_bad", first_bad}};
  std::cout << "SUMMARY " << out.dump() << std::endl;
  return 0;
}
