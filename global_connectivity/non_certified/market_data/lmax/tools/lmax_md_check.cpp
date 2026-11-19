/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// lmax_md_check: run LmaxMdFeed for a while and check what it produced.
//
//   lmax_md_check CONFIG.ini MINUTES      against the host in CONFIG (demo or uat only)
//   lmax_md_check --sim MINUTES           against SimLmaxGateway on 127.0.0.1
//
// For every W of every instrument: the book rebuilt from the records sent to
// OB equals LMAX's best `depth` levels (snapshot against the records it
// produced), the rebuilt book is not crossed, and LMAX's own book is counted
// when crossed or one-sided. For the session: delivered MsgSeqNums follow one
// another (QuickFIX gaps show as ResendRequests), logons, rejects, forced
// reconnects. A summary is printed every minute and at the end; the exit
// status is 0 only if no rebuilt book differed from LMAX's and none was
// crossed. Credentials are read from the key file the config names; nothing
// is written except the FIX log, where the password is redacted.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"

#include "lmax_fix/SimLmaxGateway.hpp"
#include "lmax_md/BookCheck.hpp"
#include "lmax_md/act/LmaxMdFeed.hpp"

using namespace lmax;
using namespace lmax::md;

namespace
{
  std::atomic<bool> g_stop{false};
  void on_signal(int) { g_stop = true; }

  class Tap : public MdTap
  {
  public:
    Tap(const std::vector<Instrument> &ins, size_t depth) : ins_(ins), depth_(depth)
    {
      for (auto &i : ins)
        checks_.emplace_back(i.sec_id, i.tick, i.chunk);
      per_.resize(ins.size());
    }
    void on_record(size_t i, const bfile::l3_t &r) override
    {
      std::lock_guard<std::mutex> l(mu_);
      checks_[i].on_record(r);
    }
    void on_snapshot(size_t i, const MdSnapshot &s, const LmaxMdSync &sync) override
    {
      std::lock_guard<std::mutex> l(mu_);
      auto &p = per_[i];
      ++p.w;
      std::string why;
      if (checks_[i].compare(sync.bids(), sync.asks(), depth_, why))
      {
        ++p.mismatch;
        if (p.mismatch <= 3)
          std::cout << "MISMATCH " << ins_[i].symbol << " seq " << s.seq << ": " << why << std::endl;
      }
      if (checks_[i].crossed())
        ++p.crossed_rebuilt;
      p.stats = sync.stats();
      p.levels_b = sync.bids().size();
      p.levels_a = sync.asks().size();
      if (!sync.bids().empty() && !sync.asks().empty())
      {
        p.bid = sync.bids().front().first;
        p.ask = sync.asks().front().first;
      }
      if (p.w <= 2)
      {
        std::cout << "first W " << ins_[i].symbol << " seq " << s.seq << ":";
        for (auto &e : s.entries)
          std::cout << " " << e.type << ":" << e.px << "x" << e.size << (e.time.empty() ? "" : "@" + e.time);
        std::cout << std::endl;
      }
    }
    void on_session(const std::string &e) override
    {
      std::cout << "session: " << e << std::endl;
    }
    bool report(const LmaxMdFeed &f, bool final)
    {
      std::lock_guard<std::mutex> l(mu_);
      auto &c = f.counters();
      std::printf("%s: messages %llu, logons %llu, logouts %llu, resend requests %llu, forced reconnects %llu, "
                  "seq breaks %llu, md rejects %llu, session rejects %llu, unparsed %llu\n",
                  final ? "FINAL" : "minute", (unsigned long long)c.messages, (unsigned long long)c.logons,
                  (unsigned long long)c.logouts, (unsigned long long)c.resend_requests,
                  (unsigned long long)c.forced_reconnects, (unsigned long long)c.seq_breaks,
                  (unsigned long long)c.md_rejects, (unsigned long long)c.session_rejects,
                  (unsigned long long)c.bad_messages);
      bool ok = true;
      for (size_t i = 0; i < per_.size(); ++i)
      {
        auto &p = per_[i];
        std::printf("  %-8s W %llu  top %.6f/%.6f  levels %zu/%zu  mismatches %llu  rebuilt crossed %llu  "
                    "LMAX crossed %llu one-sided %llu  trades applied %llu repeated %llu first-book %llu "
                    "unclassified %llu  bad entries %llu  resyncs %llu\n",
                    ins_[i].symbol.c_str(), (unsigned long long)p.w, p.bid, p.ask, p.levels_b, p.levels_a,
                    (unsigned long long)p.mismatch, (unsigned long long)p.crossed_rebuilt,
                    (unsigned long long)p.stats.crossed, (unsigned long long)p.stats.one_sided,
                    (unsigned long long)p.stats.trades_applied, (unsigned long long)p.stats.trades_repeated,
                    (unsigned long long)p.stats.trades_first_book, (unsigned long long)p.stats.trades_unclassified,
                    (unsigned long long)p.stats.bad_entries, (unsigned long long)p.stats.resyncs);
        ok = ok && p.mismatch == 0 && p.crossed_rebuilt == 0 && p.w > 0;
      }
      std::fflush(stdout);
      return ok;
    }

  private:
    struct Per
    {
      uint64_t w = 0, mismatch = 0, crossed_rebuilt = 0;
      LmaxMdSync::Stats stats;
      size_t levels_b = 0, levels_a = 0;
      double bid = 0, ask = 0;
    };
    std::mutex mu_;
    std::vector<Instrument> ins_;
    size_t depth_;
    std::vector<BookCheck> checks_;
    std::vector<Per> per_;
  };
}

int main(int argc, char **argv)
{
  if (argc != 3)
  {
    std::cerr << "usage: lmax_md_check CONFIG.ini MINUTES | --sim MINUTES" << std::endl;
    return 2;
  }
  const double minutes = std::atof(argv[2]);
  MdFeedConfig cfg;
  std::unique_ptr<fix::SimLmaxGateway> sim;
  if (std::string(argv[1]) == "--sim")
  {
    fix::SimLmaxGateway::Config g;
    g.md_dictionary = LMAX_MD_DICT;
    g.trading_dictionary = LMAX_MD_DICT; // the trading session is not used here
    g.instruments = {{"4001", 1.15230, 0.00001, 0.1}, {"4004", 155.0, 0.001, 0.1}};
    sim = std::make_unique<fix::SimLmaxGateway>(g);
    sim->start();
    cfg.session.env = fix::Env::Loopback;
    cfg.session.host = "127.0.0.1";
    cfg.session.port = sim->port();
    cfg.session.tls = false;
    cfg.session.sender = "CLIENTMD";
    cfg.session.target = "LMXBDM";
    cfg.session.dictionary = LMAX_MD_DICT;
    cfg.session.start_time = cfg.session.end_time = "00:00:00";
    cfg.username = "user";
    cfg.password = "pass";
    cfg.instruments = {{"4001", "EUR/USD", 7001, 0.00001, 0.1}, {"4004", "USD/JPY", 7004, 0.001, 0.1}};
  }
  else
  {
    std::string err;
    if (!MdFeedConfig::from_file(argv[1], cfg, err))
    {
      std::cerr << "lmax_md_check: " << err << std::endl;
      return 2;
    }
  }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  Tap tap(cfg.instruments, size_t(cfg.depth));
  auto *feed = new LmaxMdFeed(cfg, std::vector<actors::Actor *>(cfg.instruments.size(), nullptr), &tap);
  actors::Manager mgr("lmax_md_check");
  mgr.manage_and_start(feed);
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(int64_t(minutes * 60000));
  auto next = std::chrono::steady_clock::now() + std::chrono::minutes(1);
  while (!g_stop && std::chrono::steady_clock::now() < end)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (std::chrono::steady_clock::now() >= next)
    {
      tap.report(*feed, false);
      next += std::chrono::minutes(1);
    }
  }
  mgr.end();
  const bool ok = tap.report(*feed, true);
  delete feed;
  if (sim)
    sim->stop();
  std::cout << (ok ? "PASS" : "FAIL") << std::endl;
  return ok ? 0 : 1;
}
