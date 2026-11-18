/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Deribit trades to TradeCapture (from the documentation's examples), and
// DeribitFillsClient against a fake session and against SimFills.

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "deribit_pt/SimFills.hpp"
#include "deribit_pt/act/DeribitFillsClient.hpp"

using namespace deribit::pt;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace
{
  // private/get_user_trades_by_currency_and_time response example,
  // result.trades[1] (BTC-26JUN20).
  const json kRestExample = json::parse(R"({
    "trade_seq": 299513, "trade_id": "47958936", "timestamp": 1589923311862, "tick_direction": 2,
    "state": "filled", "reduce_only": false, "price": 9681.5, "post_only": false, "order_type": "limit",
    "order_id": "3993343822", "matching_id": null, "mark_price": 9684, "liquidity": "M",
    "instrument_name": "BTC-26JUN20", "index_price": 9679.48, "fee_currency": "BTC", "fee": -2.1e-7,
    "direction": "buy", "amount": 10})");

  // private/buy response example, result.trades[0] (ETH-PERPETUAL); the
  // user.trades notification carries the same trade object.
  const json kWsExample = json::parse(R"({
    "trade_seq": 1966056, "trade_id": "ETH-2696083", "timestamp": 1590483938456, "tick_direction": 0,
    "state": "filled", "reduce_only": false, "price": 203.3, "post_only": false, "order_type": "market",
    "order_id": "ETH-584849853", "matching_id": null, "mark_price": 203.28, "liquidity": "T",
    "label": "market0000234", "instrument_name": "ETH-PERPETUAL", "index_price": 203.33,
    "fee_currency": "ETH", "fee": 0.00014757, "direction": "buy", "amount": 40})");

  FillsConfig config()
  {
    FillsConfig c;
    c.ep.test = true;
    c.ep.host = "test.deribit.com";
    c.reconcile_every_s = 0;
    c.reconnect_ms = 50;
    c.instruments = {{"BTC-26JUN20", "BTC", "", "10", en::x::DERIBIT, -1},
                     {"ETH-PERPETUAL", "ETH", "", "1", en::x::DERIBIT, -1},
                     {"BTC-PERPETUAL", "BTC", "", "10", en::x::DERIBIT, -1}};
    return c;
  }

  json ws_fill(const std::string &trade, double amount = 10, uint64_t ms = 0)
  {
    json f = kRestExample;
    f["instrument_name"] = "BTC-PERPETUAL";
    f["trade_id"] = trade;
    f["amount"] = amount;
    f["timestamp"] = ms ? ms : 1589923311862ull;
    return f;
  }
  uint64_t now_ms()
  {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
  }

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
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu);
      if (auto t = dynamic_cast<const post_trade::msg::TradeCapture *>(m))
        got.push_back(t);
      else
        delete m;
    }
    size_t size()
    {
      std::lock_guard<std::mutex> l(mu);
      return got.size();
    }
    post_trade::msg::TradeCapture at(size_t i)
    {
      std::lock_guard<std::mutex> l(mu);
      return *got.at(i);
    }

  private:
    std::mutex mu;
    std::vector<const post_trade::msg::TradeCapture *> got;
  };

  // A fake session: the test pushes WebSocket fills; reconcile() answers with
  // the server's REST rows.
  class FakeServer
  {
  public:
    std::vector<json> rest_rows;
    bool fail_rest = false;
    int sessions = 0;
    int reconciles = 0;

    FillsSessionFactory factory()
    {
      return [this](const FillsConfig &, FillsEvents &ev) { return std::make_unique<S>(*this, ev); };
    }
    void push(const json &f)
    {
      std::lock_guard<std::mutex> l(mu);
      if (ev)
        ev->fill(f, false);
    }
    void drop()
    {
      std::lock_guard<std::mutex> l(mu);
      if (ev)
        ev->disconnected("test drop");
    }

  private:
    struct S : FillsSession
    {
      FakeServer &s;
      S(FakeServer &x, FillsEvents &e) : s(x)
      {
        std::lock_guard<std::mutex> l(s.mu);
        s.ev = &e;
        ++s.sessions;
      }
      ~S() override
      {
        std::lock_guard<std::mutex> l(s.mu);
        s.ev = nullptr;
      }
      void logon() override
      {
        std::lock_guard<std::mutex> l(s.mu);
        s.ev->logged_in();
      }
      CallStatus reconcile(uint64_t, uint64_t) override
      {
        std::lock_guard<std::mutex> l(s.mu);
        ++s.reconciles;
        if (s.fail_rest)
        {
          s.ev->reconciled(0, false, "HTTP 400 unauthorized");
          return CallStatus::Success;
        }
        for (const auto &r : s.rest_rows)
          s.ev->fill(r, true);
        s.ev->reconciled(s.rest_rows.size(), true, "");
        return CallStatus::Success;
      }
      void logout() override {}
    };
    std::mutex mu;
    FillsEvents *ev = nullptr;
  };

  struct Rig
  {
    Sink sink;
    std::unique_ptr<actors::Manager> mgr;
    DeribitFillsClient *c = nullptr;
    Rig(FillsSessionFactory f, FillsConfig cfg = config())
    {
      c = new DeribitFillsClient(cfg, &sink, std::move(f));
      mgr = std::make_unique<actors::Manager>("pt");
      mgr->manage_and_start(c);
    }
    ~Rig()
    {
      mgr->end();
      delete c;
    }
    void settle() { std::this_thread::sleep_for(100ms); }
  };
}

// ---- mapping -------------------------------------------------------------------------------------

TEST(DeribitFillsMapping, RestExampleMapsExactly)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(kRestExample, config(), true, t, err)) << err;
  EXPECT_EQ(t.venue, en::x::DERIBIT);
  EXPECT_EQ(t.trade_id, "BTC:47958936");
  EXPECT_EQ(t.report_id, t.trade_id);
  EXPECT_EQ(t.event, post_trade::Event::New);
  EXPECT_EQ(t.venue_order_id, "3993343822");
  EXPECT_EQ(t.symbol, "BTC-26JUN20");
  EXPECT_EQ(t.side, post_trade::Side::Buy);
  EXPECT_EQ(t.qty, 1);   // 10 USD = one contract
  EXPECT_EQ(t.price_mantissa, 96815);
  EXPECT_EQ(t.price_decimals, 1);
  EXPECT_EQ(t.trade_date, 20200519u);   // 1589923311862 ms = 2020-05-19T21:21:51.862Z
  EXPECT_EQ(t.trade_time_ns, (21 * 3600 + 21 * 60 + 51) * 1000000000ll + 862000000ll);
  EXPECT_TRUE(t.poss_dup);
}

TEST(DeribitFillsMapping, BuyResponseTradeExampleMapsExactly)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(kWsExample, config(), false, t, err)) << err;
  EXPECT_EQ(t.trade_id, "ETH:ETH-2696083");
  EXPECT_EQ(t.venue_order_id, "ETH-584849853");
  EXPECT_EQ(t.account, "market0000234");
  EXPECT_EQ(t.side, post_trade::Side::Buy);
  EXPECT_EQ(t.qty, 40);
  EXPECT_EQ(t.price_mantissa, 2033);
  EXPECT_EQ(t.price_decimals, 1);
  EXPECT_DOUBLE_EQ(t.price(), 203.3);
  EXPECT_EQ(t.trade_date, 20200526u);
  EXPECT_EQ(t.trade_time_ns, (9 * 3600 + 5 * 60 + 38) * 1000000000ll + 456000000ll);
  EXPECT_FALSE(t.poss_dup);
}

TEST(DeribitFillsMapping, UnmappableTradesAreRefused)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  json f = kRestExample;
  f["instrument_name"] = "SOL-PERPETUAL";
  EXPECT_FALSE(to_trade_capture(f, config(), false, t, err));
  f = kRestExample;
  f["amount"] = 15;   // one and a half contracts
  EXPECT_FALSE(to_trade_capture(f, config(), false, t, err));
  EXPECT_NE(err.find("whole number"), std::string::npos);
  f = kRestExample;
  f.erase("trade_id");
  EXPECT_FALSE(to_trade_capture(f, config(), false, t, err));
  f = kRestExample;
  f["price"] = 63000.0;   // printed as 63000.0
  ASSERT_TRUE(to_trade_capture(f, config(), false, t, err)) << err;
  EXPECT_EQ(t.price_mantissa, 63000);
  EXPECT_EQ(t.price_decimals, 0);
}

TEST(DeribitFillsMapping, ParseDecimal)
{
  int64_t m;
  int d;
  ASSERT_TRUE(parse_decimal("0.00192834", m, d));
  EXPECT_EQ(m, 192834);
  EXPECT_EQ(d, 8);
  EXPECT_FALSE(parse_decimal("2.1e-07", m, d));
  EXPECT_FALSE(parse_decimal("", m, d));
}

TEST(DeribitFillsConfig, ReadsTheOrderManagersExampleConfiguration)
{
  FillsConfig c;
  std::string err;
  ASSERT_TRUE(FillsConfig::from_file(DERIBIT_OE_CONFIG, c, err)) << err;
  EXPECT_EQ(c.ep.host, "test.deribit.com");
  ASSERT_EQ(c.instruments.size(), 1u);
  EXPECT_EQ(c.instruments[0].qty_unit, "10");
  EXPECT_EQ(c.reconcile_every_s, 60);
  EXPECT_EQ(c.currencies(), std::vector<std::string>{"BTC"});
  EXPECT_TRUE(c.valid(err)) << err;
}

// ---- client --------------------------------------------------------------------------------------

TEST(DeribitFillsClient, WebSocketTradesReachTheSubscriberOnce)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  s.push(ws_fill("1"));
  s.push(ws_fill("1"));   // the same trade again
  s.push(ws_fill("2", 20));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 2; }));
  r.settle();
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.sink.at(1).qty, 2);
  EXPECT_EQ(r.c->stats().duplicates, 1u);
  EXPECT_EQ(r.c->ledger().live(), 2u);
}

TEST(DeribitFillsClient, ReconciliationAddsMissedTradesOnceAndMatchesTheRest)
{
  FakeServer s;
  const uint64_t t0 = now_ms() - 60000;
  s.rest_rows = {ws_fill("10", 10, t0), ws_fill("11", 10, t0 + 1000)};
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 1; }));
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.c->stats().added_by_rest, 2u);
  s.push(ws_fill("12", 10, t0 + 2000));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 3; }));
  s.rest_rows.push_back(ws_fill("12", 10, t0 + 2000));
  r.c->send(new msg::FillsReport(msg::FillsReport::RECONCILE, 0), nullptr);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 2; }));
  r.settle();
  EXPECT_EQ(r.sink.size(), 3u);
  EXPECT_EQ(r.c->stats().added_by_rest, 2u);
  EXPECT_EQ(r.c->stats().missing_from_rest, 0u);
}

TEST(DeribitFillsClient, AWebSocketTradeMissingFromRestIsCounted)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 1; }));
  s.push(ws_fill("20", 10, now_ms() - 30000));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
  r.c->send(new msg::FillsReport(msg::FillsReport::RECONCILE, 0), nullptr);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 2; }));
  EXPECT_EQ(r.c->stats().missing_from_rest, 1u);
}

TEST(DeribitFillsClient, ReconnectReconcilesAndDoesNotDuplicate)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 1; }));
  s.push(ws_fill("30"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
  s.drop();
  s.rest_rows = {ws_fill("30"), ws_fill("31")};   // 31 happened while down
  ASSERT_TRUE(wait_until([&] { return s.sessions == 2 && r.c->stats().passes == 2; }));
  r.settle();
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.sink.at(1).trade_id, "BTC:31");
}

TEST(DeribitFillsClient, FailedReconciliationIsCountedNotFatal)
{
  FakeServer s;
  s.fail_rest = true;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().failed_passes == 1; }));
  s.push(ws_fill("40"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
}

TEST(DeribitFillsClient, UnmappableTradeIsCounted)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  json f = ws_fill("50");
  f["instrument_name"] = "DOGE_USDC-PERPETUAL";
  s.push(f);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().unmapped == 1; }));
  EXPECT_EQ(r.sink.size(), 0u);
}

TEST(DeribitFillsClient, BadConfigGivesNull)
{
  auto c = config();
  c.ep.host = "www.deribit.com";   // production host in a test configuration
  EXPECT_EQ(create_DeribitFillsClient(c, nullptr), nullptr);
  c = config();
  c.ep.test = false;   // production without allow_production
  c.ep.host = "www.deribit.com";
  EXPECT_EQ(create_DeribitFillsClient(c, nullptr), nullptr);
  c = config();
  c.instruments.clear();
  EXPECT_EQ(create_DeribitFillsClient(c, nullptr), nullptr);
}

// ---- simulator -----------------------------------------------------------------------------------

TEST(SimFills, DroppedTradesComeBackThroughReconciliation)
{
  SimFills::Config sc;
  sc.fill_every_ms = 10;
  sc.drop_every = 3;
  Rig r(SimFills::make_factory(sc));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() >= 6; }));
  r.c->send(new msg::FillsReport(msg::FillsReport::RECONCILE, 0), nullptr);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes >= 2; }));
  r.settle();
  EXPECT_GE(r.c->stats().added_by_rest, 2u);
  EXPECT_EQ(r.c->ledger().live(), r.sink.size());
}
