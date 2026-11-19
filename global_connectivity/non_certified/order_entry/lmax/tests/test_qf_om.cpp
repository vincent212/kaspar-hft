/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LmaxOM over the real QuickFIX session against SimLmaxGateway on 127.0.0.1:
// the requests validated by the gateway against LMAX's dictionary, the
// scenario of the live check (an order far from the market, amend, cancel,
// one that fills), cancel on disconnect and recovery after a reconnect.

#include <gtest/gtest.h>

#include <filesystem>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "lmax_fix/SimLmaxGateway.hpp"
#include "lmax_om/act/LmaxOM.hpp"

namespace som = frame::som::msg;
using Order = som::Order;
using namespace lmax;
using namespace lmax::om;
using namespace lmax::om::test;

namespace
{
  class QfOMTest : public ::testing::Test
  {
  protected:
    std::unique_ptr<fix::SimLmaxGateway> gw;
    Recorder somr{"SOM"};
    std::unique_ptr<actors::Manager> mgr;
    LmaxOM *om = nullptr;
    const frame::ref::Asset *eurusd = nullptr;
    std::string store;

    void SetUp() override
    {
      eurusd = fx_asset("EURUSD_Q", 0.00001);
      store = (std::filesystem::temp_directory_path() / ("lmax_qf_om_" + std::to_string(::getpid()))).string();
      std::filesystem::remove_all(store);
    }
    void TearDown() override
    {
      stop();
      if (gw)
        gw->stop();
      std::filesystem::remove_all(store);
    }

    void start_gateway(bool partial = false)
    {
      fix::SimLmaxGateway::Config g;
      g.md_dictionary = LMAX_MD_DICT;
      g.trading_dictionary = LMAX_TRADING_DICT;
      g.instruments = {{"4001", 1.15230, 0.00001, 0.1}};
      g.partial_fills = partial;
      gw = std::make_unique<fix::SimLmaxGateway>(g);
      gw->start();
      gw->set_market("4001", 1.15220, 1.15230);
    }

    LmaxOMConfig cfg(const std::string &password = "pass")
    {
      LmaxOMConfig c;
      auto &p = c.session.params;
      p.env = fix::Env::Loopback;
      p.host = "127.0.0.1";
      p.port = gw->port();
      p.tls = false;
      p.sender = "CLIENTTR";
      p.target = "LMXBD";
      p.dictionary = LMAX_TRADING_DICT;
      p.store_path = store;
      p.reset_on_logon = false;
      p.reconnect_s = 1;
      p.heartbeat_s = 5;
      p.start_time = p.end_time = "00:00:00";
      c.session.username = "user";
      c.session.password = password;
      c.instruments[eurusd->id] = OmInstrument{"4001", "EUR/USD", 0.00001, 0.1};
      return c;
    }

    void start(LmaxOMConfig c, bool wait_ready = true)
    {
      om = new LmaxOM(c, &somr, qf_session_factory(c.session), en::x::UNI);
      mgr = std::make_unique<actors::Manager>("test");
      mgr->manage_and_start(om);
      if (wait_ready)
        ASSERT_TRUE(wait_until([&] { return om->ready(); }, std::chrono::seconds(10)));
    }
    void stop()
    {
      if (mgr)
        mgr->end();
      mgr.reset();
      delete om;
      om = nullptr;
    }
    Order order(int oid, int qty, int px, en::bs side = en::bs::BUY)
    {
      Order o("FX", en::x::UNI, eurusd->id, qty, px, side, 4);
      o.oid = oid;
      return o;
    }
    std::vector<std::string> wait_replies(size_t n)
    {
      wait_until([&] { return somr.replies().size() >= n; }, std::chrono::seconds(10));
      std::this_thread::sleep_for(100ms);
      return somr.replies();
    }
  };
}

TEST_F(QfOMTest, FarOrderAmendCancelAndAFill)
{
  start_gateway();
  start(cfg());
  // 1. far from the market: rests.
  om->send(new Order(order(1, 1, 110000)), &somr);
  ASSERT_EQ(wait_replies(1), (std::vector<std::string>{"Ack 1"}));
  // 2. amend: size 2 units, price 1.10010.
  auto r = order(2, 2, 110010);
  r.oid_to_cancel = 1;
  om->send(new Order(r), &somr);
  ASSERT_EQ(wait_replies(3), (std::vector<std::string>{"Ack 1", "CancAck 1", "Ack 2"}));
  // 3. cancel.
  om->send(new som::Cancel(2), &somr);
  ASSERT_EQ(wait_replies(4), (std::vector<std::string>{"Ack 1", "CancAck 1", "Ack 2", "CancAck 2"}));
  // 4. small and marketable: fills at the offer.
  om->send(new Order(order(3, 1, 115240)), &somr);
  auto all = wait_replies(6);
  ASSERT_EQ(all, (std::vector<std::string>{"Ack 1", "CancAck 1", "Ack 2", "CancAck 2", "Ack 3", "Fill 3 1"}));
  auto f = somr.all<som::Fill>();
  EXPECT_EQ(f[0]->pxi, 115230);
  // What LMAX (the gateway) received, validated against the dictionary.
  EXPECT_EQ(gw->counts().new_orders.load(), 2);
  EXPECT_EQ(gw->counts().replaces.load(), 1);
  EXPECT_EQ(gw->counts().cancels.load(), 1);
  auto d = gw->received("D");
  ASSERT_EQ(d.size(), 2u);
  EXPECT_NE(d[0].find("|38=0.1|"), std::string::npos) << d[0];
  EXPECT_NE(d[0].find("|44=1.10000|"), std::string::npos) << d[0];
  auto g = gw->received("G");
  ASSERT_EQ(g.size(), 1u);
  EXPECT_NE(g[0].find("|38=0.2|"), std::string::npos) << g[0];
  EXPECT_NE(g[0].find("|44=1.10010|"), std::string::npos) << g[0];
  EXPECT_EQ(om->counters().unknown, 0u);
  EXPECT_EQ(om->counters().unparsed, 0u);
  EXPECT_EQ(om->counters().session_rejects, 0u);
}

TEST_F(QfOMTest, PartialFills)
{
  start_gateway(true);
  start(cfg());
  om->send(new Order(order(1, 10, 115230)), &somr);
  EXPECT_EQ(wait_replies(3), (std::vector<std::string>{"Ack 1", "Fill 1 5", "Fill 1 5"}));
}

TEST_F(QfOMTest, CancelOnDisconnectIsReportedAfterTheReconnect)
{
  start_gateway();
  start(cfg());
  om->send(new Order(order(1, 1, 110000)), &somr);
  ASSERT_EQ(wait_replies(1), (std::vector<std::string>{"Ack 1"}));
  gw->disconnect_trading();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }, std::chrono::seconds(5)));
  ASSERT_TRUE(wait_until([&] { return om->ready(); }, std::chrono::seconds(10)));
  // The cancel arrives once: resent by the gateway and confirmed by the
  // OrderStatusRequest sent at logon.
  EXPECT_EQ(wait_replies(2), (std::vector<std::string>{"Ack 1", "CancAck 1"}));
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(somr.replies().size(), 2u);
  EXPECT_GE(gw->counts().status_requests.load(), 1);
}

TEST_F(QfOMTest, ReinstatedOrderSurvivesADisconnect)
{
  start_gateway();
  auto c = cfg();
  c.reinstate_on_disconnect = true;
  start(c);
  om->send(new Order(order(1, 1, 110000)), &somr);
  ASSERT_EQ(wait_replies(1), (std::vector<std::string>{"Ack 1"}));
  gw->disconnect_trading();
  ASSERT_TRUE(wait_until([&] { return !om->ready(); }, std::chrono::seconds(5)));
  ASSERT_TRUE(wait_until([&] { return om->ready(); }, std::chrono::seconds(10)));
  ASSERT_TRUE(wait_until([&] { return gw->counts().status_requests.load() >= 1; }));
  std::this_thread::sleep_for(300ms);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack 1"}));
  EXPECT_EQ(gw->working_orders(), 1u);
  // It still fills.
  auto cl = om->clordid(1);
  ASSERT_TRUE(gw->force_fill(cl, 0.1));
  EXPECT_EQ(wait_replies(2), (std::vector<std::string>{"Ack 1", "Fill 1 1"}));
}

TEST_F(QfOMTest, WrongPasswordNeverLogsOn)
{
  start_gateway();
  start(cfg("wrong"), false);
  ASSERT_TRUE(wait_until([&] { return gw->counts().logon_rejects.load() >= 1; }, std::chrono::seconds(10)));
  EXPECT_FALSE(om->ready());
  om->send(new Order(order(1, 1, 110000)), &somr);
  auto r = somr.wait_for<som::Reject>();
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->reason, som::Reject::NOTLOGGEDIN);
}

TEST_F(QfOMTest, PasswordIsNotWrittenToTheStoreOrTheLog)
{
  start_gateway();
  auto c = cfg("s3cret-pass");
  c.session.params.log_path = store + "/log";
  // The gateway accepts only "pass"; logon is refused, but the Logon was written.
  start(c, false);
  ASSERT_TRUE(wait_until([&] { return gw->counts().logon_rejects.load() >= 1; }, std::chrono::seconds(10)));
  stop();
  size_t files = 0;
  for (auto &e : std::filesystem::recursive_directory_iterator(store))
  {
    if (!e.is_regular_file())
      continue;
    ++files;
    std::ifstream in(e.path(), std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(body.find("s3cret-pass"), std::string::npos) << e.path();
  }
  EXPECT_GT(files, 0u);
}
