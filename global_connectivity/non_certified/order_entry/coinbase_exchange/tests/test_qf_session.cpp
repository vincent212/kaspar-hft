/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * QfSession (the QuickFIX session) against a QuickFIX acceptor on 127.0.0.1
 * that plays Coinbase: it loads Coinbase's FIXT11 and FIX50 dictionaries
 * (fix/, from cb_exch_fix_dictionaries-20260612.zip) with validation on,
 * checks the Logon's signature with the test secret, and answers
 * NewOrderSingle and OrderCancelRequest with ExecutionReports. The OM runs
 * as in production, with the real session factory. Plain TCP: TLS to the
 * venue is not exercised here.
 *
 * Built only with QUICKFIX_PATH.
 */

#ifdef CBX_WITH_QUICKFIX

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

#include "chutil/Assert.hpp"
#include "actors/act/Manager.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Order.hpp"

#include "quickfix/Application.h"
#include "quickfix/DataDictionary.h"
#include "quickfix/MessageStore.h"
#include "quickfix/NullStore.h"
#include "quickfix/Session.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/SocketAcceptor.h"

#include "TestUtil.hpp"
#include "coinbase_exchange_oe/act/CbxOM.hpp"

namespace som = frame::som::msg;
using namespace coinbase_exchange;
using namespace coinbase_exchange::oe;
using namespace coinbase_exchange::oe::test;

namespace
{
  const char *SECRET = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";

  std::string here(const std::string &rel)
  {
    const char *k = std::getenv("KSPRPROJ");
    return std::string(k ? k : "") + "/global_connectivity/non_certified/order_entry/coinbase_exchange/" + rel;
  }

  // The venue side.
  struct Venue : public FIX::Application
  {
    std::atomic<int> logons{0}, bad_signatures{0}, orders{0}, cancels{0};
    std::atomic<char> cod{0};
    std::string last_order;
    std::mutex mu;
    FIX::SessionID sid;

    void onCreate(const FIX::SessionID &) override {}
    void onLogon(const FIX::SessionID &s) override
    {
      sid = s;
      ++logons;
    }
    void onLogout(const FIX::SessionID &) override {}
    void toAdmin(FIX::Message &, const FIX::SessionID &) override {}
    void fromAdmin(const FIX::Message &m, const FIX::SessionID &) override
    {
      const auto &h = m.getHeader();
      if (h.getField(35) != "A")
        return;
      auto want = sign(SECRET, fix_logon_prehash(h.getField(52), "A", h.getField(34), h.getField(49), h.getField(56),
                                                 m.getField(554)));
      if (!want || m.getField(96) != *want || m.getField(95) != std::to_string(want->size()) ||
          m.getField(553) != h.getField(49) || m.getField(1137) != "9")
      {
        ++bad_signatures;
        throw FIX::RejectLogon("invalid signature");
      }
      if (m.isSetField(8013))
        cod = m.getField(8013)[0];
    }
    void toApp(FIX::Message &, const FIX::SessionID &) override {}
    void fromApp(const FIX::Message &m, const FIX::SessionID &s) override
    {
      const auto t = m.getHeader().getField(35);
      FIX::Message r;
      r.getHeader().setField(35, "8");
      if (t == "D")
      {
        ++orders;
        {
          std::lock_guard<std::mutex> l(mu);
          last_order = m.toString();
        }
        r.setField(11, m.getField(11));
        r.setField(37, "7b52009b-64fd-4a2a-89e6-d8a939753077");
        r.setField(17, "11111111-1111-4111-8111-111111111111");
        r.setField(39, "0");
        r.setField(150, "0");
        r.setField(55, m.getField(55));
        r.setField(54, m.getField(54));
        r.setField(60, "20261118-17:25:47.637");
        FIX::Session::sendToTarget(r, s);
        FIX::Message f;
        f.getHeader().setField(35, "8");
        f.setField(11, m.getField(11));
        f.setField(37, "7b52009b-64fd-4a2a-89e6-d8a939753077");
        f.setField(17, "22222222-2222-4222-8222-222222222222");
        f.setField(39, "1");
        f.setField(150, "F");
        f.setField(55, m.getField(55));
        f.setField(54, m.getField(54));
        f.setField(32, "0.0001");
        f.setField(31, m.getField(44));
        f.setField(14, "0.0001");
        f.setField(151, "0.0024");
        f.setField(60, "20261118-17:25:47.640");
        f.setField(1003, "1135493769");
        f.setField(1057, "N");
        FIX::Session::sendToTarget(f, s);
      }
      else if (t == "F")
      {
        ++cancels;
        r.setField(11, m.getField(11));
        r.setField(41, m.getField(41));
        r.setField(37, m.getField(37));
        r.setField(17, "33333333-3333-4333-8333-333333333333");
        r.setField(39, "4");
        r.setField(150, "4");
        r.setField(55, m.getField(55));
        r.setField(60, "20261118-17:25:48.000");
        FIX::Session::sendToTarget(r, s);
      }
    }
  };

  class QfSessionTest : public ::testing::Test
  {
  protected:
    Venue venue;
    std::unique_ptr<FIX::SocketAcceptor> acceptor;
    std::unique_ptr<FIX::MemoryStoreFactory> store;
    FIX::SessionSettings settings;
    std::string keyfile, dir;
    int port = 0;

    void SetUp() override
    {
      const char *t = std::getenv("TMPDIR");
      dir = std::string(t ? t : "/tmp") + "/cbx_qf_test";
      std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str());
      keyfile = dir + "/keys.json";
      {
        std::ofstream f(keyfile);
        f << R"({"environment":"sandbox","key":"testkey","secret":")" << SECRET << R"(","passphrase":"testpass"})";
      }
      chmod(keyfile.c_str(), 0600);
      port = 47000 + int(::getpid() % 1000);
      // Coinbase's FIXT11 dictionary does not define RawDataLength (95),
      // which its Logon page requires; QuickFIX playing the venue with the
      // dictionary as published refuses our Logon ("Invalid tag number").
      // The venue here uses a copy with 95 added (feed.md section 10).
      {
        std::ifstream in(here("fix/FIXT11-prod-sand.xml"));
        std::string x((std::istreambuf_iterator<char>(in)), {});
        auto at = x.find("<field name='RawData' required='N'/>");
        x.insert(at, "<field name='RawDataLength' required='N'/>\n            ");
        at = x.find("<field name='RawData' number='96'");
        x.insert(at, "<field name='RawDataLength' number='95' type='LENGTH'/>\n        ");
        std::ofstream out(dir + "/FIXT11-venue.xml");
        out << x;
      }
      FIX::Dictionary d;
      d.setString("ConnectionType", "acceptor");
      d.setString("BeginString", "FIXT.1.1");
      d.setString("DefaultApplVerID", "FIX.5.0SP2");
      d.setString("SenderCompID", "Coinbase");
      d.setString("TargetCompID", "testkey");
      d.setInt("SocketAcceptPort", port);
      d.setString("StartTime", "00:00:00");
      d.setString("EndTime", "00:00:00");
      d.setString("UseDataDictionary", "Y");
      d.setString("TransportDataDictionary", dir + "/FIXT11-venue.xml");
      d.setString("AppDataDictionary", here("fix/FIX50-prod-sand.xml"));
      settings.set(FIX::SessionID("FIXT.1.1", "Coinbase", "testkey"), d);
      store = std::make_unique<FIX::MemoryStoreFactory>();
      acceptor = std::make_unique<FIX::SocketAcceptor>(venue, *store, settings);
      acceptor->start();
    }
    void TearDown() override
    {
      if (acceptor)
        acceptor->stop(true);
      acceptor.reset(); // before the store factory its sessions use
    }
    CbxOMConfig cfg()
    {
      CbxOMConfig c;
      c.session.environment = Environment::Sandbox;
      c.session.host = "127.0.0.1";
      c.session.port = port;
      c.session.key_file = keyfile;
      c.session.store_dir = dir;
      c.session.transport_dict = here("fix/FIXT11-prod-sand.xml");
      c.session.app_dict = here("fix/FIX50-prod-sand.xml");
      OEProduct p;
      std::string err;
      EXPECT_TRUE(OEProduct::make("BTC-USD", "0.01", "0.00000001", "0.0001", p, err));
      c.products["BTC-USD"] = p;
      return c;
    }
  };
}

TEST(QfDictionary, PublishedTransportDictionaryLacksRawDataLength)
{
  FIX::DataDictionary transport(here("fix/FIXT11-prod-sand.xml"));
  EXPECT_FALSE(transport.isField(95)) << "if this fails, Coinbase fixed its dictionary: drop the patch in QfSessionTest";
  EXPECT_TRUE(transport.isField(96));
}

TEST(QfDictionary, CoinbaseDictionariesLoadAndValidateTheOMsMessages)
{
  FIX::DataDictionary transport(here("fix/FIXT11-prod-sand.xml"));
  FIX::DataDictionary app(here("fix/FIX50-prod-sand.xml"));
  // an ExecutionReport Trade as order-entry-messages5 lists its fields, with the fee group
  std::string er = fix::FixMsg::parse("35=8|49=Coinbase|56=testkey|34=2|52=20261118-17:25:47.637|"
                                      "11=1985ca2d-61ef-49f1-bfce-6c39d8462914|37=7b52009b-64fd-4a2a-89e6-d8a939753077|"
                                      "17=22222222-2222-4222-8222-222222222222|39=1|150=F|55=BTC-USD|54=1|32=0.0001|"
                                      "31=64000.00|14=0.0001|151=0.0024|60=20261118-17:25:47.637|1003=1135493769|"
                                      "1057=N|136=1|137=0.0005|138=USD|139=4|891=2|",
                                      '|')
                       ->to_wire();
  FIX::Message m;
  EXPECT_NO_THROW(m.setString(er, true, &transport, &app));
  EXPECT_NO_THROW(FIX::DataDictionary::validate(m, &transport, &app));
  // what the OM sends: NewOrderSingle, OrderCancelRequest, OrderStatusRequest
  for (const char *s : {"35=D|49=testkey|56=Coinbase|34=2|52=20261118-17:25:47.637|11=1985ca2d-61ef-49f1-bfce-6c39d8462914|"
                        "55=BTC-USD|54=1|38=0.00250000|40=2|44=64000.00|59=1|7928=D|",
                        "35=F|49=testkey|56=Coinbase|34=3|52=20261118-17:25:47.637|11=2985ca2d-61ef-49f1-bfce-6c39d8462914|"
                        "37=7b52009b-64fd-4a2a-89e6-d8a939753077|41=1985ca2d-61ef-49f1-bfce-6c39d8462914|55=BTC-USD|",
                        "35=H|49=testkey|56=Coinbase|34=4|52=20261118-17:25:47.637|11=1985ca2d-61ef-49f1-bfce-6c39d8462914|"
                        "37=7b52009b-64fd-4a2a-89e6-d8a939753077|55=BTC-USD|"})
  {
    FIX::Message q;
    auto wire = fix::FixMsg::parse(s, '|')->to_wire();
    EXPECT_NO_THROW(q.setString(wire, true, &transport, &app)) << s;
    EXPECT_NO_THROW(FIX::DataDictionary::validate(q, &transport, &app)) << s;
  }
}

TEST_F(QfSessionTest, LogsOnSignedPlacesFillsAndCancelsThroughQuickFix)
{
  Recorder somr{"SOM"};
  auto btc = test_product("BTC-USD");
  auto om = new CbxOM(cfg(), &somr, quickfix_session_factory(), en::x::COINBASEX, frame::som::ClientIdGen(9000));
  auto mgr = std::make_unique<actors::Manager>("test");
  mgr->manage_and_start(om);
  ASSERT_TRUE(wait_until([&] { return om->ready(); }, std::chrono::seconds(10))) << "never logged on";
  EXPECT_EQ(venue.logons, 1);
  EXPECT_EQ(venue.bad_signatures, 0);
  EXPECT_EQ(venue.cod.load(), 'Y');
  som::Order o("CB", en::x::COINBASEX, btc->id, 25, 6400000, en::bs::BUY, 4);
  o.oid = 1;
  om->send(new som::Order(o), &somr);
  ASSERT_NE(somr.wait_for<som::Fill>(), nullptr);
  EXPECT_EQ(somr.all<som::Fill>()[0]->sz, 1);
  EXPECT_EQ(somr.all<som::Fill>()[0]->pxi, 6400000);
  om->send(new som::Cancel(1), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(), nullptr);
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  {
    std::lock_guard<std::mutex> l(venue.mu);
    EXPECT_NE(venue.last_order.find("\x01" "38=0.00250000\x01"), std::string::npos);
  }
  mgr->end();
  delete om;
  // the message log has no secrets
  std::string all;
  for (auto name : {"/log/testkey-Coinbase.messages.log"})
  {
    std::ifstream f(dir + name);
    all.assign(std::istreambuf_iterator<char>(f), {});
  }
  EXPECT_NE(all.find("35=A"), std::string::npos) << "the Logon is logged";
  EXPECT_EQ(all.find("testpass"), std::string::npos) << "the passphrase is not";
  EXPECT_NE(all.find("554=***"), std::string::npos);
  EXPECT_NE(all.find("96=***"), std::string::npos);
}

TEST_F(QfSessionTest, BadSecretIsRejectedAndNotRetried)
{
  {
    std::ofstream f(keyfile);
    f << R"({"environment":"sandbox","key":"testkey","secret":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","passphrase":"testpass"})";
  }
  chmod(keyfile.c_str(), 0600);
  Recorder somr{"SOM"};
  auto om = new CbxOM(cfg(), &somr, quickfix_session_factory(), en::x::COINBASEX, frame::som::ClientIdGen(9100));
  auto mgr = std::make_unique<actors::Manager>("test");
  mgr->manage_and_start(om);
  ASSERT_TRUE(wait_until([&] { return venue.bad_signatures > 0; }, std::chrono::seconds(10)));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_FALSE(om->ready());
  EXPECT_EQ(om->sessions_created(), 1) << "a rejected logon is not retried";
  mgr->end();
  delete om;
}

TEST(QfGuard, SandboxSessionRefusesAProductionHost)
{
  const char *t = std::getenv("TMPDIR");
  std::string kf = std::string(t ? t : "/tmp") + "/cbx_guard_keys.json";
  {
    std::ofstream f(kf);
    f << R"({"environment":"sandbox","key":"k","secret":")" << SECRET << R"(","passphrase":"p"})";
  }
  chmod(kf.c_str(), 0600);
  SessionConfig c;
  c.environment = Environment::Sandbox;
  c.host = "fix-ord.exchange.coinbase.com";
  c.key_file = kf;
  struct Nop : SessionEvents
  {
    void logged_on() override {}
    void logon_rejected(const std::string &) override {}
    void disconnected(const std::string &) override {}
    void busy() override {}
    void available() override {}
    void message(const fix::FixMsg &) override {}
  } ev;
  EXPECT_THROW(quickfix_session_factory()(c, ev), std::runtime_error) << "no connection is attempted";
  c.host = "";
  EXPECT_NO_THROW(quickfix_session_factory()(c, ev)) << "the sandbox host from the environment";
  std::remove(kf.c_str());
}

#endif
