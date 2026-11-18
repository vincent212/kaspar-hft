/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The Kraken Futures session against a fake Kraken Futures REST API (FakeHttp
// below) that checks every request's host and Authent and answers in the
// documentation's response formats; then KrakenOM on that session end to
// end. No network.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_oe/KrakenSessions.hpp"

#include "TestUtil.hpp"

using namespace kraken::oe;
using namespace kraken::oe::test;
namespace som = frame::som::msg;
using json = nlohmann::json;

namespace
{
  const char *kSecret = "7zxMEF5p/Z8l2p2U7Ghv6x14Af+Fx+92tPgUdVQ748FOIrEoT9bgT+bTRfXc5pz8na+hL/QdrCVG7bh9KpT0eMTm";

  std::map<std::string, std::string> form(const std::string &s)
  {
    std::map<std::string, std::string> m;
    size_t i = 0;
    while (i < s.size())
    {
      auto amp = s.find('&', i);
      auto kv = s.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
      auto eq = kv.find('=');
      if (eq != std::string::npos)
        m[kv.substr(0, eq)] += (m.count(kv.substr(0, eq)) ? "," : "") + kv.substr(eq + 1);
      if (amp == std::string::npos)
        break;
      i = amp + 1;
    }
    return m;
  }

  // A fake Kraken Futures demo: orders rest until the test fills or cancels
  // them.
  struct FakeHttp
  {
    struct Ord
    {
      std::string id, cl, symbol, side, size, price, status = "ENTERED_BOOK", reason;
      uint64_t ended_ms = 0;
    };
    std::mutex mu;
    std::vector<std::string> calls; // "POST /derivatives/api/v3/sendorder"
    std::vector<std::string> hosts;
    int bad_auth = 0, n = 0;
    std::map<std::string, Ord> orders; // by id
    json fills = json::array();
    std::string reject_status;  // next sendorder answered with this status
    bool fail_network = false;  // throw on the next call
    bool auth_error = false;    // answer every call with authenticationError
    bool hide_status = false;   // orders/status returns nothing

    std::string now_iso()
    {
      return "2030-01-01T00:00:00.000Z"; // after any since_ns used here
    }

    HttpFn fn()
    {
      return [this](const std::string &method, const std::string &host, const std::string &target,
                    const HttpHeaders &h, const std::string &body) -> kraken::HttpsResponse {
        std::lock_guard<std::mutex> l(mu);
        hosts.push_back(host);
        calls.push_back(method + " " + target.substr(0, target.find('?')));
        if (fail_network)
        {
          fail_network = false;
          throw std::runtime_error("connection reset");
        }
        // Check Authent as Kraken would: endpoint path without /derivatives.
        std::string path = target.substr(0, target.find('?'));
        std::string params = method == "GET" ? (target.find('?') == std::string::npos ? "" : target.substr(target.find('?') + 1)) : body;
        std::string nonce, authent, key;
        for (auto &[k, v] : h)
        {
          if (k == "Nonce")
            nonce = v;
          if (k == "Authent")
            authent = v;
          if (k == "APIKey")
            key = v;
        }
        if (key != "DEMOKEY" || authent != futures_authent(params, nonce, path.substr(strlen("/derivatives")), kSecret))
          ++bad_auth;
        if (auth_error)
          return {200, R"({"result":"error","serverTime":"x","error":"authenticationError"})"};
        auto p = form(params);
        if (path == "/derivatives/api/v3/sendorder")
        {
          if (!reject_status.empty())
          {
            auto s = reject_status;
            reject_status.clear();
            return {200, json{{"result", "success"}, {"sendStatus", {{"status", s}, {"receivedTime", now_iso()}}}}.dump()};
          }
          Ord o;
          o.id = "uuid-" + std::to_string(++n);
          o.cl = p["cliOrdId"];
          o.symbol = p["symbol"];
          o.side = p["side"];
          o.size = p["size"];
          o.price = p["limitPrice"];
          orders[o.id] = o;
          return {200, json{{"result", "success"},
                            {"sendStatus",
                             {{"order_id", o.id}, {"status", "placed"}, {"receivedTime", now_iso()}, {"cliOrdId", o.cl}}}}
                           .dump()};
        }
        if (path == "/derivatives/api/v3/cancelorder")
        {
          for (auto &[id, o] : orders)
            if ((p.count("order_id") && id == p["order_id"]) || (p.count("cliOrdId") && o.cl == p["cliOrdId"]))
            {
              if (o.status != "ENTERED_BOOK")
                return {200, R"({"result":"success","cancelStatus":{"status":"notFound"}})"};
              o.status = "CANCELLED";
              o.reason = "CANCELLED_BY_USER";
              return {200, json{{"result", "success"},
                                {"cancelStatus", {{"status", "cancelled"}, {"order_id", id}}}}
                               .dump()};
            }
          return {200, R"({"result":"success","cancelStatus":{"status":"notFound"}})"};
        }
        if (path == "/derivatives/api/v3/fills")
          return {200, json{{"result", "success"}, {"fills", fills}}.dump()};
        if (path == "/derivatives/api/v3/orders/status")
        {
          json out = json::array();
          if (!hide_status)
            for (auto &[id, o] : orders)
            {
              bool want = p["orderIds"].find(id) != std::string::npos ||
                          (!o.cl.empty() && p["cliOrdIds"].find(o.cl) != std::string::npos);
              if (!want)
                continue;
              out.push_back({{"order", {{"orderId", id}, {"cliOrdId", o.cl}, {"symbol", o.symbol}, {"side", o.side}}},
                             {"status", o.status},
                             {"updateReason", o.reason.empty() ? json() : json(o.reason)},
                             {"error", nullptr}});
            }
          return {200, json{{"result", "success"}, {"orders", out}}.dump()};
        }
        if (path == "/derivatives/api/v3/cancelallordersafter")
          return {200, R"({"result":"success","status":{"currentTime":"x","triggerTime":"y"}})"};
        return {404, "{}"};
      };
    }

    void fill(const std::string &id, const std::string &fill_id, const std::string &size, const std::string &px,
              const std::string &time = "2030-01-01T00:00:01.000Z")
    {
      std::lock_guard<std::mutex> l(mu);
      auto &o = orders.at(id);
      fills.push_back({{"fill_id", fill_id}, {"fillTime", time}, {"fillType", "maker"}, {"order_id", id},
                       {"cliOrdId", o.cl}, {"price", json::parse(px)}, {"side", o.side},
                       {"size", json::parse(size)}, {"symbol", o.symbol}});
    }
    void set_status(const std::string &id, const std::string &st, const std::string &why = "")
    {
      std::lock_guard<std::mutex> l(mu);
      orders.at(id).status = st;
      orders.at(id).reason = why;
    }
    std::string only_order()
    {
      std::lock_guard<std::mutex> l(mu);
      return orders.empty() ? std::string() : orders.rbegin()->first;
    }
    size_t count(const std::string &call)
    {
      std::lock_guard<std::mutex> l(mu);
      return size_t(std::count(calls.begin(), calls.end(), call));
    }
  };

  struct Events : SessionEvents
  {
    std::mutex mu;
    std::vector<Report> reports;
    std::atomic<int> logged{0}, failed{0}, lost{0};
    std::string why;
    void logged_in() override { ++logged; }
    void login_failed(const std::string &r) override
    {
      std::lock_guard<std::mutex> l(mu);
      why = r;
      ++failed;
    }
    void disconnected(const std::string &r) override
    {
      std::lock_guard<std::mutex> l(mu);
      why = r;
      ++lost;
    }
    void busy() override {}
    void available() override {}
    void report(const Report &r) override
    {
      std::lock_guard<std::mutex> l(mu);
      reports.push_back(r);
    }
    void gap(uint64_t, uint64_t) override {}
    std::vector<Report> all()
    {
      std::lock_guard<std::mutex> l(mu);
      return reports;
    }
    size_t count(Report::Kind k)
    {
      std::lock_guard<std::mutex> l(mu);
      return size_t(std::count_if(reports.begin(), reports.end(), [k](const Report &r) { return r.kind == k; }));
    }
  };

  class FuturesSessionTest : public ::testing::Test
  {
  protected:
    FakeHttp http;
    Settings s;
    std::string keyfile = "build/kraken_test_demo_keys.json";
    void SetUp() override
    {
      std::ofstream(keyfile) << json{{"api_key", "DEMOKEY"}, {"api_secret", kSecret}}.dump();
      s.key_file = keyfile;
      s.poll_ms = 200;
    }
    void TearDown() override { std::remove(keyfile.c_str()); }
    uint64_t since() { return kraken::parse_rfc3339("2029-12-31T00:00:00Z"); }
  };

  NewOrder new_order(const std::string &cl)
  {
    NewOrder o;
    o.cl_ord_id = cl;
    o.symbol = "PF_XBTUSD";
    o.qty = "0.0010";
    o.price = "10000.0";
    return o;
  }
}

TEST_F(FuturesSessionTest, RefusesProductionHostWhenTestnetBeforeAnyRequest)
{
  s.ep.rest_host = "futures.kraken.com";
  Events ev;
  auto f = futures_session_factory(s, http.fn(), since());
  auto ses = f(ev);
  ses->logon();
  ASSERT_TRUE(wait_until([&] { return ev.failed == 1; }));
  EXPECT_NE(ev.why.find("refused"), std::string::npos);
  EXPECT_TRUE(http.calls.empty()); // nothing was sent anywhere
}

TEST_F(FuturesSessionTest, AuthenticationErrorIsALoginFailure)
{
  http.auth_error = true;
  Events ev;
  auto ses = futures_session_factory(s, http.fn(), since())(ev);
  ses->logon();
  ASSERT_TRUE(wait_until([&] { return ev.failed == 1; }));
  EXPECT_EQ(ev.logged, 0);
}

TEST_F(FuturesSessionTest, PlaceFillCancelWithSignedRequestsToDemoOnly)
{
  Events ev;
  auto ses = futures_session_factory(s, http.fn(), since())(ev);
  ses->logon();
  ASSERT_TRUE(wait_until([&] { return ev.logged == 1; }));
  ASSERT_EQ(ses->add_order(new_order("c1")), CallStatus::Success);
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Accepted) == 1; }));
  auto id = http.only_order();
  EXPECT_EQ(ev.all()[0].order_id, id);
  http.fill(id, "f1", "0.0004", "10000.0");
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Trade) == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(500)); // more polls: the fill is not repeated
  EXPECT_EQ(ev.count(Report::Trade), 1u);
  auto t = ev.all()[1];
  EXPECT_EQ(t.exec_id, "f1");
  EXPECT_EQ(t.cl_ord_id, "c1");
  EXPECT_EQ(t.last_qty, "0.0004");
  ASSERT_EQ(ses->cancel_order(id, "c1"), CallStatus::Success);
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Canceled) == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ(ev.count(Report::Canceled), 1u); // status poll does not repeat it
  EXPECT_EQ(ev.count(Report::Accepted), 1u); // nor the acceptance
  EXPECT_EQ(http.bad_auth, 0);
  for (auto &h : http.hosts)
    EXPECT_EQ(h, "demo-futures.kraken.com");
  EXPECT_GE(http.count("POST /derivatives/api/v3/orders/status"), 1u);
}

TEST_F(FuturesSessionTest, RejectStatusBecomesRejected)
{
  Events ev;
  auto ses = futures_session_factory(s, http.fn(), since())(ev);
  ses->logon();
  ASSERT_TRUE(wait_until([&] { return ev.logged == 1; }));
  http.reject_status = "postWouldExecute";
  ses->add_order(new_order("c1"));
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Rejected) == 1; }));
  EXPECT_EQ(ev.all()[0].reason, "postWouldExecute");
  EXPECT_EQ(ev.all()[0].cl_ord_id, "c1");
}

TEST_F(FuturesSessionTest, UnsolicitedCancelAndFullFillFromStatusPolling)
{
  Events ev;
  auto ses = futures_session_factory(s, http.fn(), since())(ev);
  ses->logon();
  ASSERT_TRUE(wait_until([&] { return ev.logged == 1; }));
  ses->add_order(new_order("c1"));
  ses->add_order(new_order("c2"));
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Accepted) == 2; }));
  std::string id1, id2;
  for (auto &r : ev.all())
    (r.cl_ord_id == "c1" ? id1 : id2) = r.order_id;
  http.set_status(id1, "CANCELLED", "DEAD_MAN_SWITCH");
  http.fill(id2, "f9", "0.0010", "10000.0");
  http.set_status(id2, "FULLY_EXECUTED", "FULL_FILL");
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Canceled) == 1 && ev.count(Report::Filled) == 1; }));
  auto all = ev.all();
  // The fill precedes the Filled status.
  size_t ti = 0, fi = 0;
  for (size_t i = 0; i < all.size(); ++i)
  {
    if (all[i].kind == Report::Trade)
      ti = i;
    if (all[i].kind == Report::Filled)
      fi = i;
    if (all[i].kind == Report::Canceled)
      EXPECT_EQ(all[i].reason, "DEAD_MAN_SWITCH");
  }
  EXPECT_LT(ti, fi);
}

TEST_F(FuturesSessionTest, OrderAbsentFromStatusIsEndedAfterThreePolls)
{
  Events ev;
  auto ses = futures_session_factory(s, http.fn(), since())(ev);
  ses->logon();
  ASSERT_TRUE(wait_until([&] { return ev.logged == 1; }));
  ses->add_order(new_order("c1"));
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Accepted) == 1; }));
  http.hide_status = true;
  ASSERT_TRUE(wait_until([&] { return ev.count(Report::Canceled) == 1; }, std::chrono::seconds(3)));
  EXPECT_NE(ev.all().back().reason.find("absent"), std::string::npos);
}

TEST_F(FuturesSessionTest, HistoryFillsAreNotReportedAndReconnectReportsMissedFillOnce)
{
  {
    std::lock_guard<std::mutex> l(http.mu);
    http.fills.push_back({{"fill_id", "old"}, {"fillTime", "2020-01-01T00:00:00.000Z"}, {"fillType", "taker"},
                          {"order_id", "x"}, {"price", 1}, {"side", "buy"}, {"size", 1}, {"symbol", "PF_XBTUSD"}});
  }
  auto factory = futures_session_factory(s, http.fn(), since());
  Events ev1;
  auto s1 = factory(ev1);
  s1->logon();
  ASSERT_TRUE(wait_until([&] { return ev1.logged == 1; }));
  s1->add_order(new_order("c1"));
  ASSERT_TRUE(wait_until([&] { return ev1.count(Report::Accepted) == 1; }));
  auto id = http.only_order();
  http.fill(id, "f1", "0.0003", "10000.0");
  ASSERT_TRUE(wait_until([&] { return ev1.count(Report::Trade) == 1; }));
  http.fail_network = true;
  ASSERT_TRUE(wait_until([&] { return ev1.lost == 1; }));
  s1.reset();
  http.fill(id, "f2", "0.0002", "10000.0"); // while disconnected
  Events ev2;
  auto s2 = factory(ev2);
  s2->logon();
  ASSERT_TRUE(wait_until([&] { return ev2.count(Report::Trade) == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ(ev2.count(Report::Trade), 1u);
  EXPECT_EQ(ev2.all()[0].exec_id, "f2");
  for (auto &r : ev1.all())
    EXPECT_NE(r.exec_id, "old");
}

TEST_F(FuturesSessionTest, NothingQueuedBeforeLogon)
{
  Events ev;
  auto ses = futures_session_factory(s, http.fn(), since())(ev);
  EXPECT_EQ(ses->add_order(new_order("c1")), CallStatus::Disconnected);
}

// ---- KrakenOM end to end on the futures session ---------------------------------------------

TEST_F(FuturesSessionTest, KrakenOMOnFuturesSessionEndToEnd)
{
  auto a = assets();
  Recorder somr{"SOM"};
  auto c = om_config();
  auto *om = new KrakenOM(c, &somr, futures_session_factory(s, http.fn(), since()), en::x::KRAKENF,
                          frame::som::ClientIdGen(7000));
  auto mgr = std::make_unique<actors::Manager>("test");
  mgr->manage_and_start(om);
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));
  som::Order o("KR", en::x::KRAKENF, uint(a.fut), 10, 20000, en::bs::BUY, 4); // 0.001 BTC at 10000.0 (tick 0.5)
  o.oid = 1;
  o.tif = som::Order::TimeInForce::GTC;
  om->send(new som::Order(o), &somr);
  ASSERT_NE(somr.wait_for<som::Ack>(0), nullptr);
  auto id = http.only_order();
  {
    std::lock_guard<std::mutex> l(http.mu);
    EXPECT_EQ(http.orders[id].size, "0.0010");
    EXPECT_EQ(http.orders[id].price, "10000.0");
    EXPECT_EQ(http.orders[id].symbol, "PF_XBTUSD");
  }
  http.fill(id, "f1", "0.0004", "10000.0");
  auto f = somr.wait_for<som::Fill>(0);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->sz, 4);
  EXPECT_EQ(f->still_to_be_filled, 6);
  EXPECT_EQ(f->venue, en::x::KRAKENF);
  om->send(new som::Cancel(1), &somr);
  ASSERT_NE(somr.wait_for<som::CancAck>(0), nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ(somr.replies(), (std::vector<std::string>{"Ack", "Fill", "CancAck"}));
  EXPECT_EQ(http.bad_auth, 0);
  mgr->end();
  mgr.reset();
  delete om;
}
