/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LmaxMdFeed over QuickFIX against SimLmaxGateway on 127.0.0.1: logon,
// subscription, books checked after every W, gaps (filled and not filled),
// disconnects, rejects, and the environment guard.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"

#include "lmax_fix/SimLmaxGateway.hpp"
#include "lmax_md/BookCheck.hpp"
#include "lmax_md/act/LmaxMdFeed.hpp"

using namespace lmax;
using namespace lmax::md;
using namespace std::chrono_literals;

namespace
{
  template <class P>
  bool wait_until(P p, std::chrono::milliseconds limit = 10s)
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (p())
        return true;
      std::this_thread::sleep_for(5ms);
    }
    return p();
  }

  const std::string TRADING_DICT = std::string(LMAX_MD_DICT).substr(0, std::string(LMAX_MD_DICT).find("market_data")) +
                                   "order_entry/lmax/dict/LMAX-FIX44-Trading.xml";

  // Checks every instrument's rebuilt book after each W.
  class CheckTap : public MdTap
  {
  public:
    explicit CheckTap(const std::vector<Instrument> &ins)
    {
      for (auto &i : ins)
        checks.emplace_back(i.sec_id, i.tick, i.chunk);
      snaps.resize(ins.size());
    }
    void on_record(size_t i, const bfile::l3_t &r) override
    {
      std::lock_guard<std::mutex> l(mu);
      checks[i].on_record(r);
      if (std::holds_alternative<bfile::l3_chr_v2_t>(r))
        ++clears;
      if (std::holds_alternative<bfile::l3_gap_v2_t>(r))
        ++gaps;
    }
    void on_snapshot(size_t i, const MdSnapshot &s, const LmaxMdSync &sync) override
    {
      std::lock_guard<std::mutex> l(mu);
      ++snaps[i];
      std::string why;
      if (checks[i].compare(sync.bids(), sync.asks(), 20, why))
      {
        ++mismatches;
        last_why = why;
      }
      if (checks[i].crossed())
        ++crossed;
      trades_applied = sync.stats().trades_applied;
    }
    void on_session(const std::string &e) override
    {
      std::lock_guard<std::mutex> l(mu);
      events.push_back(e);
    }
    size_t snapshots(size_t i)
    {
      std::lock_guard<std::mutex> l(mu);
      return snaps[i];
    }
    bool saw(const std::string &prefix)
    {
      std::lock_guard<std::mutex> l(mu);
      for (auto &e : events)
        if (e.rfind(prefix, 0) == 0)
          return true;
      return false;
    }
    std::mutex mu;
    std::vector<BookCheck> checks;
    std::vector<size_t> snaps;
    std::vector<std::string> events;
    size_t mismatches = 0, crossed = 0, clears = 0, gaps = 0;
    uint64_t trades_applied = 0;
    std::string last_why;
  };

  class MdFeedTest : public ::testing::Test
  {
  protected:
    std::unique_ptr<fix::SimLmaxGateway> gw;
    std::unique_ptr<actors::Manager> mgr;
    LmaxMdFeed *feed = nullptr;
    std::unique_ptr<CheckTap> tap;

    fix::SimLmaxGateway::Config gw_cfg()
    {
      fix::SimLmaxGateway::Config c;
      c.md_dictionary = LMAX_MD_DICT;
      c.trading_dictionary = TRADING_DICT;
      c.instruments = {{"4001", 1.15230, 0.00001, 0.1}, {"4004", 155.000, 0.001, 0.1}};
      c.md_interval_ms = 10;
      return c;
    }

    MdFeedConfig feed_cfg()
    {
      MdFeedConfig c;
      c.session.env = fix::Env::Loopback;
      c.session.host = "127.0.0.1";
      c.session.port = 0; // set in start()
      c.session.tls = false;
      c.session.sender = "CLIENTMD";
      c.session.target = "LMXBDM";
      c.session.dictionary = LMAX_MD_DICT;
      c.session.reconnect_s = 1;
      c.session.heartbeat_s = 5;
      c.session.start_time = "00:00:00";
      c.session.end_time = "00:00:00";
      c.username = "user";
      c.password = "pass";
      c.instruments = {{"4001", "EUR/USD", 7001, 0.00001, 0.1}, {"4004", "USD/JPY", 7004, 0.001, 0.1}};
      c.venue = 'L';
      c.gap_timeout_ms = 300;
      return c;
    }

    void start(fix::SimLmaxGateway::Config g, MdFeedConfig f)
    {
      gw = std::make_unique<fix::SimLmaxGateway>(g);
      gw->start();
      f.session.port = gw->port();
      tap = std::make_unique<CheckTap>(f.instruments);
      feed = new LmaxMdFeed(f, {nullptr, nullptr}, tap.get());
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(feed);
    }
    void start() { start(gw_cfg(), feed_cfg()); }

    void TearDown() override
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete feed;
      if (gw)
        gw->stop();
    }
  };
}

TEST_F(MdFeedTest, LogsOnSubscribesAndKeepsTheBook)
{
  start();
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(0) >= 100 && tap->snapshots(1) >= 100; }));
  EXPECT_EQ(gw->counts().md_logons.load(), 1);
  EXPECT_EQ(gw->counts().md_requests.load(), 2);
  EXPECT_EQ(gw->counts().validation_errors.load(), 0);
  auto v = gw->received("V");
  ASSERT_EQ(v.size(), 2u);
  // The gateway re-serialises what it parsed, so field order is not the wire's
  // (that is checked in LmaxMdParse.RequestAsBuiltValidatesAgainstTheDictionary).
  for (const char *f : {"|262=1|", "|263=1|", "|264=20|", "|265=0|", "|266=Y|", "|267=3|269=0|269=1|269=2|",
                        "|146=1|48=4001|22=8|"})
    EXPECT_NE(v[0].find(f), std::string::npos) << f << " in " << v[0];
  mgr->end(); // stop the feed before reading its state
  std::lock_guard<std::mutex> l(tap->mu);
  EXPECT_EQ(tap->mismatches, 0u) << tap->last_why;
  EXPECT_EQ(tap->crossed, 0u);
  EXPECT_GT(tap->trades_applied, 0u);
  EXPECT_EQ(feed->counters().seq_breaks, 0u);
  EXPECT_EQ(feed->counters().resend_requests, 0u);
  EXPECT_EQ(feed->counters().bad_messages, 0u);
  EXPECT_EQ(feed->sync(0).stats().resyncs, 1u);
  EXPECT_GT(feed->sync(0).stats().trades_repeated, 0u); // the simulator repeats each trade once
  EXPECT_EQ(feed->sync(0).stats().crossed, 0u);
}

TEST_F(MdFeedTest, WrongPasswordIsRejected)
{
  auto f = feed_cfg();
  f.password = "wrong";
  start(gw_cfg(), f);
  ASSERT_TRUE(wait_until([&] { return gw->counts().logon_rejects.load() >= 1; }));
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(feed->logged_on());
  EXPECT_EQ(tap->snapshots(0), 0u);
}

TEST_F(MdFeedTest, GapFilledByTheGatewayKeepsGoing)
{
  start();
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(0) >= 20; }));
  gw->inject_md_gap(3);
  ASSERT_TRUE(wait_until([&] { return tap->saw("gap: ResendRequest"); }));
  const size_t at = tap->snapshots(0);
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(0) >= at + 50; }));
  ASSERT_TRUE(wait_until([&] { return tap->saw("gap closed"); }));
  mgr->end();
  std::lock_guard<std::mutex> l(tap->mu);
  EXPECT_GE(feed->counters().resend_requests, 1u);
  EXPECT_EQ(feed->counters().forced_reconnects, 0u);
  EXPECT_EQ(feed->counters().seq_breaks, 0u);
  EXPECT_GE(tap->gaps, 2u); // one per book
  EXPECT_EQ(tap->mismatches, 0u) << tap->last_why;
}

TEST_F(MdFeedTest, GapNotFilledForcesAReconnectAndAResync)
{
  auto g = gw_cfg();
  g.ignore_resend = true;
  start(g, feed_cfg());
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(0) >= 20; }));
  gw->inject_md_gap(2);
  ASSERT_TRUE(wait_until([&] { return tap->saw("gap not filled"); })) << [&] {
    std::lock_guard<std::mutex> l(tap->mu);
    std::string all;
    for (auto &e : tap->events)
      all += e + "; ";
    return all + " resend requests at gateway " + std::to_string(gw->counts().resend_requests.load());
  }();
  ASSERT_TRUE(wait_until([&] { return gw->counts().md_logons.load() >= 2; }));
  const size_t at = tap->snapshots(0);
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(0) >= at + 30; }));
  mgr->end();
  std::lock_guard<std::mutex> l(tap->mu);
  EXPECT_GE(feed->counters().forced_reconnects, 1u);
  EXPECT_EQ(feed->counters().logons, 2u);
  EXPECT_EQ(feed->sync(0).stats().resyncs, 2u);
  EXPECT_EQ(tap->clears, 2u); // a book-clear before each instrument's second snapshot
  EXPECT_EQ(tap->mismatches, 0u) << tap->last_why;
  EXPECT_EQ(gw->counts().md_requests.load(), 4);
}

TEST_F(MdFeedTest, DisconnectReconnectsAndResyncs)
{
  start();
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(1) >= 20; }));
  gw->disconnect_md();
  ASSERT_TRUE(wait_until([&] { return gw->counts().md_logons.load() >= 2; }));
  const size_t at = tap->snapshots(1);
  ASSERT_TRUE(wait_until([&] { return tap->snapshots(1) >= at + 20; }));
  mgr->end();
  std::lock_guard<std::mutex> l(tap->mu);
  EXPECT_EQ(feed->sync(1).stats().resyncs, 2u);
  EXPECT_EQ(feed->counters().seq_breaks, 0u);
  EXPECT_EQ(tap->mismatches, 0u) << tap->last_why;
}

TEST_F(MdFeedTest, UnknownInstrumentIsRejectedByTheGateway)
{
  auto f = feed_cfg();
  f.instruments.push_back({"9999", "XXX/YYY", 7999, 0.0001, 0.1});
  start(gw_cfg(), f);
  ASSERT_TRUE(wait_until([&] { return tap->saw("MarketDataRequestReject"); }));
  mgr->end();
  EXPECT_EQ(feed->counters().md_rejects, 1u);
  EXPECT_GT(tap->snapshots(0), 0u);
}

TEST(MdFeedConfigTest, RefusesAProductionHostForDemo)
{
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "lmax_md_cfg_test";
  fs::create_directories(dir);
  {
    std::ofstream(dir / "key.json") << R"({"environment":"demo","username":"u","password":"p",
      "md_target_comp_id":"T"})";
    std::ofstream(dir / "inst.csv") << "4001,EUR/USD,7001,0.00001,0.1\n";
  }
  auto ini = [&](const std::string &env, const std::string &host) {
    std::ofstream(dir / "md.ini") << "environment = " << env << "\nhost = " << host
                                  << "\nport = 443\nkey_file = " << (dir / "key.json").string()
                                  << "\ninstruments = " << (dir / "inst.csv").string()
                                  << "\ndictionary = " << LMAX_MD_DICT << "\n";
    MdFeedConfig c;
    std::string err;
    bool ok = MdFeedConfig::from_file((dir / "md.ini").string(), c, err);
    return std::make_pair(ok, err);
  };
  EXPECT_TRUE(ini("demo", "fix-marketdata.london-demo.lmax.com").first);
  auto r = ini("demo", "fix-market-data.london-perpetual.lmax.com");
  EXPECT_FALSE(r.first);
  EXPECT_NE(r.second.find("london-demo"), std::string::npos) << r.second;
  EXPECT_FALSE(ini("demo", "fix.lmaxtrader.com").first);
  EXPECT_FALSE(ini("production", "fix-marketdata.london-demo.lmax.com").first);
  EXPECT_FALSE(ini("uat", "fix-marketdata.london-uat.lmax.com").first); // key file is for demo
  EXPECT_EQ(create_LmaxMdFeed((dir / "md.ini").string(), {}), nullptr);
  fs::remove_all(dir);
}
