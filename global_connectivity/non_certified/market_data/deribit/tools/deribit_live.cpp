/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// deribit_live: the live check of the public JSON-RPC book (testing ladder
// step 2). No account, no API key.
//
//   deribit_live [--instrument BTC-PERPETUAL] [--minutes 30] [--rest-every 10]
//                [--rest-depth 20] [--record frames.bin] [--replay frames.bin]
//                [--tick 0.5] [--chunk 100] [--depth 50] [--no-ob]
//
// For --minutes it reads book.<instrument>.100ms and trades.<instrument>.100ms
// from wss://www.deribit.com and checks, continuously:
//   - the change_id chain (every gap and resync is counted, with its reason);
//   - no crossed book after any notification;
//   - against public/get_order_book (REST, every --rest-every seconds, depth
//     --rest-depth): when the snapshot's change_id is one the stream has
//     delivered, the book at exactly that change_id, level by level; when it
//     is not (it falls inside a 100 ms aggregate), the share of the top levels
//     that match the stream's book nearest in change_id;
//   - the chain DeribitBookSync -> LevelWindow -> Mbp2Mbo -> OB, in process:
//     after every notification, OB's best bid and offer and its size at every
//     level Mbp2Mbo holds in the window, against Mbp2Mbo's.
// REST requests: one per --rest-every seconds, far below the public limit
// (feed.md 2). --replay runs the same checks (without REST) on recorded frames.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <boost/property_tree/ptree.hpp>

#include "bfile/r_l3.hpp"
#include "deribit_md/DeribitBookSync.hpp"
#include "deribit_md/DeribitWs.hpp"
#include "deribit_md/FrameRecorder.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "sim/Mbp2Mbo.hpp"
#include "sim/WsNet.hpp"

using namespace sim::wsnet;
using nlohmann::json;

namespace
{
  struct Opts
  {
    std::string instrument = "BTC-PERPETUAL";
    double minutes = 30;
    int rest_every = 10;
    int rest_depth = 20;
    std::string record, replay;
    double tick = 0.5, chunk = 1000;
    int depth = 50;
    bool ob = true;
  };

  // The probe the OB answers console requests to.
  class Probe : public actors::Actor
  {
  public:
    Probe() { strncpy(name, "probe", sizeof(name) - 1); }
    std::string last;
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      if (auto p = dynamic_cast<const frame::cons::msg::Page *>(m))
        last = p->val;
    }
  };

  struct TestableOB : public frame::ob::act::OB
  {
    using frame::ob::act::OB::OB;
    using frame::ob::act::OB::reply_to;
  };

  void dispatch(actors::Actor *a, const actors::Message *m, actors::Actor *sender)
  {
    const_cast<actors::Message *>(m)->sender = sender;
    auto it = a->handlers.find(std::type_index(typeid(*m)));
    if (it != a->handlers.end())
      (a->*(it->second))(m);
  }

  struct Chain
  {
    Probe probe;
    std::unique_ptr<TestableOB> ob;
    int sym = 0;
    uint64_t records = 0;
    uint64_t checks = 0, bbo_mismatch = 0, level_checks = 0, level_mismatch = 0;

    std::string ask(const char *cmd, std::map<std::string, std::string> args = {})
    {
      frame::cons::msg::Get g(cmd, args);
      probe.last.clear();
      ob->reply_to = &probe;
      dispatch(ob.get(), &g, &probe);
      ob->reply_to = nullptr;
      return probe.last;
    }
    std::pair<int, int> bbo()
    {
      int b = -1, a = -1;
      std::sscanf(ask("bbbo").c_str(), "BBBO: %d %d", &b, &a);
      return {b, a};
    }
    int qat(bool bid, int64_t t)
    {
      int sz = -1, cnt = -1, sim = -1;
      std::sscanf(ask("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}}).c_str(),
                  "QAT: %d %d %d", &sz, &cnt, &sim);
      return sz;
    }
  };

  struct RestResult
  {
    uint64_t at_ns = 0;
    json body;
  };

  std::string http_get(const std::string &host, const std::string &target)
  {
    net::io_context io;
    auto tls = make_tls();
    beast::ssl_stream<beast::tcp_stream> s(io, tls);
    tls_connect(io, s, host, "443");
    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "kaspar-hft deribit_live");
    beast::get_lowest_layer(s).expires_after(kRestTimeout);
    await(io, [&](auto h) { http::async_write(s, req, std::move(h)); });
    beast::flat_buffer buf;
    http::response_parser<http::string_body> parser;
    parser.body_limit(16 * 1024 * 1024);
    await(io, [&](auto h) { http::async_read(s, buf, parser, std::move(h)); });
    auto res = parser.release();
    beast::error_code ec;
    s.shutdown(ec);
    if (res.result() != http::status::ok)
      throw std::runtime_error("HTTP " + std::to_string(res.result_int()) + " " + res.body().substr(0, 200));
    return res.body();
  }

  struct Rest
  {
    uint64_t fetched = 0, failed = 0;
    uint64_t exact = 0, exact_match = 0;           // snapshots at a change_id the stream delivered
    uint64_t inexact = 0;                          // snapshots inside an aggregate
    uint64_t inexact_levels = 0, inexact_levels_match = 0;   // against the book just before
    uint64_t bracket_levels = 0, bracket_levels_match = 0;   // equal to the book just before or just after
    uint64_t not_bracketed = 0;                               // no kept book on one side of the change_id
    uint64_t stream_behind = 0;                    // REST newer than every notification kept
    uint64_t exact_level_mismatch = 0;
  };

  // Level-by-level comparison of `n` levels per side; returns levels equal.
  size_t compare(const deribit::DeribitBookSync::Top &t, const json &r, double tick, size_t &total)
  {
    size_t eq = 0;
    for (const char *side : {"bids", "asks"})
    {
      const auto &mine = std::strcmp(side, "bids") == 0 ? t.bids : t.asks;
      const auto &theirs = r.at(side);
      for (size_t i = 0; i < theirs.size(); ++i)
      {
        ++total;
        if (i >= mine.size())
          continue;
        const int64_t px = std::llround(theirs[i].at(0).get<double>() / tick);
        const double q = theirs[i].at(1).get<double>();
        if (mine[i].first == px && std::abs(mine[i].second - q) < 1e-9)
          ++eq;
      }
    }
    return eq;
  }
}

int main(int argc, char **argv)
{
  Opts o;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--instrument") o.instrument = next();
    else if (a == "--minutes") o.minutes = std::stod(next());
    else if (a == "--rest-every") o.rest_every = std::stoi(next());
    else if (a == "--rest-depth") o.rest_depth = std::stoi(next());
    else if (a == "--record") o.record = next();
    else if (a == "--replay") o.replay = next();
    else if (a == "--tick") o.tick = std::stod(next());
    else if (a == "--chunk") o.chunk = std::stod(next());
    else if (a == "--depth") o.depth = std::stoi(next());
    else if (a == "--no-ob") o.ob = false;
    else
    {
      std::cerr << "unknown option " << a << std::endl;
      return 2;
    }
  }

  // The chain: Mbp2Mbo into a real OB, as bnsim registers the asset.
  Chain chain;
  std::unique_ptr<sim::Mbp2Mbo> m;
  if (o.ob)
  {
    const char *k = std::getenv("KSPRPROJ");
    frame::ref::RefData::set_universe(std::string(k ? k : ".") + "/unit_test/config/universe.csv", "");
    frame::ref::RefData::inst();
    auto a = frame::ref::RefData::add_future_asset(o.instrument + "_DERIBIT", en::x::DERIBIT, 993,
                                                   "", o.instrument, 1.0);
    if (!a)
    {
      std::cerr << "add_future_asset failed" << std::endl;
      return 1;
    }
    a->maxpx = 1 << 30;
    chain.sym = a->id;
    boost::property_tree::ptree pt;
    pt.put("ladder_ticks", 1 << 16);
    chain.ob = std::make_unique<TestableOB>(nullptr, true, nullptr, chain.sym, pt);
    m = std::make_unique<sim::Mbp2Mbo>(
        sim::Mbp2Mbo::Config{993, char(en::x::DERIBIT), o.tick, o.chunk},
        [&chain](const bfile::l3_t &r) {
          ++chain.records;
          frame::mda::msg::Data d(true);
          d.l3 = r;
          dispatch(chain.ob.get(), &d, &chain.probe);
        });
  }
  deribit::DeribitBookSync sync(o.instrument, o.tick, m.get(), size_t(o.depth));
  sync.keep_history(size_t(o.rest_depth), 600);   // 60 s of 100 ms notifications
  if (m)
    sync.on_resync([&chain](uint64_t) {
      bfile::l3_chr_v2_t c{};
      c.typ = en::l3::CHR_V2;
      c.venue = char(en::x::DERIBIT);
      frame::mda::msg::Data d(true);
      d.l3 = c;
      dispatch(chain.ob.get(), &d, &chain.probe);
    });

  std::unique_ptr<deribit::FrameRecorder> rec;
  if (!o.record.empty())
    rec = std::make_unique<deribit::FrameRecorder>(o.record);

  std::mutex mu;
  std::deque<RestResult> rest_q;
  Rest rest;
  std::atomic<bool> stop{false};
  uint64_t resyncs = 0, reconnects = 0, frames = 0, heartbeats = 0;
  std::map<std::string, uint64_t> resync_reasons;

  auto check_chain = [&]() {
    if (!m || !sync.synced())
      return;
    ++chain.checks;
    const auto [b, a] = chain.bbo();
    if (b != int(m->best(true)) || a != int(m->best(false)))
    {
      if (chain.bbo_mismatch < 5)
        std::cerr << "OB bbo " << b << " " << a << " vs Mbp2Mbo " << m->best(true) << " "
                  << m->best(false) << std::endl;
      ++chain.bbo_mismatch;
    }
    // Every level of the window, both sides.
    const auto t = sync.top(size_t(o.depth));
    for (int s = 0; s < 2; ++s)
      for (const auto &[px, q] : (s == 0 ? t.bids : t.asks))
      {
        ++chain.level_checks;
        if (chain.qat(s == 0, px) != int(m->chunks(s == 0, px)))
          ++chain.level_mismatch;
      }
  };

  auto handle_rest = [&]() {
    // A snapshot newer than the stream's last notification waits (up to 5 s)
    // for the stream to pass its change_id, so that it is compared with the
    // books on both sides of it.
    std::deque<RestResult> q, later;
    {
      std::lock_guard<std::mutex> l(mu);
      q.swap(rest_q);
    }
    const uint64_t now = now_ns();
    for (auto &r : q)
    {
      const auto &res = r.body.at("result");
      const int64_t cid = res.at("change_id").get<int64_t>();
      if (cid > sync.change_id() && now - r.at_ns < 5000000000ull && !stop)
      {
        later.push_back(std::move(r));
        continue;
      }
      size_t total = 0;
      if (const auto *t = sync.at_change_id(cid))
      {
        ++rest.exact;
        const size_t eq = compare(*t, res, o.tick, total);
        if (eq == total)
          ++rest.exact_match;
        else
        {
          rest.exact_level_mismatch += total - eq;
          std::cerr << "REST change_id " << cid << ": " << total - eq << " of " << total
                    << " levels differ" << std::endl;
        }
      }
      else
      {
        ++rest.inexact;
        if (cid > sync.change_id())
          ++rest.stream_behind;
        const auto [before, after] = sync.around(cid);
        if (!before || !after)
        {
          ++rest.not_bracketed;
          continue;
        }
        rest.inexact_levels_match += compare(*before, res, o.tick, total);
        rest.inexact_levels += total;
        // A level counts as matching the bracket when it equals the book
        // just before the snapshot's change_id or the book just after.
        for (const char *side : {"bids", "asks"})
        {
          const bool bid = std::strcmp(side, "bids") == 0;
          const auto &theirs = res.at(side);
          for (size_t i = 0; i < theirs.size(); ++i)
          {
            ++rest.bracket_levels;
            const int64_t px = std::llround(theirs[i].at(0).get<double>() / o.tick);
            const double q = theirs[i].at(1).get<double>();
            auto eq = [&](const deribit::DeribitBookSync::Top *t) {
              const auto &v = bid ? t->bids : t->asks;
              return i < v.size() && v[i].first == px && std::abs(v[i].second - q) < 1e-9;
            };
            if (eq(before) || eq(after))
              ++rest.bracket_levels_match;
          }
        }
      }
    }
    if (!later.empty())
    {
      std::lock_guard<std::mutex> l(mu);
      for (auto &r : later)
        rest_q.push_front(std::move(r));
    }
  };

  const auto t_end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(int64_t(o.minutes * 60000));

  std::thread rest_thread;
  if (o.replay.empty() && o.rest_every > 0)
    rest_thread = std::thread([&] {
      const std::string target = "/api/v2/public/get_order_book?instrument_name=" + o.instrument +
                                 "&depth=" + std::to_string(o.rest_depth);
      while (!stop)
      {
        for (int i = 0; i < o.rest_every * 10 && !stop; ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stop)
          break;
        try
        {
          auto body = json::parse(http_get(deribit::ws::kProdHost, target));
          std::lock_guard<std::mutex> l(mu);
          rest_q.push_back({now_ns(), std::move(body)});
          ++rest.fetched;
        }
        catch (const std::exception &e)
        {
          ++rest.failed;
          std::cerr << "REST: " << e.what() << std::endl;
        }
      }
    });

  auto on_frame = [&](const std::string &text) {
    ++frames;
    const auto msg = json::parse(text);
    if (msg.value("method", std::string()) == "heartbeat")
      ++heartbeats;
    if (!sync.on_message(msg))
    {
      ++resyncs;
      ++resync_reasons["change_id gap"];
      return false;
    }
    if (msg.value("method", std::string()) == "subscription" &&
        msg["params"].value("channel", std::string()).rfind("book.", 0) == 0)
      check_chain();
    handle_rest();
    return true;
  };

  if (!o.replay.empty())
  {
    deribit::FrameReader rd(o.replay);
    uint64_t ns;
    std::string text;
    while (rd.next(ns, text))
      if (!on_frame(text))
        sync.reset();
  }
  else
  {
    int64_t id = 1;
    while (std::chrono::steady_clock::now() < t_end)
    {
      try
      {
        net::io_context io;
        auto tls = make_tls();
        WsStream ws(io, tls);
        ws.read_message_max(64 * 1024 * 1024);
        ws_connect(io, ws, deribit::ws::kProdHost, "443", deribit::ws::kPath, std::chrono::seconds(75));
        ws_write(io, ws, deribit::ws::set_heartbeat(id++, 30).dump());
        ws_write(io, ws,
                 deribit::ws::subscribe(id++, deribit::ws::channels(o.instrument, "100ms")).dump());
        beast::flat_buffer buf;
        bool gap = false;
        while (!gap && std::chrono::steady_clock::now() < t_end)
        {
          const auto text = ws_read(io, ws, buf);
          if (rec)
            rec->write(now_ns(), text);
          const auto msg = json::parse(text);
          if (auto reply = deribit::ws::heartbeat_reply(msg, id); !reply.is_null())
          {
            ++id;
            ws_write(io, ws, reply.dump());
          }
          if (msg.contains("error"))
            throw std::runtime_error("error: " + msg["error"].dump());
          gap = !on_frame(text);
        }
        beast::error_code ec;
        ws.close(websocket::close_code::normal, ec);
      }
      catch (const std::exception &e)
      {
        ++reconnects;
        ++resync_reasons[std::string("connection: ") + e.what()];
        std::cerr << "connection: " << e.what() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      sync.reset();
    }
  }
  stop = true;
  if (rest_thread.joinable())
    rest_thread.join();
  handle_rest();

  const auto &s = sync.stats();
  std::cout << "instrument " << o.instrument << "\n"
            << "frames " << frames << " heartbeats " << heartbeats << "\n"
            << "snapshots " << s.snapshots << " notifications " << s.notifications
            << " level_updates " << s.level_updates << " trades " << s.trades << "\n"
            << "gaps " << s.gaps << " resyncs " << resyncs << " reconnects " << reconnects
            << " bad_actions " << s.bad_actions << " unknown_action " << s.unknown_action
            << " crossed " << s.crossed << "\n"
            << "rest fetched " << rest.fetched << " failed " << rest.failed << " exact "
            << rest.exact << " exact_match " << rest.exact_match << " exact_level_mismatch "
            << rest.exact_level_mismatch << " inexact " << rest.inexact << " stream_behind "
            << rest.stream_behind << " inexact_levels " << rest.inexact_levels
            << " inexact_levels_match " << rest.inexact_levels_match << " bracket_levels "
            << rest.bracket_levels << " bracket_levels_match " << rest.bracket_levels_match
            << " not_bracketed " << rest.not_bracketed << "\n"
            << "ob records " << chain.records << " checks " << chain.checks << " bbo_mismatch "
            << chain.bbo_mismatch << " level_checks " << chain.level_checks << " level_mismatch "
            << chain.level_mismatch << "\n";
  for (const auto &[r, n] : resync_reasons)
    std::cout << "resync reason: " << r << " x" << n << "\n";
  if (rec)
    std::cout << "recorded " << rec->frames() << " frames to " << o.record << "\n";
  return 0;
}
