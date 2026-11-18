/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The real SoupSession against SimDropServer, over TCP on 127.0.0.1 inside
// this process (nothing else is contacted): login, replay, heartbeats both
// ways, a dropped connection, a rejected login, end of session, logout. Then
// the whole client, built by the factory in simulator mode and run by an
// actors::Manager.

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/property_tree/info_parser.hpp>

#include "actors/act/Manager.hpp"
#include "nasdaq_post_trade/SimDropServer.hpp"
#include "nasdaq_post_trade/SoupSession.hpp"
#include "nasdaq_post_trade/act/NasdaqDropClient.hpp"
#include "nasdaq_post_trade/if/NasdaqDropClient.hpp"

#include "Recorder.hpp"

using namespace nasdaq_post_trade;
using namespace std::chrono_literals;
using nasdaq_post_trade::test::Recorder;
using nasdaq_post_trade::test::wait_until;

namespace
{
  struct Events : DropSessionListener
  {
    mutable std::mutex mu;
    std::vector<std::pair<uint64_t, std::string>> lines;
    std::vector<std::pair<std::string, uint64_t>> accepted;
    std::vector<char> rejected;
    int ended = 0, disconnects = 0;

    void on_login_accepted(const std::string &s, uint64_t n) override
    {
      std::lock_guard<std::mutex> l(mu);
      accepted.emplace_back(s, n);
    }
    void on_login_rejected(char r) override
    {
      std::lock_guard<std::mutex> l(mu);
      rejected.push_back(r);
    }
    void on_line(const std::string &, uint64_t seq, std::string_view p) override
    {
      std::lock_guard<std::mutex> l(mu);
      lines.emplace_back(seq, std::string(p));
    }
    void on_end_of_session(const std::string &) override
    {
      std::lock_guard<std::mutex> l(mu);
      ++ended;
    }
    void on_disconnected(const std::string &, bool) override
    {
      std::lock_guard<std::mutex> l(mu);
      ++disconnects;
    }
    size_t n_lines() const
    {
      std::lock_guard<std::mutex> l(mu);
      return lines.size();
    }
    template <class F>
    auto with(F f) const
    {
      std::lock_guard<std::mutex> l(mu);
      return f(*this);
    }
  };

  std::string make_line(uint64_t ref)
  {
    DropReport r;
    r.type = 'E';
    r.time_ns = 36000'000'000'000LL;
    r.source = "SIM01";
    r.side = 'B';
    r.shares = 100;
    r.stock = "INTC";
    r.price_e4 = 1000000;
    r.firm = "SIMF";
    r.reference = ref;
    r.match_tif = 9000 + ref;
    return format_line(r);
  }

  SoupSessionConfig soup_cfg(int port)
  {
    SoupSessionConfig c;
    c.host = "127.0.0.1";
    c.port = port;
    c.heartbeat_ms = 50;
    c.server_timeout_ms = 2000;
    c.reconnect_ms = 50;
    c.connect_timeout_ms = 1000;
    return c;
  }

  LoginParams login(uint64_t seq = 1, std::string pw = "SIMPASS")
  {
    return {"SIMUSR", std::move(pw), "", seq};
  }

  SimDropConfig sim_cfg()
  {
    SimDropConfig c;
    c.heartbeat_ms = 50;
    return c;
  }
}

TEST(SoupSession, LogsInAndReplaysTheSession)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  for (uint64_t i = 1; i <= 3; ++i)
    sim.publish(make_line(i));

  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  s.start(login(1), &ev);
  ASSERT_TRUE(wait_until([&] { return ev.n_lines() == 3; }));
  ev.with([&](const Events &e) {
    ASSERT_EQ(e.accepted.size(), 1u);
    EXPECT_EQ(e.accepted[0].first, "SIM0000001");
    EXPECT_EQ(e.accepted[0].second, 1u);
    for (uint64_t i = 0; i < 3; ++i)
    {
      EXPECT_EQ(e.lines[i].first, i + 1);
      EXPECT_EQ(e.lines[i].second, make_line(i + 1));
    }
  });
  auto lg = sim.logins();
  ASSERT_EQ(lg.size(), 1u);
  EXPECT_EQ(lg[0].username, "SIMUSR");
  EXPECT_EQ(lg[0].requested_seq, 1u);

  // Live lines follow the replay.
  sim.publish(make_line(4));
  ASSERT_TRUE(wait_until([&] { return ev.n_lines() == 4; }));
  s.stop();
}

TEST(SoupSession, HeartbeatsBothWays)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  Events ev;
  SoupSessionConfig c = soup_cfg(sim.port());
  c.server_timeout_ms = 400; // the server's 50 ms heartbeats keep the link up
  SoupSession s(c);
  s.start(login(), &ev);
  ASSERT_TRUE(wait_until([&] { return sim.client_heartbeats() >= 5; }));
  std::this_thread::sleep_for(600ms);
  EXPECT_EQ(ev.with([](const Events &e) { return e.disconnects; }), 0)
      << "server heartbeats count as traffic";
  EXPECT_EQ(sim.connections(), 1u);
  s.stop();
}

TEST(SoupSession, ReconnectResumesWithoutDuplicates)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  for (uint64_t i = 1; i <= 3; ++i)
    sim.publish(make_line(i));
  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  s.start(login(1), &ev);
  ASSERT_TRUE(wait_until([&] { return ev.n_lines() == 3; }));

  sim.drop_connection();
  ASSERT_TRUE(wait_until([&] { return ev.with([](const Events &e) { return e.disconnects; }) == 1; }));
  sim.publish(make_line(4));
  sim.publish(make_line(5));
  ASSERT_TRUE(wait_until([&] { return ev.n_lines() == 5; }));
  std::this_thread::sleep_for(100ms);

  auto lg = sim.logins();
  ASSERT_EQ(lg.size(), 2u);
  EXPECT_EQ(lg[1].session, "SIM0000001") << "the session it was in";
  EXPECT_EQ(lg[1].requested_seq, 4u) << "the next line not delivered";
  ev.with([](const Events &e) {
    EXPECT_EQ(e.lines.size(), 5u);
    std::set<uint64_t> seqs;
    for (auto &[q, l] : e.lines)
      seqs.insert(q);
    EXPECT_EQ(seqs.size(), 5u) << "no line delivered twice";
    EXPECT_EQ(*seqs.rbegin(), 5u);
  });
  s.stop();
}

TEST(SoupSession, RejectedLoginDoesNotRetry)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  s.start(login(1, "WRONG"), &ev);
  ASSERT_TRUE(wait_until([&] { return ev.with([](const Events &e) { return e.rejected.size(); }) == 1; }));
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(ev.with([](const Events &e) { return e.rejected[0]; }), 'A');
  EXPECT_EQ(sim.connections(), 1u) << "no second attempt with bad credentials";
  s.stop();
}

TEST(SoupSession, UnknownSessionIsRejected)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  LoginParams p = login();
  p.session = "OTHER";
  s.start(p, &ev);
  ASSERT_TRUE(wait_until([&] { return ev.with([](const Events &e) { return e.rejected.size(); }) == 1; }));
  EXPECT_EQ(ev.with([](const Events &e) { return e.rejected[0]; }), 'S');
  s.stop();
}

TEST(SoupSession, EndOfSessionStops)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  sim.publish(make_line(1));
  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  s.start(login(), &ev);
  ASSERT_TRUE(wait_until([&] { return ev.n_lines() == 1; }));
  sim.end_session();
  ASSERT_TRUE(wait_until([&] { return ev.with([](const Events &e) { return e.ended; }) == 1; }));
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(sim.connections(), 1u) << "no reconnect after End of Session";
  s.stop();
}

TEST(SoupSession, StopLogsOut)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  s.start(login(), &ev);
  ASSERT_TRUE(wait_until([&] { return sim.client_logged_in(); }));
  s.stop();
  ASSERT_TRUE(wait_until([&] { return sim.logouts() == 1; }));
}

TEST(SoupSession, SequenceZeroStartsAtTheMostRecentLine)
{
  SimDropServer sim(sim_cfg());
  ASSERT_TRUE(sim.start());
  for (uint64_t i = 1; i <= 4; ++i)
    sim.publish(make_line(i));
  Events ev;
  SoupSession s(soup_cfg(sim.port()));
  s.start(login(0), &ev);
  ASSERT_TRUE(wait_until([&] { return ev.n_lines() == 1; }));
  std::this_thread::sleep_for(100ms);
  ev.with([](const Events &e) {
    EXPECT_EQ(e.accepted[0].second, 4u);
    EXPECT_EQ(e.lines.size(), 1u);
    EXPECT_EQ(e.lines[0].first, 4u);
  });
  s.stop();
}

TEST(SoupSession, ConnectionRefusedIsRetried)
{
  // A port with nothing listening (the simulator's, after it is gone).
  int port;
  {
    SimDropServer sim(sim_cfg());
    ASSERT_TRUE(sim.start());
    port = sim.port();
  }
  Events ev;
  SoupSession s(soup_cfg(port));
  s.start(login(), &ev);
  ASSERT_TRUE(wait_until([&] { return ev.with([](const Events &e) { return e.disconnects; }) >= 2; }));
  s.stop();
}

// ------------------------------------------------- the client, end to end

TEST(NasdaqDropClientSim, GeneratedTradingReachesTheSubscriberOnce)
{
  Recorder sub("subscriber");
  std::istringstream cfg(
      "heartbeat_ms 50\nserver_timeout_ms 2000\nreconnect_ms 50\n"
      "trade_date 20261118\nassets { INTC 101 MSFT 102 }\n"
      "sim { enabled true trade_ms 5 symbols \"INTC MSFT\" }\n");
  boost::property_tree::ptree pt;
  boost::property_tree::read_info(cfg, pt);
  auto *client = dynamic_cast<act::NasdaqDropClient *>(create_NasdaqDropClient(pt, &sub));
  ASSERT_NE(client, nullptr);
  SimDropServer *sim = client->simulator();
  ASSERT_NE(sim, nullptr);

  {
    actors::Manager mgr;
    mgr.add_to_manage_q(client);
    mgr.init();
    ASSERT_TRUE(wait_until([&] { return sub.count() >= 20; }));
    sim->drop_connection();
    ASSERT_TRUE(wait_until([&] { return sim->logins().size() >= 2; }));
    const size_t at_drop = sim->journal_size();
    ASSERT_TRUE(wait_until([&] { return sub.count() >= 40 && sim->journal_size() > at_drop + 10; }));
    mgr.end();
  }

  auto tc = sub.all<post_trade::msg::TradeCapture>();
  std::set<std::string> reports;
  std::map<post_trade::Event, int> by_event;
  for (auto *t : tc)
  {
    EXPECT_TRUE(reports.insert(t->report_id).second) << "forwarded twice: " << t->report_id;
    ++by_event[t->event];
    EXPECT_NE(t->asset, -1);
  }
  EXPECT_GT(by_event[post_trade::Event::New], 0);
  EXPECT_GT(by_event[post_trade::Event::Bust], 0);
  EXPECT_GT(by_event[post_trade::Event::Amend], 0);
  EXPECT_EQ(client->stats().gaps, 0u);
  EXPECT_EQ(client->stats().parse_errors, 0u);
  EXPECT_GE(client->stats().disconnects, 1u);
  delete client;
}
