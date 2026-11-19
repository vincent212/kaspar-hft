/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Kraken post-trade: the fill -> TradeCapture mapping on the documentation's
// examples, the ledger across stream and REST, KrakenFillsClient on SimFills,
// and the futures fills session against a fake Kraken Futures REST API.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_pt/SimFills.hpp"
#include "kraken_pt/if/KrakenFillsClient.hpp"

using namespace kraken::pt;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace
{
  template <class P>
  bool wait_until(P p, std::chrono::milliseconds limit = 5s)
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (p())
        return true;
      std::this_thread::sleep_for(2ms);
    }
    return p();
  }

  class Sink : public actors::Actor
  {
  public:
    Sink() { strncpy(name, "sink", sizeof(name) - 1); }
    ~Sink() override
    {
      for (auto m : got)
        delete m;
    }
    const char *get_name() const override { return name; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu);
      if (auto t = dynamic_cast<const post_trade::msg::TradeCapture *>(m))
        got.push_back(t);
      else
        delete m;
    }
    std::vector<const post_trade::msg::TradeCapture *> all()
    {
      std::lock_guard<std::mutex> l(mu);
      return got;
    }
    size_t size() { return all().size(); }

  private:
    std::mutex mu;
    std::vector<const post_trade::msg::TradeCapture *> got;
  };

  FillsConfig spot_cfg()
  {
    FillsConfig c;
    c.ep.product = kraken::oe::Product::Spot;
    c.ep.testnet = false;
    c.ep.rest_host = "api.kraken.com";
    c.ep.ws_host = "ws-auth.kraken.com";
    c.key_file = "/nonexistent";
    PtInstrument i;
    i.symbol = "BTC/USD";
    i.rest_pair = "XXBTZUSD";
    i.asset = "KRAKEN_BTCUSD";
    i.lot = "0.00000001";
    i.venue = en::x::KRAKEN;
    c.instruments = {i};
    return c;
  }

  FillsConfig fut_cfg(const std::string &symbol = "PF_XBTUSD", const std::string &lot = "0.0001")
  {
    FillsConfig c;
    c.key_file = "/nonexistent";
    PtInstrument i;
    i.symbol = symbol;
    i.asset = "KRAKENF_PFXBTUSD";
    i.lot = lot;
    i.venue = en::x::KRAKENF;
    c.instruments = {i};
    return c;
  }

  // exchange/api-reference/spot-websocket-v2/executions, "Execution" example.
  const char *kSpotExec = R"({"order_id":"OK4GJX-KSTLS-7DZZO5","order_userref":3,"exec_id":"TGBB7L-HT5LX-J3BZ4A",
      "exec_type":"trade","trade_id":62887576,"symbol":"BTC/USD","side":"sell","last_qty":0.005,
      "last_price":26599.9,"liquidity_ind":"t","cost":132.9995,"order_type":"limit",
      "timestamp":"2023-09-22T10:33:05.709993Z","order_status":"partially_filled","cum_qty":0.005,
      "cum_cost":132.9995,"avg_price":26599.9,"fee_usd_equiv":0.3458,"fees":[{"asset":"USD","qty":0.3458}]})";

  // api-reference/account-data/get-trades-history example, first row.
  const char *kSpotRest = R"({"ordertxid":"OQCLML-BW3P3-BUCMWZ","postxid":"TKH2SE-M7IF5-CFI7LT","pair":"XXBTZUSD",
      "time":1688667796.8802,"type":"buy","ordertype":"limit","price":"30010.00000","cost":"600.20000",
      "fee":"0.00000","vol":"0.02000000","margin":"0.00000","misc":"","trade_id":40274859,"maker":true})";

  json fut_fill(const std::string &id, const std::string &time, const std::string &size = "0.0004")
  {
    return kraken::parse_raw(R"({"fill_id":")" + id + R"(","fillTime":")" + time +
                             R"(","fillType":"maker","order_id":"o1","cliOrdId":"c1","price":10000.5,"side":"buy",
                             "size":)" + size + R"(,"symbol":"PF_XBTUSD"})");
  }
}

// ---- mapping -----------------------------------------------------------------------------------

TEST(KrakenFills, SpotExecutionExampleMaps)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(kraken::parse_raw(kSpotExec), "", Source::SpotWs, spot_cfg(), t, err)) << err;
  EXPECT_EQ(t.venue, en::x::KRAKEN);
  EXPECT_EQ(t.trade_id, "TGBB7L-HT5LX-J3BZ4A");
  EXPECT_EQ(t.report_id, t.trade_id);
  EXPECT_EQ(t.event, post_trade::Event::New);
  EXPECT_EQ(t.venue_order_id, "OK4GJX-KSTLS-7DZZO5");
  EXPECT_EQ(t.symbol, "BTC/USD");
  EXPECT_EQ(t.side, post_trade::Side::Sell);
  EXPECT_EQ(t.qty, 500000); // 0.005 BTC in 1e-8 lots
  EXPECT_EQ(t.price_mantissa, 265999);
  EXPECT_EQ(t.price_decimals, 1);
  EXPECT_EQ(t.trade_date, 20230922u);
  EXPECT_EQ(t.trade_time_ns, ((10 * 3600 + 33 * 60 + 5) * 1'000'000'000LL) + 709'993'000LL);
  EXPECT_FALSE(t.poss_dup);
}

TEST(KrakenFills, SpotTradesHistoryExampleMapsToTheSameSymbol)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(
      to_trade_capture(kraken::parse_raw(kSpotRest), "THVRQM-33VKH-UCI7BS", Source::SpotRest, spot_cfg(), t, err))
      << err;
  EXPECT_EQ(t.trade_id, "THVRQM-33VKH-UCI7BS");
  EXPECT_EQ(t.symbol, "BTC/USD");
  EXPECT_EQ(t.side, post_trade::Side::Buy);
  EXPECT_EQ(t.qty, 2000000);
  EXPECT_EQ(t.price_mantissa, 3001000000);
  EXPECT_EQ(t.price_decimals, 5);
  EXPECT_EQ(t.trade_date, 20230706u);
  EXPECT_EQ(t.trade_time_ns, (18 * 3600 + 23 * 60 + 16) * 1'000'000'000LL + 880'200'000LL);
  EXPECT_TRUE(t.poss_dup);
}

TEST(KrakenFills, FuturesFillsExampleMaps)
{
  // api-reference/historical-data/get-your-fills example (PI_XBTUSD, size in
  // contracts: lot 1).
  auto f = kraken::parse_raw(R"({"fill_id":"98e3deeb-0385-4b25-b15e-7e8453512cb2","fillTime":"2021-11-18T02:39:41.826Z",
      "fillType":"maker","order_id":"06b9d509-965c-4788-b317-0e5ca11d56fb","price":47000,"side":"buy","size":10,
      "symbol":"PI_XBTUSD","realized_pnl":12.5})");
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(f, "", Source::FuturesPoll, fut_cfg("PI_XBTUSD", "1"), t, err)) << err;
  EXPECT_EQ(t.venue, en::x::KRAKENF);
  EXPECT_EQ(t.trade_id, "98e3deeb-0385-4b25-b15e-7e8453512cb2");
  EXPECT_EQ(t.qty, 10);
  EXPECT_EQ(t.price_mantissa, 47000);
  EXPECT_EQ(t.price_decimals, 0);
  EXPECT_EQ(t.trade_date, 20211118u);
}

TEST(KrakenFills, UnmappableFillsAreRefusedWithAReason)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  auto other = kraken::parse_raw(std::string(kSpotExec));
  other["symbol"] = "ETH/USD";
  EXPECT_FALSE(to_trade_capture(other, "", Source::SpotWs, spot_cfg(), t, err));
  EXPECT_NE(err.find("not configured"), std::string::npos);
  EXPECT_FALSE(to_trade_capture(fut_fill("x", "2030-01-01T00:00:00Z", "0.00015"), "", Source::FuturesPoll, fut_cfg(), t,
                                err));
  EXPECT_NE(err.find("whole number of lots"), std::string::npos);
  auto status = kraken::parse_raw(R"({"order_id":"O","exec_type":"new"})");
  EXPECT_FALSE(to_trade_capture(status, "", Source::SpotWs, spot_cfg(), t, err));
  int64_t m;
  int d;
  EXPECT_TRUE(parse_decimal("0.00000001", m, d));
  EXPECT_EQ(m, 1);
  EXPECT_EQ(d, 8);
  EXPECT_FALSE(parse_decimal("1e5", m, d));
}

TEST(KrakenFills, LedgerTakesAStreamFillAndItsRestCopyOnce)
{
  post_trade::TradeLedger ledger;
  post_trade::msg::TradeCapture ws, rest;
  std::string err;
  ASSERT_TRUE(to_trade_capture(kraken::parse_raw(kSpotExec), "", Source::SpotWs, spot_cfg(), ws, err));
  // The same trade from TradesHistory, keyed by its txid (= exec_id).
  auto row = kraken::parse_raw(R"({"ordertxid":"OK4GJX-KSTLS-7DZZO5","pair":"XXBTZUSD","time":1695378785.709993,
      "type":"sell","ordertype":"limit","price":"26599.9","vol":"0.00500000","trade_id":62887576,"maker":false})");
  ASSERT_TRUE(to_trade_capture(row, "TGBB7L-HT5LX-J3BZ4A", Source::SpotRest, spot_cfg(), rest, err)) << err;
  EXPECT_TRUE(ledger.apply(ws).emit());
  EXPECT_EQ(ledger.apply(rest).what, post_trade::Applied::Duplicate);
  EXPECT_EQ(ledger.net_qty("BTC/USD"), -500000);
}

TEST(KrakenFills, ConfigFileAndTestnetGuard)
{
  FillsConfig c;
  std::string err;
  ASSERT_TRUE(FillsConfig::from_file("config/kraken_fills_futures_demo_example.ini", c, err)) << err;
  EXPECT_TRUE(c.ep.testnet);
  EXPECT_EQ(c.ep.rest_host, "demo-futures.kraken.com");
  ASSERT_EQ(c.instruments.size(), 1u);
  EXPECT_EQ(c.instruments[0].lot, "0.0001");
  const std::string p = "build/kraken_fills_bad.ini";
  std::ofstream(p) << "[kraken_fills]\ntestnet = true\nrest_host = futures.kraken.com\nkey_file = /k\n"
                      "instruments = PF_XBTUSD||A|0.0001\n";
  EXPECT_FALSE(FillsConfig::from_file(p, c, err));
  std::ofstream(p) << "[kraken_fills]\nproduct = spot\ntestnet = true\nkey_file = /k\n"
                      "instruments = BTC/USD|XXBTZUSD|A|0.00000001\n";
  EXPECT_FALSE(FillsConfig::from_file(p, c, err));
  std::remove(p.c_str());
  EXPECT_EQ(create_KrakenFillsClient(fut_cfg(), nullptr, nullptr), nullptr);
}

// ---- KrakenFillsClient on SimFills ----------------------------------------------------------------

namespace
{
  struct ClientRun
  {
    Sink sink;
    std::shared_ptr<SimFills::Venue> v = std::make_shared<SimFills::Venue>();
    std::unique_ptr<actors::Manager> mgr;
    KrakenFillsClient *c = nullptr;
    explicit ClientRun(int reconcile_s = 0)
    {
      auto cfg = fut_cfg();
      cfg.sim = true;
      cfg.reconcile_every_s = reconcile_s;
      cfg.reconnect_ms = 50;
      c = new KrakenFillsClient(cfg, &sink, SimFills::make_factory(v));
      mgr = std::make_unique<actors::Manager>("pt");
      mgr->manage_and_start(c);
    }
    ~ClientRun()
    {
      mgr->end();
      mgr.reset();
      delete c;
    }
  };
}

TEST(KrakenFillsClient, StreamFillsReachTheSubscriberOnceDespiteRestCopies)
{
  ClientRun r(1);
  ASSERT_TRUE(wait_until([&] { return r.c->ready() && r.c->stats().passes >= 1; }));
  SimFills::make(*r.v, fut_fill("f1", "2030-01-01T00:00:01.000Z"));
  SimFills::make(*r.v, fut_fill("f2", "2030-01-01T00:00:02.000Z"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 2; }));
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes >= 2; }, 4s)); // a pass returns both again
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_GE(r.c->stats().duplicates, 2u);
  EXPECT_EQ(r.c->stats().added_by_rest, 0u);
  EXPECT_EQ(r.c->ledger().net_qty("PF_XBTUSD"), 8);
  auto t = r.sink.all()[0];
  EXPECT_EQ(t->trade_id, "f1");
  EXPECT_EQ(t->venue, en::x::KRAKENF);
}

TEST(KrakenFillsClient, FillMissedByTheStreamIsRecoveredByReconciliation)
{
  ClientRun r(1);
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  {
    std::lock_guard<std::mutex> l(r.v->mu);
    r.v->stream_drops = 1;
  }
  SimFills::make(*r.v, fut_fill("lost", "2030-01-01T00:00:01.000Z"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }, 4s));
  EXPECT_EQ(r.c->stats().added_by_rest, 1u);
  EXPECT_EQ(r.sink.all()[0]->trade_id, "lost");
  EXPECT_TRUE(r.sink.all()[0]->poss_dup);
}

TEST(KrakenFillsClient, FillMadeWhileDisconnectedArrivesOnceAfterReconnect)
{
  ClientRun r(0); // reconciliation only at logon
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  SimFills::make(*r.v, fut_fill("f1", "2030-01-01T00:00:01.000Z"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
  SimFills::drop_link(*r.v);
  SimFills::make(*r.v, fut_fill("f2", "2030-01-01T00:00:02.000Z")); // nobody listening
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 2; }));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.c->sessions_created(), 2);
  EXPECT_EQ(r.c->stats().added_by_rest, 1u);
}

TEST(KrakenFillsClient, UnmappedFillIsCountedNotEmitted)
{
  ClientRun r(0);
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  auto f = fut_fill("odd", "2030-01-01T00:00:01.000Z");
  f["symbol"] = "PF_ETHUSD";
  SimFills::make(*r.v, f);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().unmapped >= 1; }));
  EXPECT_EQ(r.sink.size(), 0u);
}

// ---- the futures fills session against a fake REST API ---------------------------------------------

namespace
{
  const char *kSecret = "7zxMEF5p/Z8l2p2U7Ghv6x14Af+Fx+92tPgUdVQ748FOIrEoT9bgT+bTRfXc5pz8na+hL/QdrCVG7bh9KpT0eMTm";

  struct FakeFills
  {
    std::mutex mu;
    json fills = json::array(); // newest first, as Kraken returns them
    std::vector<std::string> targets, hosts;
    int bad_auth = 0;
    kraken::oe::HttpFn fn()
    {
      return [this](const std::string &method, const std::string &host, const std::string &target,
                    const kraken::oe::HttpHeaders &h, const std::string &) -> kraken::HttpsResponse {
        std::lock_guard<std::mutex> l(mu);
        targets.push_back(target);
        hosts.push_back(host);
        std::string nonce, authent;
        for (auto &[k, v] : h)
        {
          if (k == "Nonce")
            nonce = v;
          else if (k == "Authent")
            authent = v;
        }
        auto q = target.find('?');
        std::string params = q == std::string::npos ? "" : target.substr(q + 1);
        if (authent != kraken::oe::futures_authent(params, nonce, "/api/v3/fills", kSecret))
          ++bad_auth;
        json out = json::array();
        std::string before;
        if (params.rfind("lastFillTime=", 0) == 0)
        {
          // url-decoded ISO time: ':' arrives as %3A
          before = params.substr(13);
          for (size_t i; (i = before.find("%3A")) != std::string::npos;)
            before.replace(i, 3, ":");
        }
        for (auto &f : fills)
          if (out.size() < 100 && (before.empty() || f["fillTime"].get<std::string>() < before))
            out.push_back(f);
        return {200, json{{"result", "success"}, {"fills", out}}.dump()};
      };
    }
  };

  struct Ev : FillsEvents
  {
    std::mutex mu;
    std::vector<std::pair<std::string, Source>> got;
    std::atomic<int> logged{0}, failed{0}, passes{0};
    size_t rows = 0;
    void logged_in() override { ++logged; }
    void login_failed(const std::string &) override { ++failed; }
    void disconnected(const std::string &) override {}
    void fill(const json &f, const std::string &, Source s) override
    {
      std::lock_guard<std::mutex> l(mu);
      got.emplace_back(f.value("fill_id", std::string()), s);
    }
    void reconciled(size_t r, bool, const std::string &) override
    {
      rows = r;
      ++passes;
    }
    size_t count(Source s)
    {
      std::lock_guard<std::mutex> l(mu);
      return size_t(std::count_if(got.begin(), got.end(), [s](auto &p) { return p.second == s; }));
    }
  };
}

TEST(KrakenFuturesFills, RefusesProductionHostWhenTestnet)
{
  FakeFills fake;
  auto c = fut_cfg();
  c.ep.rest_host = "futures.kraken.com";
  Ev ev;
  auto s = futures_fills_factory(c, fake.fn())(ev);
  s->logon();
  ASSERT_TRUE(wait_until([&] { return ev.failed == 1; }));
  EXPECT_TRUE(fake.targets.empty());
}

TEST(KrakenFuturesFills, PollReportsNewFillsOnceAndReconcilePagesBack)
{
  FakeFills fake;
  // 150 fills, the newest first; the 10 oldest are before `since`.
  for (int i = 149; i >= 0; --i)
  {
    char t[40];
    std::snprintf(t, sizeof t, "2026-01-01T00:%02d:%02d.000Z", i / 60, i % 60);
    fake.fills.push_back(fut_fill("f" + std::to_string(i), t));
  }
  const std::string kf = "build/kraken_pt_test_keys.json";
  std::ofstream(kf) << json{{"api_key", "K"}, {"api_secret", kSecret}}.dump();
  auto c = fut_cfg();
  c.key_file = kf;
  c.poll_ms = 200;
  Ev ev;
  auto s = futures_fills_factory(c, fake.fn(), kraken::parse_rfc3339("2026-01-01T00:00:10.000Z"))(ev);
  s->logon();
  ASSERT_TRUE(wait_until([&] { return ev.logged == 1; }));
  // The poll sees the newest 100 (f50..f149): all after `since`.
  EXPECT_EQ(ev.count(Source::FuturesPoll), 100u);
  std::this_thread::sleep_for(500ms);
  EXPECT_EQ(ev.count(Source::FuturesPoll), 100u);
  {
    std::lock_guard<std::mutex> l(fake.mu);
    fake.fills.insert(fake.fills.begin(), fut_fill("new", "2026-01-01T00:03:00.000Z"));
  }
  ASSERT_TRUE(wait_until([&] { return ev.count(Source::FuturesPoll) == 101; }));
  // Reconciliation from 00:00:00 pages back through all 151.
  ASSERT_TRUE(s->reconcile(kraken::parse_rfc3339("2026-01-01T00:00:00.000Z")));
  ASSERT_TRUE(wait_until([&] { return ev.passes == 1; }));
  EXPECT_EQ(ev.rows, 151u);
  EXPECT_EQ(fake.bad_auth, 0);
  for (auto &h : fake.hosts)
    EXPECT_EQ(h, "demo-futures.kraken.com");
  std::remove(kf.c_str());
}
