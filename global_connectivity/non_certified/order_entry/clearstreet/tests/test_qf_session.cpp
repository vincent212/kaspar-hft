/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// QfCsSession over a real QuickFIX connection to a local acceptor on
// 127.0.0.1 that plays Clear Street as the FIX API guide describes: it
// checks the Logon credentials, answers with the account list in Text,
// validates every request with build/ClearStreet-FIX42.xml, acknowledges and
// fills orders, and sends a session Reject for an unauthorized account.
// Never Clear Street itself.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unistd.h>

#include "quickfix/Application.h"
#include "quickfix/MessageStore.h"
#include "quickfix/Session.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/SocketAcceptor.h"

#include "TestUtil.hpp"
#include "clearstreet_om/CsSession.hpp"

using namespace clearstreet::om;
using namespace clearstreet::om::test;
namespace fs = std::filesystem;

namespace
{
  const char *KEY = "FaCm6AO4cDOGfcRGIaZXeQ.test-key-never-real";

  class Acceptor : public FIX::Application
  {
  public:
    std::atomic<int> logons{0}, orders{0}, cancels{0};
    std::string last_username, last_password;

    void onCreate(const FIX::SessionID &) override {}
    void onLogon(const FIX::SessionID &) override { ++logons; }
    void onLogout(const FIX::SessionID &) override {}
    void toAdmin(FIX::Message &m, const FIX::SessionID &) override
    {
      if (m.getHeader().getField(35) == "A")
        m.setField(58, "ACC1,ACC2");
    }
    void fromAdmin(const FIX::Message &m, const FIX::SessionID &) override
    {
      if (m.getHeader().getField(35) != "A")
        return;
      std::lock_guard<std::mutex> l(mu);
      last_username = m.isSetField(553) ? m.getField(553) : "";
      last_password = m.isSetField(554) ? m.getField(554) : "";
      if (last_username != "CLIENT" || last_password != KEY)
        throw FIX::RejectLogon("bad credentials");
    }
    void toApp(FIX::Message &, const FIX::SessionID &) override {}
    void fromApp(const FIX::Message &m, const FIX::SessionID &id) override
    {
      const std::string t = m.getHeader().getField(35);
      if (t == "D")
      {
        ++orders;
        if (m.getField(1) != "ACC1" && m.getField(1) != "ACC2")
        {
          FIX::Message j;
          j.getHeader().setField(35, "3");
          j.setField(45, m.getHeader().getField(34));
          j.setField(371, "1");
          j.setField(372, "D");
          j.setField(373, "5");
          j.setField(58, "Account not authorized");
          FIX::Session::sendToTarget(j, id);
          return;
        }
        auto er = [&](char type, const std::string &cum, const std::string &last) {
          FIX::Message e;
          e.getHeader().setField(35, "8");
          e.setField(37, "01a01234-0000-7000-8000-" + m.getField(11));
          e.setField(17, std::string("E") + type + m.getField(11));
          e.setField(20, "0");
          e.setField(150, std::string(1, type));
          e.setField(39, std::string(1, type));
          e.setField(11, m.getField(11));
          e.setField(1, m.getField(1));
          e.setField(55, m.getField(55));
          e.setField(54, m.getField(54));
          e.setField(38, m.getField(38));
          e.setField(44, m.getField(44));
          e.setField(31, last == "0" ? "0" : m.getField(44));
          e.setField(32, last);
          e.setField(151, type == '2' ? "0" : m.getField(38));
          e.setField(14, cum);
          e.setField(6, cum == "0" ? "0" : m.getField(44));
          e.setField(60, utc_now());
          FIX::Session::sendToTarget(e, id);
        };
        er('0', "0", "0");
        er('2', m.getField(38), m.getField(38));
      }
      else if (t == "F")
        ++cancels;
    }
    std::mutex mu;
  };

  class Events : public SessionEvents
  {
  public:
    void logged_on(const std::string &t) override
    {
      std::lock_guard<std::mutex> l(mu);
      on = true;
      logon_text = t;
    }
    void logged_out(const std::string &) override
    {
      std::lock_guard<std::mutex> l(mu);
      on = false;
    }
    void exec(const ExecReport &r) override
    {
      std::lock_guard<std::mutex> l(mu);
      execs.push_back(r);
    }
    void cancel_reject(const CancelReject &) override {}
    void session_reject(const SessionReject &r) override
    {
      std::lock_guard<std::mutex> l(mu);
      rejects.push_back(r);
    }
    void unparsed(const std::string &w) override
    {
      std::lock_guard<std::mutex> l(mu);
      unparsed_.push_back(w);
    }
    template <class F>
    auto with(F f)
    {
      std::lock_guard<std::mutex> l(mu);
      return f();
    }
    std::mutex mu;
    bool on = false;
    std::string logon_text;
    std::vector<ExecReport> execs;
    std::vector<SessionReject> rejects;
    std::vector<std::string> unparsed_;
  };

  class QfSessionTest : public ::testing::Test
  {
  protected:
    fs::path dir;
    int port = 0;
    Acceptor app;
    FIX::MemoryStoreFactory astore;
    std::unique_ptr<FIX::SocketAcceptor> acc;
    FIX::SessionSettings asettings;

    void SetUp() override
    {
      port = 41000 + int(getpid() % 2000);
      dir = fs::temp_directory_path() / ("cs_qf_" + std::to_string(getpid()));
      fs::remove_all(dir);
      fs::create_directories(dir);
      FIX::Dictionary d;
      d.setString("ConnectionType", "acceptor");
      d.setInt("SocketAcceptPort", port);
      d.setString("SocketReuseAddress", "Y");
      d.setString("NonStopSession", "Y");
      d.setString("UseDataDictionary", "Y");
      d.setString("DataDictionary", CS_DICT);
      d.setInt("HeartBtInt", 30);
      asettings.set(FIX::SessionID("FIX.4.2", "CLST", "CLIENT"), d);
      acc = std::make_unique<FIX::SocketAcceptor>(app, astore, asettings);
      acc->start();
    }
    void TearDown() override
    {
      acc->stop();
      fs::remove_all(dir);
    }
    QfSessionConfig cfg(const std::string &key = KEY)
    {
      QfSessionConfig c;
      c.host = "127.0.0.1";
      c.port = port;
      c.sender_comp_id = "CLIENT";
      c.target_comp_id = "CLST";
      c.api_key = key;
      c.dictionary = CS_DICT;
      c.store_path = (dir / "store").string();
      c.log_path = (dir / "log").string();
      c.reconnect_interval = 1;
      c.extra["NonStopSession"] = "Y";
      return c;
    }
    NewOrderReq order(const std::string &id, const std::string &account = "ACC1")
    {
      NewOrderReq r;
      r.account = account;
      r.clordid = id;
      r.symbol = "AAPL";
      r.qty = "100";
      r.px = "230.25";
      return r;
    }
    std::string slurp_dir(const fs::path &p)
    {
      std::string all;
      for (auto &e : fs::recursive_directory_iterator(p))
        if (e.is_regular_file())
        {
          std::ifstream in(e.path());
          std::stringstream ss;
          ss << in.rdbuf();
          all += ss.str();
        }
      return all;
    }
  };
}

TEST_F(QfSessionTest, LogsOnWithCredentialsOrdersAndFills)
{
  Events ev;
  auto s = qf_session_factory(cfg())(ev);
  EXPECT_EQ(s->send_new(order("BEFORE")), CallStatus::Disconnected); // not logged on: nothing sent
  s->start();
  ASSERT_TRUE(wait_until([&] { return ev.with([&] { return ev.on; }); }));
  EXPECT_EQ(ev.with([&] { return ev.logon_text; }), "ACC1,ACC2");
  EXPECT_EQ(app.last_username, "CLIENT");
  ASSERT_EQ(s->send_new(order("C1")), CallStatus::Success);
  ASSERT_TRUE(wait_until([&] { return ev.with([&] { return ev.execs.size(); }) == 2; }));
  auto ex = ev.with([&] { return ev.execs; });
  EXPECT_EQ(ex[0].exec_type, '0');
  EXPECT_EQ(ex[1].exec_type, '2');
  EXPECT_DOUBLE_EQ(ex[1].cum, 100);
  EXPECT_EQ(ex[1].clordid, "C1");
  EXPECT_EQ(app.orders.load(), 1); // "BEFORE" never went out
  CancelReq c;
  c.account = "ACC1";
  c.orig_clordid = "C1";
  c.clordid = "C2";
  c.symbol = "AAPL";
  c.qty = "100";
  ASSERT_EQ(s->send_cancel(c), CallStatus::Success);
  ASSERT_TRUE(wait_until([&] { return app.cancels.load() == 1; }));
  EXPECT_TRUE(ev.with([&] { return ev.unparsed_.empty(); }));
  s->stop();
  // the API key reaches neither the FIX log nor the message store
  const std::string disk = slurp_dir(dir);
  EXPECT_NE(disk.find("553=CLIENT"), std::string::npos);
  EXPECT_EQ(disk.find(KEY), std::string::npos);
}

TEST_F(QfSessionTest, SessionRejectIsMatchedToTheOrder)
{
  Events ev;
  auto s = qf_session_factory(cfg())(ev);
  s->start();
  ASSERT_TRUE(wait_until([&] { return ev.with([&] { return ev.on; }); }));
  ASSERT_EQ(s->send_new(order("C9", "NOTMINE")), CallStatus::Success);
  ASSERT_TRUE(wait_until([&] { return ev.with([&] { return ev.rejects.size(); }) == 1; }));
  auto j = ev.with([&] { return ev.rejects[0]; });
  EXPECT_EQ(j.clordid, "C9");
  EXPECT_EQ(j.ref_msg_type, "D");
  EXPECT_EQ(j.ref_tag, 1);
  s->stop();
}

TEST_F(QfSessionTest, WrongKeyNeverLogsOn)
{
  Events ev;
  auto s = qf_session_factory(cfg("wrong"))(ev);
  s->start();
  std::this_thread::sleep_for(1500ms);
  EXPECT_FALSE(ev.with([&] { return ev.on; }));
  EXPECT_EQ(s->send_new(order("C1")), CallStatus::Disconnected);
  s->stop();
  EXPECT_EQ(app.orders.load(), 0);
}
