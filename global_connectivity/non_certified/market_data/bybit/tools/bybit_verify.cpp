/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// bybit_verify: run BybitSync on Bybit's live public stream (no account) and
// check it continuously.
//
//   - orderbook.full.{symbol} and publicTrade.{symbol} on one WebSocket;
//   - every --interval seconds a REST GET /v5/market/full_orderbook. Its (u,
//     seq) is that of one stream delta, so the sync's book is compared at
//     exactly that u: if the stream is ahead, the deltas since are undone
//     from an undo log; if it is behind, the comparison waits for it;
//   - levels are compared over the price range both books know (the REST
//     snapshot's depth, and the depth of the snapshot the sync started from:
//     beyond that a level the stream never touched is unknown), and over the
//     best 200 levels per side;
//   - with --ob, every record goes through Mbp2Mbo into a real OB, and after
//     each comparison OB's best --window levels are checked against the
//     sync's book (sizes in chunks) and its BBO;
//   - --record writes every frame and REST body (BybitNet's Recorder format).
//
// REST use: one full_orderbook request per --interval (default 10 s) plus at
// most one a second while synchronising; Bybit's IP limit is 600 per 5 s.
//
//   bybit_verify --category linear --symbol BTCUSDT --tick 0.1 --unit 0.001 \
//                --minutes 30 [--ob --chunk 0.1 --window 1000] [--record f]

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "bybit_md/BybitNet.hpp"
#include "bybit_md/BybitSync.hpp"
#include "ChainOB.hpp"

using namespace bybit;

namespace
{
  struct Args
  {
    std::string category = "linear", symbol = "BTCUSDT", record, universe;
    double tick = 0.1, unit = 0.001, chunk = 0.1;
    double minutes = 30;
    int interval = 10;
    size_t window = 1000;
    bool ob = false;
    int ladder = 65536;
  };

  bool eq(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(a)); }

  struct Event
  {
    int64_t u = 0;
    std::vector<std::tuple<bool, int64_t, double>> before; // side, tick, quantity before
  };

  struct Compare
  {
    uint64_t levels = 0, qty_diff = 0, missing = 0, extra = 0;
    uint64_t top_levels = 0, top_bad = 0;
    std::string first;
    bool ok() const { return !qty_diff && !missing && !extra; }
  };

  // ours vs rest, bids high to low, asks low to high. lo/hi bound the range
  // compared (ticks).
  void compare_side(bool bid, const BybitSync::Book &ours, const BybitSync::Book &rest,
                    int64_t limit, Compare &c)
  {
    auto in = [&](int64_t t) { return bid ? t >= limit : t <= limit; };
    for (const auto &[t, q] : rest)
    {
      if (!in(t))
        continue;
      ++c.levels;
      auto it = ours.find(t);
      if (it == ours.end())
      {
        if (c.first.empty())
          c.first = std::string(bid ? "bid " : "ask ") + std::to_string(t) + " missing";
        ++c.missing;
      }
      else if (!eq(it->second, q))
      {
        if (c.first.empty())
          c.first = std::string(bid ? "bid " : "ask ") + std::to_string(t) + " qty " +
                    std::to_string(it->second) + " rest " + std::to_string(q);
        ++c.qty_diff;
      }
    }
    for (const auto &[t, q] : ours)
      if (in(t) && !rest.count(t))
      {
        if (c.first.empty())
          c.first = std::string(bid ? "bid " : "ask ") + std::to_string(t) + " extra";
        ++c.extra;
      }
    // best 200 levels
    auto top = [&](const BybitSync::Book &b) {
      std::vector<std::pair<int64_t, double>> v;
      if (bid)
        for (auto it = b.rbegin(); it != b.rend() && v.size() < 200; ++it)
          v.push_back(*it);
      else
        for (auto it = b.begin(); it != b.end() && v.size() < 200; ++it)
          v.push_back(*it);
      return v;
    };
    auto a = top(ours), r = top(rest);
    c.top_levels += r.size();
    for (size_t i = 0; i < r.size(); ++i)
      if (i >= a.size() || a[i].first != r[i].first || !eq(a[i].second, r[i].second))
        ++c.top_bad;
  }
}

int main(int argc, char **argv)
{
  Args A;
  for (int i = 1; i < argc; ++i)
  {
    std::string k = argv[i];
    auto v = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--category") A.category = v();
    else if (k == "--symbol") A.symbol = v();
    else if (k == "--tick") A.tick = std::stod(v());
    else if (k == "--unit") A.unit = std::stod(v());
    else if (k == "--chunk") A.chunk = std::stod(v());
    else if (k == "--minutes") A.minutes = std::stod(v());
    else if (k == "--interval") A.interval = std::stoi(v());
    else if (k == "--window") A.window = std::stoul(v());
    else if (k == "--ladder") A.ladder = std::stoi(v());
    else if (k == "--record") A.record = v();
    else if (k == "--universe") A.universe = v();
    else if (k == "--ob") A.ob = true;
    else
    {
      std::cerr << "unknown argument " << k << std::endl;
      return 2;
    }
  }
  if (A.universe.empty())
  {
    const char *p = std::getenv("KSPRPROJ");
    A.universe = std::string(p ? p : ".") + "/unit_test/config/universe.csv";
  }
  const en::x venue = A.category == "spot" ? en::x::BYBIT : en::x::BYBITF;
  const int32_t sec_id = A.category == "spot" ? 7701 : 7702;

  std::unique_ptr<ChainOB> chain;
  if (A.ob)
    chain = std::make_unique<ChainOB>(A.universe, "BTCUSDT_" + A.category, venue, sec_id, A.ladder);

  uint64_t records = 0, prints = 0, gaps_rec = 0, clears_rec = 0;
  auto emit = [&](const bfile::l3_t &r) {
    ++records;
    if (std::holds_alternative<bfile::l3_mbp_trd_t>(r)) ++prints;
    if (std::holds_alternative<bfile::l3_gap_v2_t>(r)) ++gaps_rec;
    if (std::holds_alternative<bfile::l3_chr_v2_t>(r)) ++clears_rec;
    if (chain)
      chain->feed(r);
  };
  sim::Mbp2Mbo m(sim::Mbp2Mbo::Config{sec_id, char(venue), A.tick, A.chunk}, emit);
  BybitSync::Config sc;
  sc.mode = BybitSync::Mode::FULL;
  sc.symbol = A.symbol;
  sc.tick = A.tick;
  sc.print_unit = A.unit;
  sc.window = A.window;
  sc.sec_id = sec_id;
  sc.venue = char(venue);
  BybitSync sync(sc, m, emit);

  // Undo log and the depth the sync started from.
  std::deque<Event> log;
  Event cur;
  int64_t known_bid = 0, known_ask = 0; // deepest ticks of the snapshot the sync started from
  sync.before_level = [&](bool bid, int64_t t, double before) { cur.before.emplace_back(bid, t, before); };

  // Comparison state.
  struct Pending
  {
    int64_t u;
    BybitSync::Book b, a;
    uint64_t at_ns;
  };
  std::optional<Pending> waiting;
  uint64_t checks = 0, exact_ok = 0, top_ok = 0, not_comparable = 0, waited = 0;
  uint64_t lag_sum = 0, lag_max = 0;
  uint64_t ob_checks = 0, ob_ok = 0, ob_levels = 0, ob_bad_levels = 0, ob_bbo_bad = 0, mbp_bad = 0;
  bool ob_due = false;

  auto do_compare = [&](const Pending &p, const BybitSync::Book &ob, const BybitSync::Book &oa,
                        const char *how) {
    Compare c;
    const int64_t bid_lim = std::max(p.b.empty() ? INT64_MIN : p.b.begin()->first, known_bid);
    const int64_t ask_lim = std::min(p.a.empty() ? INT64_MAX : p.a.rbegin()->first, known_ask);
    compare_side(true, ob, p.b, bid_lim, c);
    compare_side(false, oa, p.a, ask_lim, c);
    ++checks;
    if (c.ok()) ++exact_ok;
    if (!c.top_bad) ++top_ok;
    std::cout << "check u=" << p.u << " (" << how << ") levels " << c.levels
              << " qty_diff " << c.qty_diff << " missing " << c.missing << " extra " << c.extra
              << " top200_bad " << c.top_bad << "/" << c.top_levels
              << (c.first.empty() ? "" : "  first: " + c.first) << std::endl;
    ob_due = true;
  };

  sync.after_event = [&](int64_t u, bool snapshot) {
    if (snapshot)
    {
      log.clear();
      cur.before.clear();
      known_bid = sync.bids().empty() ? INT64_MIN : sync.bids().begin()->first;
      known_ask = sync.asks().empty() ? INT64_MAX : sync.asks().rbegin()->first;
    }
    else
    {
      cur.u = u;
      log.push_back(std::move(cur));
      cur = Event{};
      while (log.size() > 600)
        log.pop_front();
    }
    if (waiting && u == waiting->u)
    {
      do_compare(*waiting, sync.bids(), sync.asks(), "waited");
      ++waited;
      waiting.reset();
    }
    else if (waiting && u > waiting->u)
    {
      ++not_comparable; // skipped over it (a gap or a resync)
      waiting.reset();
    }
    if (chain && ob_due && !snapshot)
    {
      ob_due = false;
      auto c = chain->check(sync, m, A.window);
      ++ob_checks;
      ob_levels += c.levels;
      ob_bad_levels += c.size_mismatch;
      mbp_bad += c.mbp_mismatch;
      if (!c.bbo_ok) ++ob_bbo_bad;
      if (!c.size_mismatch && c.bbo_ok) ++ob_ok;
      std::cout << "ob u=" << u << " levels " << c.levels << " size_bad " << c.size_mismatch
                << " mbp_bad " << c.mbp_mismatch << " bbo " << (c.bbo_ok ? "ok" : "BAD")
                << (c.first_bad.empty() ? "" : "  first: " + c.first_bad) << std::endl;
    }
  };

  // Compare a REST snapshot against the sync at its u.
  auto check_snapshot = [&](const nlohmann::json &res, uint64_t recv_ns) {
    if (!sync.synced())
    {
      ++not_comparable;
      return;
    }
    Pending p;
    p.u = res.at("u").get<int64_t>();
    p.at_ns = recv_ns;
    for (const auto &l : res.at("b"))
      p.b[sync.ticks(BybitSync::num(l[0]))] = BybitSync::num(l[1]);
    for (const auto &l : res.at("a"))
      p.a[sync.ticks(BybitSync::num(l[0]))] = BybitSync::num(l[1]);
    const int64_t now_u = sync.last_u();
    if (now_u < p.u)
    {
      if (waiting) ++not_comparable;
      waiting = std::move(p);
      return;
    }
    // The stream is ahead: undo the deltas after p.u on a copy.
    if (!log.empty() && log.front().u > p.u + 1 && now_u != p.u)
    {
      ++not_comparable;
      return;
    }
    if (now_u - p.u > int64_t(log.size()))
    {
      ++not_comparable;
      return;
    }
    const uint64_t lag = uint64_t(now_u - p.u);
    lag_sum += lag;
    lag_max = std::max(lag_max, lag);
    BybitSync::Book b = sync.bids(), a = sync.asks();
    for (auto it = log.rbegin(); it != log.rend() && it->u > p.u; ++it)
      for (auto e = it->before.rbegin(); e != it->before.rend(); ++e)
      {
        auto &side = std::get<0>(*e) ? b : a;
        if (std::get<2>(*e) <= 0)
          side.erase(std::get<1>(*e));
        else
          side[std::get<1>(*e)] = std::get<2>(*e);
      }
    do_compare(p, b, a, lag ? "undone" : "current");
  };

  // REST thread: synchronisation snapshots and comparison snapshots.
  bnet::Hosts hosts;
  std::unique_ptr<bnet::Recorder> rec;
  if (!A.record.empty())
    rec = std::make_unique<bnet::Recorder>(A.record);
  std::mutex mtx;
  std::condition_variable cv;
  std::deque<std::pair<std::string, uint64_t>> sync_bodies, check_bodies;
  std::atomic<bool> want_sync{false}, stop{false};
  uint64_t rest_calls = 0, rest_errors = 0;
  std::thread rest([&] {
    auto last_sync = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    auto next_check = std::chrono::steady_clock::now() + std::chrono::seconds(A.interval);
    while (!stop)
    {
      const auto now = std::chrono::steady_clock::now();
      bool do_sync = want_sync && now - last_sync >= std::chrono::seconds(1);
      bool do_check = now >= next_check;
      if (!do_sync && !do_check)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      try
      {
        ++rest_calls;
        auto body = bnet::full_orderbook(hosts, A.category, A.symbol);
        const uint64_t t = sim::wsnet::now_ns();
        if (rec) rec->rest(t, body);
        std::lock_guard<std::mutex> lk(mtx);
        if (do_sync)
        {
          sync_bodies.emplace_back(body, t);
          want_sync = false;
          last_sync = now;
        }
        if (do_check)
          check_bodies.emplace_back(std::move(body), t);
      }
      catch (const std::exception &e)
      {
        ++rest_errors;
        std::cerr << "REST: " << e.what() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      if (do_check)
        next_check += std::chrono::seconds(A.interval);
    }
  });

  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(int64_t(A.minutes * 60000));
  uint64_t frames = 0, reconnects = 0, snap_too_old = 0, snap_mismatch = 0;
  auto last_report = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() < end)
  {
    try
    {
      bnet::PublicWs ws(hosts, A.category);
      ws.connect({"orderbook.full." + A.symbol, "publicTrade." + A.symbol});
      std::cerr << "connected " << A.category << " " << A.symbol << std::endl;
      while (std::chrono::steady_clock::now() < end)
      {
        auto frame = ws.read();
        const uint64_t t = sim::wsnet::now_ns();
        ++frames;
        if (rec) rec->ws(t, frame);
        sync.on_message(nlohmann::json::parse(frame));
        if (sync.need_snapshot() && sync.buffering())
          want_sync = true;
        std::deque<std::pair<std::string, uint64_t>> s, c;
        {
          std::lock_guard<std::mutex> lk(mtx);
          s.swap(sync_bodies);
          c.swap(check_bodies);
        }
        for (auto &[body, at] : s)
        {
          auto j = nlohmann::json::parse(body);
          if (!sync.need_snapshot())
            continue; // a second request raced the first; already synchronised
          auto r = sync.on_snapshot(j.at("result"));
          if (r == BybitSync::SnapResult::TooOld) { ++snap_too_old; want_sync = true; }
          if (r == BybitSync::SnapResult::SeqMismatch) { ++snap_mismatch; want_sync = true; }
          std::cerr << "sync snapshot u=" << j.at("result").at("u") << ": "
                    << (r == BybitSync::SnapResult::Applied ? "applied" : "refused") << std::endl;
        }
        for (auto &[body, at] : c)
          check_snapshot(nlohmann::json::parse(body).at("result"), at);
        if (std::chrono::steady_clock::now() - last_report > std::chrono::seconds(60))
        {
          last_report = std::chrono::steady_clock::now();
          const auto &st = sync.stats();
          std::cout << "status frames " << frames << " deltas " << st.deltas << " trades "
                    << st.trades << " gaps " << st.gaps << " checks " << checks << " exact_ok "
                    << exact_ok << std::endl;
        }
      }
      ws.close();
    }
    catch (const std::exception &e)
    {
      std::cerr << "ws: " << e.what() << "; reconnecting" << std::endl;
      ++reconnects;
      sync.reset();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  stop = true;
  rest.join();

  const auto &st = sync.stats();
  nlohmann::json out = {
      {"category", A.category}, {"symbol", A.symbol}, {"minutes", A.minutes},
      {"frames", frames}, {"reconnects", reconnects},
      {"deltas", st.deltas}, {"snapshots_applied", st.snapshots}, {"trades", st.trades},
      {"prints_only_rpi_or_block", st.prints_only}, {"trades_while_unsynced", st.trades_unsynced},
      {"gaps", st.gaps}, {"resets_u1", st.resets}, {"duplicates", st.duplicates},
      {"seq_back", st.seq_back}, {"snapshots_too_old", snap_too_old},
      {"snapshots_seq_mismatch", snap_mismatch}, {"crossed_events", st.crossed},
      {"max_levels_bid", st.max_levels_bid}, {"max_levels_ask", st.max_levels_ask},
      {"rest_calls", rest_calls}, {"rest_errors", rest_errors},
      {"checks", checks}, {"checks_exact_full_range", exact_ok}, {"checks_top200_exact", top_ok},
      {"checks_not_comparable", not_comparable}, {"checks_waited_for_stream", waited},
      {"stream_ahead_by_u_mean", checks ? double(lag_sum) / double(checks) : 0.0},
      {"stream_ahead_by_u_max", lag_max},
      {"records", records}, {"prints", prints}, {"gap_records", gaps_rec}, {"clear_records", clears_rec},
      {"ob_checks", ob_checks}, {"ob_checks_ok", ob_ok}, {"ob_levels_checked", ob_levels},
      {"ob_levels_bad", ob_bad_levels}, {"ob_bbo_bad", ob_bbo_bad}, {"mbp2mbo_levels_bad", mbp_bad},
  };
  std::cout << "SUMMARY " << out.dump() << std::endl;
  return 0;
}
