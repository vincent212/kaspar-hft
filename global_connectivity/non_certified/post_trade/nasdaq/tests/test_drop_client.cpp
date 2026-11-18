/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// NasdaqDropClient against FakeDropSession, run synchronously: the fake calls
// the client's listener on the test thread, the listener sends messages to the
// client, and SyncClient delivers each one at once with fast_send. The
// subscriber records what the client forwards.
//
// Then the factory: a bad configuration gives nullptr, not a crash.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/property_tree/info_parser.hpp>

#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "nasdaq_post_trade/act/NasdaqDropClient.hpp"
#include "nasdaq_post_trade/if/NasdaqDropClient.hpp"

#include "FakeDropSession.hpp"
#include "Recorder.hpp"

using namespace nasdaq_post_trade;
using nasdaq_post_trade::test::FakeDropSession;
using nasdaq_post_trade::test::Recorder;
using Client = nasdaq_post_trade::act::NasdaqDropClient;
using post_trade::Event;
using post_trade::msg::TradeCapture;

namespace
{
  // Delivers every message on the sender's thread, so a test runs in order.
  class SyncClient : public Client
  {
  public:
    using Client::Client;
    void send(const actors::Message *m, actors::Actor *from) noexcept override
    {
      fast_send(*m, from);
      delete m;
    }
  };

  std::string line(char type, uint64_t ref, uint64_t match, int64_t shares = 100,
                   int64_t px_e4 = 1000000, char side = 'B', const std::string &sym = "INTC")
  {
    DropReport r;
    r.type = type;
    r.time_ns = 36000'000'000'000LL;
    r.source = "PORT01";
    r.order_token = "USR1,TKN" + std::to_string(ref);
    r.side = side;
    r.shares = shares;
    r.stock = sym;
    r.price_e4 = px_e4;
    r.firm = "FIRM";
    r.reference = ref;
    r.match_tif = match;
    r.capacity = 'A';
    r.liquidity = type == 'E' ? 'A' : ' ';
    return format_line(r, true);
  }

  struct Harness
  {
    Recorder sub{"subscriber"};
    FakeDropSession *fake = nullptr;
    std::unique_ptr<SyncClient> client;

    explicit Harness(ClientConfig cfg = {})
    {
      if (cfg.login.username.empty())
      {
        cfg.login = {"USER01", "PASSWORD01", "", 1};
        cfg.trade_date = 20261118;
        cfg.assets = {{"INTC", 101}};
      }
      auto f = std::make_unique<FakeDropSession>();
      fake = f.get();
      client = std::make_unique<SyncClient>(cfg, std::move(f), &sub);
    }

    void start()
    {
      actors::msg::Start s;
      client->fast_send(s, nullptr);
    }

    // A day: two orders accepted and executed, a third executed, one break.
    void day()
    {
      fake->line(line('A', 1, 0));
      fake->line(line('E', 1, 5001));
      fake->line(line('A', 2, 0, 200, 1000000, 'S'));
      fake->line(line('E', 2, 5002, 200, 1000000, 'S'));
      fake->line(line('E', 3, 5003, 300));
      fake->line(line('B', 3, 5003, 300));
    }
  };
}

TEST(NasdaqDropClient, StartsTheSessionOnStartAndLogsIn)
{
  Harness h;
  EXPECT_FALSE(h.fake->started) << "not in the constructor";
  h.start();
  ASSERT_TRUE(h.fake->started);
  ASSERT_EQ(h.fake->logins.size(), 1u);
  EXPECT_EQ(h.fake->logins[0].username, "USER01");
  EXPECT_EQ(h.fake->logins[0].session, "") << "blank: the active session";
  EXPECT_EQ(h.fake->logins[0].next_seq, 1u) << "replay the day from its start";
  EXPECT_EQ(h.client->state(), Client::State::Connecting);

  h.fake->accept("S0001", 1);
  EXPECT_EQ(h.client->state(), Client::State::LoggedIn);
  EXPECT_EQ(h.client->session(), "S0001");
  EXPECT_EQ(h.client->next_seq(), 1u);
  EXPECT_EQ(h.sub.count(), 0u);
}

TEST(NasdaqDropClient, ForwardsTradesNotOrderEvents)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.day();

  auto tc = h.sub.all<TradeCapture>();
  ASSERT_EQ(tc.size(), 4u) << "three executions and one break";
  EXPECT_EQ(tc[0]->event, Event::New);
  EXPECT_EQ(tc[0]->trade_id, "5001.1");
  EXPECT_EQ(tc[0]->report_id, "S0001:2");
  EXPECT_EQ(tc[0]->asset, 101);
  EXPECT_EQ(tc[0]->trade_date, 20261118u);
  EXPECT_EQ(tc[1]->side, post_trade::Side::Sell);
  EXPECT_EQ(tc[3]->event, Event::Bust);
  EXPECT_EQ(tc[3]->ref_trade_id, "5003.3");
  EXPECT_EQ(h.client->stats().order_events, 2u);
  EXPECT_EQ(h.client->stats().lines, 6u);
  EXPECT_EQ(h.client->ledger().live(), 2u);
  EXPECT_EQ(h.client->ledger().net_qty("INTC"), 100 - 200);
}

TEST(NasdaqDropClient, CorrectionIsForwardedAsAmend)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.fake->line(line('E', 7, 6001, 100, 1000000));
  h.fake->line(line('C', 7, 6001, 100, 1000100));
  auto tc = h.sub.all<TradeCapture>();
  ASSERT_EQ(tc.size(), 2u);
  EXPECT_EQ(tc[1]->event, Event::Amend);
  EXPECT_EQ(tc[1]->ref_trade_id, "6001.7");
  EXPECT_EQ(h.client->ledger().find("6001.7")->price_mantissa, 1000100);
}

TEST(NasdaqDropClient, ReplayFromStartOfDayAfterRestartIsNotForwardedTwice)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.day();
  ASSERT_EQ(h.sub.count(), 4u);

  // The session is restarted and asks for the day from line 1 again (as
  // replay_from_start does at every logon): the same six lines come back.
  h.fake->lose_connection();
  h.fake->accept("S0001", 1);
  h.day();
  EXPECT_EQ(h.sub.count(), 4u) << "no duplicate downstream";
  EXPECT_EQ(h.client->stats().duplicates, 4u);
  EXPECT_EQ(h.client->ledger().net_qty("INTC"), -100);
}

TEST(NasdaqDropClient, ReconnectResumesAtTheNextSequenceNumber)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.fake->line(line('E', 1, 5001));
  h.fake->line(line('E', 2, 5002));
  ASSERT_EQ(h.sub.count(), 2u);

  h.fake->lose_connection();
  EXPECT_EQ(h.client->state(), Client::State::Down);
  ASSERT_EQ(h.fake->logins.size(), 2u);
  EXPECT_EQ(h.fake->logins[1].session, "S0001") << "the same session";
  EXPECT_EQ(h.fake->logins[1].next_seq, 3u) << "the line after the last delivered";

  // The server accepts at an earlier number (2) and resends line 2.
  h.fake->accept("S0001", 2);
  h.fake->line(line('E', 2, 5002));
  h.fake->line(line('E', 3, 5003));
  auto tc = h.sub.all<TradeCapture>();
  ASSERT_EQ(tc.size(), 3u) << "line 2 once, then line 3";
  EXPECT_EQ(tc[2]->trade_id, "5003.3");
  EXPECT_FALSE(tc[2]->poss_dup);
  EXPECT_EQ(h.client->stats().duplicates, 1u);
  EXPECT_EQ(h.client->stats().disconnects, 1u);
  EXPECT_EQ(h.client->stats().logins, 2u);
  EXPECT_EQ(h.client->stats().gaps, 0u);
}

TEST(NasdaqDropClient, ResentLineIsMarkedPossDup)
{
  // Line 2 first arrives garbled (not parsed); after a reconnect the server
  // resends it intact. It is new to the ledger, so it is forwarded, marked as
  // possibly delivered before.
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.fake->line(line('E', 1, 5001));
  h.fake->line("garbled");
  h.fake->lose_connection();
  h.fake->accept("S0001", 2);
  h.fake->line(line('E', 2, 5002));
  auto tc = h.sub.all<TradeCapture>();
  ASSERT_EQ(tc.size(), 2u);
  EXPECT_FALSE(tc[0]->poss_dup);
  EXPECT_TRUE(tc[1]->poss_dup);
  EXPECT_EQ(tc[1]->report_id, "S0001:2");
}

TEST(NasdaqDropClient, NoAcknowledgementIsSent)
{
  // DROP and SoupBinTCP have no application acknowledgement: the client's only
  // requests are logins; the sequence number of a relogin is its implicit ack.
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.day();
  EXPECT_EQ(h.fake->logins.size(), 1u);
  h.fake->lose_connection();
  ASSERT_EQ(h.fake->logins.size(), 2u);
  EXPECT_EQ(h.fake->logins[1].next_seq, 7u) << "acknowledges lines 1-6";
}

TEST(NasdaqDropClient, SequenceGapIsCounted)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.fake->line(line('E', 1, 5001));
  h.fake->next_seq = 5; // lines 2-4 never arrive (not possible from a conforming server)
  h.fake->line(line('E', 5, 5005));
  EXPECT_EQ(h.client->stats().gaps, 1u);
  EXPECT_EQ(h.sub.count(), 2u);
}

TEST(NasdaqDropClient, BadLineIsCountedAndSkipped)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.fake->line("not a drop line");
  h.fake->line(line('E', 1, 5001));
  EXPECT_EQ(h.client->stats().parse_errors, 1u);
  EXPECT_EQ(h.sub.count(), 1u);
}

TEST(NasdaqDropClient, LoginRejectedStops)
{
  Harness h;
  h.start();
  h.fake->reject('A');
  EXPECT_EQ(h.client->state(), Client::State::Rejected);
  EXPECT_EQ(h.sub.count(), 0u);
}

TEST(NasdaqDropClient, EndOfSession)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.day();
  h.fake->end_of_session();
  EXPECT_EQ(h.client->state(), Client::State::Ended);
  EXPECT_EQ(h.client->ledger().live(), 2u) << "the day's trades are kept";
}

TEST(NasdaqDropClient, NewSessionIsANewTradingDay)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  h.day();
  h.fake->lose_connection();
  h.fake->accept("S0002", 1); // the venue's next session
  EXPECT_EQ(h.client->ledger().live(), 0u);
  h.fake->line(line('E', 1, 5001)); // the same match number, a new day
  EXPECT_EQ(h.sub.count(), 5u) << "forwarded: it is a new trade";
  EXPECT_EQ(h.sub.all<TradeCapture>().back()->report_id, "S0002:1");
}

TEST(NasdaqDropClient, ShutdownStopsTheSession)
{
  Harness h;
  h.start();
  h.fake->accept("S0001", 1);
  actors::msg::Shutdown s;
  h.client->fast_send(s, nullptr);
  EXPECT_FALSE(h.fake->started);
  EXPECT_EQ(h.client->state(), Client::State::Idle);
}

// ------------------------------------------------------------------ factory

namespace
{
  namespace fs = std::filesystem;

  struct TmpDir
  {
    fs::path p;
    TmpDir()
    {
      p = fs::temp_directory_path() / ("nasdaq_drop_test_" + std::to_string(::getpid()));
      fs::create_directories(p);
    }
    ~TmpDir() { fs::remove_all(p); }
    std::string write(const std::string &name, const std::string &text) const
    {
      std::ofstream(p / name) << text;
      return (p / name).string();
    }
  };

  boost::property_tree::ptree info(const std::string &text)
  {
    std::istringstream s(text);
    boost::property_tree::ptree pt;
    boost::property_tree::read_info(s, pt);
    return pt;
  }
}

TEST(NasdaqDropFactory, BadConfigGivesNullptr)
{
  Recorder sub("s");
  TmpDir d;
  d.write("cred.info", "username USER01\npassword PASSWORD01\n");
  d.write("long.info", "username TOOLONGUSER\npassword x\n");
  const std::string ok = "host drop.example\nport 9000\ncredentials cred.info\n";

  EXPECT_EQ(create_NasdaqDropClient(info(ok), nullptr, d.p.string()), nullptr) << "no subscriber";
  EXPECT_EQ(create_NasdaqDropClient(info("port 9000\ncredentials cred.info\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info("host h\ncredentials cred.info\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info("host h\nport 70000\ncredentials cred.info\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info("host h\nport abc\ncredentials cred.info\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info("host h\nport 9000\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info("host h\nport 9000\ncredentials missing.info\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info("host h\nport 9000\ncredentials long.info\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info(ok + "replay_from_start maybe\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info(ok + "trade_date 20261340\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info(ok + "heartbeat_ms 5000\nserver_timeout_ms 1000\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(info(ok + "assets { INTC x }\n"), &sub, d.p.string()), nullptr);
  EXPECT_EQ(create_NasdaqDropClient(d.p.string() + "/no_such.info", &sub), nullptr);
  const std::string nosec = d.write("nosec.info", "other { a 1 }\n");
  EXPECT_EQ(create_NasdaqDropClient(nosec, &sub), nullptr);
}

TEST(NasdaqDropFactory, GoodConfig)
{
  Recorder sub("s");
  TmpDir d;
  d.write("cred.info", "username USER01\npassword PASSWORD01\n");
  const std::string path = d.write(
      "drop.info",
      "nasdaq_drop\n{\n host drop.example\n port 9000\n credentials cred.info\n"
      " replay_from_start false\n trade_date 20261118\n assets { INTC 101 MSFT 102 }\n}\n");

  std::string err;
  boost::property_tree::ptree pt;
  boost::property_tree::read_info(path, pt);
  auto c = parse_config(pt.get_child("nasdaq_drop"), d.p.string(), &err);
  ASSERT_TRUE(c) << err;
  EXPECT_EQ(c->soup.host, "drop.example");
  EXPECT_EQ(c->soup.port, 9000);
  EXPECT_EQ(c->login.username, "USER01");
  EXPECT_EQ(c->login.password, "PASSWORD01");
  EXPECT_EQ(c->login.next_seq, 0u);
  EXPECT_EQ(c->trade_date, 20261118u);
  EXPECT_EQ(c->assets.at("MSFT"), 102);
  EXPECT_FALSE(c->sim);

  // Built, not started: no connection is attempted before Start.
  actors::Actor *a = create_NasdaqDropClient(path, &sub);
  ASSERT_NE(a, nullptr);
  delete a;
}
