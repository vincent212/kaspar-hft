/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// kraken_verify -- checks KrakenBookSync against Kraken itself, live or on a
// recording (README.md, "Live check").
//
//   live:   connects to wss://ws.kraken.com/v2 (book + trade, no account),
//           applies every frame, checks the CRC32 checksum of every book
//           message, and every --rest-every seconds fetches the public REST
//           Depth snapshot (in a second thread, so the stream is not held up)
//           and compares it with the stream's book at the nearest time and in
//           a window around the request.
//   replay: --replay FILE applies a FrameRecorder file instead (no REST).
//   --ob:   also sends the records through Mbp2Mbo into a real OB (in-process,
//           synchronous, as unit_test/src/test_mbp2mbo_ob.cpp does) and, after
//           a book message every --rest-every seconds, compares OB's size at
//           each of the best --ob-levels levels with Mbp2Mbo's chunks, OB's
//           BBO with Mbp2Mbo's, and Mbp2Mbo's chunks with the sync's book.
//
// Rate limits: one public REST call every 10 s by default; Kraken's public
// REST limit is far above that.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/property_tree/ptree.hpp>

#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "kraken_md/FrameRecorder.hpp"
#include "kraken_md/Https.hpp"
#include "kraken_md/KrakenBookSync.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "sim/Mbp2Mbo.hpp"
#include "sim/WsNet.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace kraken;
using json = nlohmann::json;
using Levels = std::vector<std::pair<int64_t, int64_t>>;

namespace
{
  struct Args
  {
    std::string symbol = "BTC/USD", rest_pair = "XBTUSD", host = "ws.kraken.com";
    int depth = 1000, minutes = 30, rest_every = 10, rest_count = 100, compare_levels = 25;
    double window_s = 1.0;
    std::string record, replay, report;
    bool ob = false;
    int ob_levels = 20;
    double tick = 0.1, chunk = 0.01;
  };

  struct State
  {
    uint64_t recv_ns;
    Levels bids, asks;
  };

  // REST Depth gives quantities rounded to fewer decimals than the stream
  // (3 for XBTUSD against the stream's 8), so a REST level is compared with
  // the stream's level by price, and by quantity at the REST's precision.
  struct RestLevel
  {
    int64_t px = 0, qty = 0;
    int decimals = 0; // of the REST quantity text
  };
  using RestLevels = std::vector<RestLevel>;

  struct RestSnap
  {
    uint64_t sent_ns = 0, recv_ns = 0;
    RestLevels bids, asks;
    std::string error;
  };

  int64_t pow10(int n)
  {
    int64_t p = 1;
    while (n-- > 0)
      p *= 10;
    return p;
  }

  // The stream's quantity rounded up to the REST's decimals equals it (REST
  // Depth rounds up: 0.0000612 is "0.001"; found on the live data); trunc:
  // truncated, for comparison.
  bool qty_round_eq(int64_t stream, const RestLevel &r)
  {
    const int64_t u = pow10(kScale - r.decimals);
    return (stream + u - 1) / u * u == r.qty;
  }
  bool qty_trunc_eq(int64_t stream, const RestLevel &r)
  {
    const int64_t u = pow10(kScale - r.decimals);
    return stream / u * u == r.qty;
  }

  uint64_t now_ns() { return sim::wsnet::now_ns(); }

  RestLevels rest_side(const json &arr, size_t n)
  {
    RestLevels v;
    for (const auto &e : arr)
    {
      if (v.size() >= n)
        break;
      RestLevel l;
      l.px = fixed(e.at(0));
      l.qty = fixed(e.at(1));
      const std::string q = num_text(e.at(1));
      auto dot = q.find('.');
      l.decimals = dot == std::string::npos ? 0 : int(q.size() - dot - 1);
      v.push_back(l);
    }
    return v;
  }

  struct Match
  {
    size_t px = 0, round = 0, trunc = 0;
  };
  // Each REST level looked up by price among the stream's levels.
  Match level_matches(const RestLevels &rest, const Levels &stream)
  {
    Match m;
    for (const auto &r : rest)
    {
      auto it = std::find_if(stream.begin(), stream.end(), [&](const auto &l) { return l.first == r.px; });
      if (it == stream.end())
        continue;
      ++m.px;
      m.round += qty_round_eq(it->second, r);
      m.trunc += qty_trunc_eq(it->second, r);
    }
    return m;
  }
  // The REST side equals the stream's best levels: same prices in the same
  // order, quantities equal at the REST's precision.
  bool side_equal(const RestLevels &rest, const Levels &stream)
  {
    if (stream.size() < rest.size())
      return false;
    for (size_t i = 0; i < rest.size(); ++i)
      if (stream[i].first != rest[i].px || !qty_round_eq(stream[i].second, rest[i]))
        return false;
    return true;
  }

  // The OB chain: Mbp2Mbo -> a real OB, driven synchronously.
  // See tests/test_book_ob.cpp: the venue OB sees.
#ifndef KRAKEN_OB_VENUE
#define KRAKEN_OB_VENUE KRAKEN
#endif
  constexpr en::x kObVenue = en::x::KRAKEN_OB_VENUE;

  struct TestableOB : public frame::ob::act::OB
  {
    using frame::ob::act::OB::OB;
    using frame::ob::act::OB::reply_to;
  };

  struct ObChain
  {
    unit_test::MockActor probe{"probe"};
    std::unique_ptr<TestableOB> ob;
    std::unique_ptr<sim::Mbp2Mbo> m;
    uint64_t records = 0, checks = 0, level_checks = 0, ob_vs_chunks_bad = 0, chunks_vs_sync_bad = 0,
             bbo_checks = 0, bbo_bad = 0;

    ObChain(const Args &a)
    {
      const char *k = std::getenv("KSPRPROJ");
      frame::ref::RefData::set_universe(std::string(k ? k : ".") + "/unit_test/config/universe.csv", "");
      frame::ref::RefData::inst();
      auto as = frame::ref::RefData::add_future_asset("KRAKEN_BTCUSD", kObVenue, 9901, "", a.symbol, 1.0);
      as->maxpx = 1 << 30;
      boost::property_tree::ptree pt;
      pt.put("ladder_ticks", 1 << 19);
      ob = std::make_unique<TestableOB>(nullptr, true, nullptr, as->id, pt);
      m = std::make_unique<sim::Mbp2Mbo>(sim::Mbp2Mbo::Config{9901, char(kObVenue), a.tick, a.chunk},
                                         [this](const bfile::l3_t &r) {
                                           ++records;
                                           frame::mda::msg::Data d(true);
                                           d.l3 = r;
                                           dispatch(&d);
                                         });
    }
    void dispatch(const actors::Message *msg)
    {
      ob->reply_to = &probe;
      unit_test::TestHelper::invoke_handler(ob.get(), msg, &probe);
      ob->reply_to = nullptr;
    }
    int qat(bool bid, int64_t t)
    {
      probe.clear();
      frame::cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}});
      dispatch(&g);
      auto page = probe.get_message<frame::cons::msg::Page>(0);
      int sz = -1, cnt = -1, simq = -1;
      if (page)
        std::sscanf(page->val.c_str(), "QAT: %d %d %d", &sz, &cnt, &simq);
      return sz;
    }
    std::pair<int, int> bbo()
    {
      probe.clear();
      frame::cons::msg::Get g("bbbo");
      dispatch(&g);
      auto page = probe.get_message<frame::cons::msg::Page>(0);
      int b = -1, a = -1;
      if (page)
        std::sscanf(page->val.c_str(), "BBBO: %d %d", &b, &a);
      return {b, a};
    }
    void check(const KrakenBookSync &s, int levels)
    {
      ++checks;
      for (bool bid : {true, false})
        for (const auto &[px, qty] : s.top(bid, size_t(levels)))
        {
          const int64_t t = m->ticks(fixed_to_double(px));
          const size_t want = m->target(fixed_to_double(qty));
          const size_t have = m->chunks(bid, t);
          const int obsz = qat(bid, t);
          ++level_checks;
          if (have != want)
            ++chunks_vs_sync_bad;
          if (obsz != int(have))
          {
            if (ob_vs_chunks_bad < 10)
              std::cerr << "OB " << (bid ? "bid " : "ask ") << t << ": OB " << obsz << " chunks " << have << std::endl;
            ++ob_vs_chunks_bad;
          }
        }
      ++bbo_checks;
      auto [b, a] = bbo();
      if (b != int(m->best(true)) || a != int(m->best(false)))
        ++bbo_bad;
    }
  };

  struct Verifier
  {
    Args a;
    std::unique_ptr<ObChain> chain;
    std::unique_ptr<KrakenBookSync> sync;
    std::unique_ptr<FrameRecorder> rec;
    std::deque<State> history;
    uint64_t frames = 0, book_msgs = 0, mismatches = 0, errors = 0, reconnects = 0;
    uint64_t rest_ok = 0, rest_fail = 0, exact_nearest = 0, exact_window = 0;
    uint64_t lvl_total = 0, lvl_px = 0, lvl_round = 0, lvl_trunc = 0, top1_match = 0;
    uint64_t best_total = 0, best_full = 0;
    std::vector<double> lags_ms;
    double max_nearest_gap_ms = 0;
    uint64_t next_ob_check = 0;
    bool ob_due = false;

    std::mutex mu;
    std::deque<RestSnap> rest_q;

    explicit Verifier(const Args &args) : a(args)
    {
      if (a.ob)
        chain = std::make_unique<ObChain>(a);
      sync = std::make_unique<KrakenBookSync>(a.symbol, size_t(a.depth), chain ? chain->m.get() : nullptr,
                                              size_t(a.depth));
      if (!a.record.empty())
        rec = std::make_unique<FrameRecorder>(a.record);
    }

    // false: resubscribe
    bool apply(uint64_t recv_ns, const std::string &frame)
    {
      ++frames;
      if (rec)
        rec->write(recv_ns, frame);
      json msg;
      try
      {
        msg = parse_raw(frame);
      }
      catch (const std::exception &e)
      {
        ++errors;
        std::cerr << "unparsable frame: " << e.what() << std::endl;
        return false;
      }
      const bool book = msg.value("channel", std::string()) == "book";
      auto r = sync->on_message(msg);
      if (book)
      {
        ++book_msgs;
        if (sync->synced())
        {
          history.push_back(State{recv_ns, sync->top(true, size_t(2 * a.compare_levels)),
                                  sync->top(false, size_t(2 * a.compare_levels))});
          while (!history.empty() && history.front().recv_ns + 15'000'000'000ull < recv_ns)
            history.pop_front();
        }
        if (chain && ob_due && sync->synced())
        {
          chain->check(*sync, a.ob_levels);
          ob_due = false;
        }
      }
      if (r == KrakenBookSync::Result::ChecksumMismatch)
      {
        ++mismatches;
        std::cerr << "checksum mismatch at frame " << frames << ": " << frame.substr(0, 300) << std::endl;
        return false;
      }
      if (r == KrakenBookSync::Result::Error)
      {
        ++errors;
        return false;
      }
      return true;
    }

    void compare(const RestSnap &s)
    {
      if (!s.error.empty())
      {
        ++rest_fail;
        std::cerr << "REST: " << s.error << std::endl;
        return;
      }
      ++rest_ok;
      const uint64_t mid = (s.sent_ns + s.recv_ns) / 2;
      const State *nearest = nullptr;
      bool window = false;
      for (const auto &h : history)
      {
        if (!nearest || std::llabs(int64_t(h.recv_ns - mid)) < std::llabs(int64_t(nearest->recv_ns - mid)))
          nearest = &h;
        const uint64_t w = uint64_t(a.window_s * 1e9);
        if (h.recv_ns + w >= s.sent_ns && h.recv_ns <= s.recv_ns + w && side_equal(s.bids, h.bids) &&
            side_equal(s.asks, h.asks))
          window = true;
      }
      if (!nearest)
        return;
      max_nearest_gap_ms = std::max(max_nearest_gap_ms, std::abs(double(int64_t(nearest->recv_ns - mid))) / 1e6);
      if (side_equal(s.bids, nearest->bids) && side_equal(s.asks, nearest->asks))
        ++exact_nearest;
      if (window)
        ++exact_window;
      if (!s.bids.empty() && !s.asks.empty() && !nearest->bids.empty() && !nearest->asks.empty() &&
          nearest->bids[0].first == s.bids[0].px && nearest->asks[0].first == s.asks[0].px &&
          qty_round_eq(nearest->bids[0].second, s.bids[0]) && qty_round_eq(nearest->asks[0].second, s.asks[0]))
        ++top1_match;
      // The stream state (last 15 s) that agrees with the snapshot on the most
      // levels, and how long before the REST response it was.
      size_t best = 0;
      const State *bs = nullptr;
      for (const auto &h : history)
      {
        auto b = level_matches(s.bids, h.bids), x = level_matches(s.asks, h.asks);
        if (b.round + x.round >= best)
        {
          best = b.round + x.round;
          bs = &h;
        }
      }
      best_total += best;
      if (bs && best == s.bids.size() + s.asks.size())
      {
        ++best_full;
        lags_ms.push_back(double(int64_t(s.recv_ns - bs->recv_ns)) / 1e6);
      }
      if (std::getenv("KRAKEN_VERIFY_DEBUG") && rest_ok <= 2)
        for (size_t i = 0; i < 5; ++i)
          std::cerr << "dbg bid " << (i < nearest->bids.size() ? nearest->bids[i].first : -1) << " "
                    << (i < nearest->bids.size() ? nearest->bids[i].second : -1) << " | " << s.bids[i].px << " "
                    << s.bids[i].qty << "   ask " << (i < nearest->asks.size() ? nearest->asks[i].first : -1) << " "
                    << (i < nearest->asks.size() ? nearest->asks[i].second : -1) << " | " << s.asks[i].px << " "
                    << s.asks[i].qty << std::endl;
      lvl_total += s.bids.size() + s.asks.size();
      auto mb = level_matches(s.bids, nearest->bids), ma = level_matches(s.asks, nearest->asks);
      lvl_px += mb.px + ma.px;
      lvl_round += mb.round + ma.round;
      lvl_trunc += mb.trunc + ma.trunc;
    }

    void drain_rest()
    {
      std::deque<RestSnap> q;
      {
        std::lock_guard<std::mutex> l(mu);
        q.swap(rest_q);
      }
      for (const auto &s : q)
        compare(s);
    }

    void rest_loop(std::atomic<bool> &stop)
    {
      const std::string target = "/0/public/Depth?pair=" + a.rest_pair + "&count=" + std::to_string(a.rest_count);
      auto next = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!stop)
      {
        std::this_thread::sleep_until(next);
        next += std::chrono::seconds(a.rest_every);
        if (stop)
          break;
        RestSnap s;
        s.sent_ns = now_ns();
        try
        {
          auto r = https_request("GET", "api.kraken.com", target);
          s.recv_ns = now_ns();
          if (r.status != 200)
            s.error = "HTTP " + std::to_string(r.status);
          else
          {
            auto j = parse_raw(r.body);
            if (!j.at("error").empty())
              s.error = j.at("error").dump();
            else
            {
              const auto &book = j.at("result").begin().value();
              s.bids = rest_side(book.at("bids"), size_t(a.compare_levels));
              s.asks = rest_side(book.at("asks"), size_t(a.compare_levels));
            }
          }
        }
        catch (const std::exception &e)
        {
          s.recv_ns = now_ns();
          s.error = e.what();
        }
        std::lock_guard<std::mutex> l(mu);
        rest_q.push_back(std::move(s));
      }
    }

    void status(double elapsed_s)
    {
      const auto &st = sync->stats();
      std::cout << std::fixed;
      std::cout.precision(0);
      std::cout << "t=" << elapsed_s << "s frames " << frames << " book " << book_msgs << " snapshots "
                << st.snapshots << " updates " << st.updates << " checksum ok " << st.checksum_ok << " bad "
                << st.checksum_bad << " crossed " << st.crossed << " trades " << st.trades << " trade_id gaps "
                << st.trade_id_gaps << " REST " << rest_ok << "/" << (rest_ok + rest_fail) << " equal(nearest) "
                << exact_nearest << " equal(window) " << exact_window << " levels px " << lvl_px << " qty(ceil) "
                << lvl_round << " qty(trunc) " << lvl_trunc << " of " << lvl_total << " best-state " << best_total
                << " full " << best_full;
      if (chain)
        std::cout << " OB checks " << chain->checks << " OB!=chunks " << chain->ob_vs_chunks_bad
                  << " chunks!=book " << chain->chunks_vs_sync_bad << " bbo bad " << chain->bbo_bad;
      std::cout << std::endl;
    }

    json lag_summary()
    {
      if (lags_ms.empty())
        return json::object();
      auto v = lags_ms;
      std::sort(v.begin(), v.end());
      return {{"n", v.size()}, {"min", v.front()}, {"median", v[v.size() / 2]}, {"p90", v[v.size() * 9 / 10]},
              {"max", v.back()}};
    }

    void summary(double elapsed_s)
    {
      const auto &st = sync->stats();
      json r = {
          {"symbol", a.symbol},
          {"depth", a.depth},
          {"mode", a.replay.empty() ? "live" : "replay"},
          {"seconds", elapsed_s},
          {"frames", frames},
          {"book_messages", book_msgs},
          {"snapshots", st.snapshots},
          {"updates", st.updates},
          {"levels_applied", st.levels_applied},
          {"checksum_ok", st.checksum_ok},
          {"checksum_bad", st.checksum_bad},
          {"checksum_missing", st.checksum_missing},
          {"truncated_levels", st.truncated},
          {"crossed", st.crossed},
          {"heartbeats", st.heartbeats},
          {"trades", st.trades},
          {"trade_id_gaps", st.trade_id_gaps},
          {"trade_id_missing", st.trade_id_missing},
          {"errors", errors + st.errors},
          {"resubscribes", reconnects},
          {"rest_ok", rest_ok},
          {"rest_failed", rest_fail},
          {"rest_compare_levels", a.compare_levels},
          {"rest_exact_nearest", exact_nearest},
          {"rest_exact_in_window", exact_window},
          {"rest_window_s", a.window_s},
          {"rest_top1_match", top1_match},
          {"rest_levels_total", lvl_total},
          {"rest_levels_price_match", lvl_px},
          {"rest_levels_qty_match_ceil", lvl_round},
          {"rest_levels_qty_match_truncated", lvl_trunc},
          {"rest_max_nearest_gap_ms", max_nearest_gap_ms},
          {"rest_levels_matching_best_state", best_total},
          {"rest_snapshots_fully_matched_by_some_state", best_full},
          {"rest_lag_ms_of_full_match", lag_summary()},
      };
      if (chain)
        r["ob"] = {{"records", chain->records},
                   {"checks", chain->checks},
                   {"level_checks", chain->level_checks},
                   {"ob_vs_chunks_mismatch", chain->ob_vs_chunks_bad},
                   {"chunks_vs_book_mismatch", chain->chunks_vs_sync_bad},
                   {"bbo_checks", chain->bbo_checks},
                   {"bbo_mismatch", chain->bbo_bad}};
      std::cout << r.dump(2) << std::endl;
      if (!a.report.empty())
        std::ofstream(a.report) << r.dump(2) << std::endl;
    }

    int run_replay()
    {
      FrameReader in(a.replay);
      if (!in.ok())
      {
        std::cerr << "cannot read " << a.replay << std::endl;
        return 2;
      }
      uint64_t t, first = 0, last = 0, last_check = 0;
      std::string f;
      while (in.next(t, f))
      {
        if (!first)
          first = t;
        last = t;
        if (chain && t >= last_check + uint64_t(a.rest_every) * 1'000'000'000ull)
        {
          ob_due = true;
          last_check = t;
        }
        if (!apply(t, f))
        {
          ++reconnects; // a live run would resubscribe here; the recording goes on
          sync->reset();
        }
      }
      summary(double(last - first) / 1e9);
      return 0;
    }

    int run_live()
    {
      using namespace sim::wsnet;
      std::atomic<bool> stop{false};
      std::thread rest([&] { rest_loop(stop); });
      const auto t0 = std::chrono::steady_clock::now();
      const auto end = t0 + std::chrono::minutes(a.minutes);
      auto next_status = t0 + std::chrono::seconds(60);
      auto next_ob = t0 + std::chrono::seconds(a.rest_every);
      while (std::chrono::steady_clock::now() < end)
      {
        try
        {
          net::io_context io;
          auto tls = make_tls();
          WsStream ws(io, tls);
          ws.read_message_max(16 * 1024 * 1024);
          ws_connect(io, ws, a.host, "443", "/v2", std::chrono::seconds(10));
          const auto sym = json::array({a.symbol});
          ws_write(io, ws,
                   json{{"method", "subscribe"},
                        {"params", {{"channel", "book"}, {"symbol", sym}, {"depth", a.depth}}}}
                       .dump());
          ws_write(io, ws,
                   json{{"method", "subscribe"},
                        {"params", {{"channel", "trade"}, {"symbol", sym}, {"snapshot", false}}}}
                       .dump());
          beast::flat_buffer buf;
          bool ok = true;
          while (ok && std::chrono::steady_clock::now() < end)
          {
            const std::string frame = ws_read(io, ws, buf);
            const auto now = std::chrono::steady_clock::now();
            if (chain && now >= next_ob)
            {
              ob_due = true;
              next_ob = now + std::chrono::seconds(a.rest_every);
            }
            ok = apply(now_ns(), frame);
            drain_rest();
            if (now >= next_status)
            {
              status(std::chrono::duration<double>(now - t0).count());
              next_status += std::chrono::seconds(60);
            }
          }
          beast::error_code ec;
          ws.close(websocket::close_code::normal, ec);
        }
        catch (const std::exception &e)
        {
          std::cerr << "connection: " << e.what() << std::endl;
        }
        if (std::chrono::steady_clock::now() < end)
        {
          ++reconnects;
          sync->reset();
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
      }
      stop = true;
      rest.join();
      drain_rest();
      if (rec)
        rec->flush();
      summary(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
      return 0;
    }
  };
}

int main(int argc, char **argv)
{
  Args a;
  for (int i = 1; i < argc; ++i)
  {
    std::string k = argv[i];
    auto val = [&]() -> std::string {
      if (i + 1 >= argc)
      {
        std::cerr << k << " needs a value" << std::endl;
        std::exit(2);
      }
      return argv[++i];
    };
    if (k == "--symbol") a.symbol = val();
    else if (k == "--rest-pair") a.rest_pair = val();
    else if (k == "--depth") a.depth = std::stoi(val());
    else if (k == "--minutes") a.minutes = std::stoi(val());
    else if (k == "--rest-every") a.rest_every = std::stoi(val());
    else if (k == "--rest-count") a.rest_count = std::stoi(val());
    else if (k == "--compare-levels") a.compare_levels = std::stoi(val());
    else if (k == "--window") a.window_s = std::stod(val());
    else if (k == "--record") a.record = val();
    else if (k == "--replay") a.replay = val();
    else if (k == "--report") a.report = val();
    else if (k == "--ob") a.ob = true;
    else if (k == "--ob-levels") a.ob_levels = std::stoi(val());
    else if (k == "--tick") a.tick = std::stod(val());
    else if (k == "--chunk") a.chunk = std::stod(val());
    else
    {
      std::cerr << "usage: kraken_verify [--minutes 30] [--symbol BTC/USD] [--rest-pair XBTUSD] [--depth 1000]\n"
                   "         [--rest-every 10] [--rest-count 100] [--compare-levels 25] [--window 1.0]\n"
                   "         [--record FILE] [--replay FILE] [--report FILE.json]\n"
                   "         [--ob] [--ob-levels 20] [--tick 0.1] [--chunk 0.01]\n";
      return 2;
    }
  }
  if (a.compare_levels > a.rest_count)
    a.compare_levels = a.rest_count;
  Verifier v(a);
  return a.replay.empty() ? v.run_live() : v.run_replay();
}
