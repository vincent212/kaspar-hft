/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OKX fills to TradeCapture (from the V5 documentation's examples), and
// OkxFillsClient against a fake session and against SimFills.

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "okx_pt/SimFills.hpp"
#include "okx_pt/if/OkxFillsClient.hpp"

using namespace okx::pt;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace
{
  // "GET / Transaction details (last 3 days)" response example, data[0].
  const json kRestExample = json::parse(R"({
    "side": "buy", "fillSz": "0.00192834", "fillPx": "51858", "fillPxVol": "", "fillFwdPx": "",
    "fee": "-0.00000192834", "fillPnl": "0", "ordId": "680800019749904384", "feeRate": "-0.001",
    "instType": "SPOT", "fillPxUsd": "", "instId": "BTC-USDT", "clOrdId": "", "posSide": "net",
    "billId": "680800019754098688", "subType": "1", "fillMarkVol": "", "tag": "", "fillTime": "1708587373361",
    "execType": "T", "fillIdxPx": "", "tradeId": "744876980", "fillMarkPx": "", "feeCcy": "BTC",
    "ts": "1708587373362", "tradeQuoteCcy": "USDT"})");

  // "WS / Order channel" push example, data[0] (abridged to the fields used).
  const json kWsExample = json::parse(R"({
    "accFillSz": "0.001", "avgPx": "31527.1", "cancelSource": "", "clOrdId": "", "execType": "M",
    "fillPx": "31527.1", "fillSz": "0.001", "fillTime": "1654084353263", "instId": "BTC-USDT",
    "instType": "SPOT", "ordId": "452197707845865472", "ordType": "limit", "px": "31527.1", "side": "sell",
    "state": "filled", "sz": "0.001", "tdMode": "cash", "tradeId": "242589207", "uTime": "1654084353264"})");

  FillsConfig config()
  {
    FillsConfig c;
    c.ep.demo = true;
    c.ep.ws_host = "wspap.okx.com";
    c.reconcile_every_s = 0;
    c.reconnect_ms = 50;
    PtInstrument s{"BTC-USDT", "", "0.00000001", en::x::OKX, -1};
    PtInstrument w{"BTC-USDT-SWAP", "", "0.01", en::x::OKXF, -1};
    c.instruments = {s, w};
    return c;
  }

  json ws_fill(const std::string &trade, const std::string &sz = "0.001", uint64_t ms = 0)
  {
    json f = kWsExample;
    f["tradeId"] = trade;
    f["fillSz"] = sz;
    if (ms)
      f["fillTime"] = std::to_string(ms);
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
      CallStatus reconcile(uint64_t) override
      {
        std::lock_guard<std::mutex> l(s.mu);
        ++s.reconciles;
        if (s.fail_rest)
        {
          s.ev->reconciled(0, false, "HTTP 401 code 50113");
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
    OkxFillsClient *c = nullptr;
    Rig(FillsSessionFactory f, FillsConfig cfg = config())
    {
      c = new OkxFillsClient(cfg, &sink, std::move(f));
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

// ---- mapping ------------------------------------------------------------------------------------

TEST(OkxFillsMapping, RestExampleMapsExactly)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(kRestExample, config(), true, t, err)) << err;
  EXPECT_EQ(t.venue, en::x::OKX);
  EXPECT_EQ(t.trade_id, "BTC-USDT:744876980");
  EXPECT_EQ(t.report_id, t.trade_id);
  EXPECT_EQ(t.event, post_trade::Event::New);
  EXPECT_EQ(t.venue_order_id, "680800019749904384");
  EXPECT_EQ(t.side, post_trade::Side::Buy);
  EXPECT_EQ(t.qty, 192834); // satoshis: lotSz 0.00000001
  EXPECT_EQ(t.price_mantissa, 51858);
  EXPECT_EQ(t.price_decimals, 0);
  EXPECT_EQ(t.trade_date, 20240222u); // 1708587373361 ms = 2024-02-22T07:36:13.361Z
  EXPECT_EQ(t.trade_time_ns, (7 * 3600 + 36 * 60 + 13) * 1000000000ll + 361000000ll);
  EXPECT_TRUE(t.poss_dup);
}

TEST(OkxFillsMapping, OrdersChannelExampleMapsExactly)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(kWsExample, config(), false, t, err)) << err;
  EXPECT_EQ(t.trade_id, "BTC-USDT:242589207");
  EXPECT_EQ(t.side, post_trade::Side::Sell);
  EXPECT_EQ(t.qty, 100000);
  EXPECT_EQ(t.price_mantissa, 315271);
  EXPECT_EQ(t.price_decimals, 1);
  EXPECT_DOUBLE_EQ(t.price(), 31527.1);
  EXPECT_FALSE(t.poss_dup);
}

TEST(OkxFillsMapping, SwapSizesAreContractsInLots)
{
  json f = kWsExample;
  f["instId"] = "BTC-USDT-SWAP";
  f["fillSz"] = "1.37";
  post_trade::msg::TradeCapture t;
  std::string err;
  ASSERT_TRUE(to_trade_capture(f, config(), false, t, err)) << err;
  EXPECT_EQ(t.venue, en::x::OKXF);
  EXPECT_EQ(t.qty, 137);
}

TEST(OkxFillsMapping, NonFillsAndUnmappableFillsAreRefused)
{
  post_trade::msg::TradeCapture t;
  std::string err;
  json f = kWsExample;
  f["fillSz"] = "0";
  EXPECT_FALSE(to_trade_capture(f, config(), false, t, err));
  EXPECT_EQ(err, "not a fill");
  f = kWsExample;
  f["instId"] = "ETH-USDT";
  EXPECT_FALSE(to_trade_capture(f, config(), false, t, err));
  f = kWsExample;
  f["instId"] = "BTC-USDT-SWAP";
  f["fillSz"] = "0.005"; // half a lot
  EXPECT_FALSE(to_trade_capture(f, config(), false, t, err));
  EXPECT_NE(err.find("whole number"), std::string::npos);
  f = kWsExample;
  f["tradeId"] = "-12"; // a liquidation fill: still a fill
  EXPECT_TRUE(to_trade_capture(f, config(), false, t, err)) << err;
}

TEST(OkxFillsMapping, ParseDecimal)
{
  int64_t m;
  int d;
  ASSERT_TRUE(parse_decimal("0.00192834", m, d));
  EXPECT_EQ(m, 192834);
  EXPECT_EQ(d, 8);
  ASSERT_TRUE(parse_decimal("-1.5", m, d));
  EXPECT_EQ(m, -15);
  EXPECT_FALSE(parse_decimal("1e5", m, d));
  EXPECT_FALSE(parse_decimal("", m, d));
  EXPECT_FALSE(parse_decimal("1.2.3", m, d));
  // post_trade/Decimal.hpp, shared by the four venues: the length limit is
  // int64_t, not 18 digits.
  ASSERT_TRUE(parse_decimal("1234567890123456789", m, d)); // 19 digits
  EXPECT_EQ(m, 1234567890123456789LL);
  EXPECT_EQ(d, 0);
  ASSERT_TRUE(parse_decimal("9223372036854775807", m, d));
  EXPECT_EQ(m, INT64_MAX);
  EXPECT_FALSE(parse_decimal("9223372036854775808", m, d));
  EXPECT_FALSE(parse_decimal("12345678901234567890", m, d));
  EXPECT_FALSE(parse_decimal("-", m, d));
}

// ---- client ------------------------------------------------------------------------------------

TEST(OkxFillsClient, WebSocketFillsReachTheSubscriberOnce)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  s.push(ws_fill("1"));
  s.push(ws_fill("1")); // the same fill again (a resend)
  s.push(ws_fill("2", "0.002"));
  json not_fill = kWsExample;
  not_fill["fillSz"] = "0";
  not_fill["tradeId"] = "";
  s.push(not_fill); // a state change without a fill
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 2; }));
  r.settle();
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.sink.at(1).qty, 200000);
  EXPECT_EQ(r.c->stats().duplicates, 1u);
  EXPECT_EQ(r.c->ledger().live(), 2u);
}

TEST(OkxFillsClient, ReconciliationAddsMissedFillsOnceAndMatchesTheRest)
{
  FakeServer s;
  const uint64_t t0 = now_ms() - 60000;
  s.rest_rows = {ws_fill("10", "0.001", t0), ws_fill("11", "0.001", t0 + 1000)};
  Rig r(s.factory());
  // At logon: both REST rows are new (missed while down) and reach the sink.
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 1; }));
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.c->stats().added_by_rest, 2u);
  // Live fills, then a pass that returns all of them: nothing is new.
  s.push(ws_fill("12", "0.001", t0 + 2000));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 3; }));
  s.rest_rows.push_back(ws_fill("12", "0.001", t0 + 2000));
  r.c->send(new msg::FillsReport(msg::FillsReport::RECONCILE, 0), nullptr);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 2; }));
  r.settle();
  EXPECT_EQ(r.sink.size(), 3u);
  EXPECT_EQ(r.c->stats().added_by_rest, 2u);
  EXPECT_EQ(r.c->stats().missing_from_rest, 0u);
}

TEST(OkxFillsClient, AWebSocketFillMissingFromRestIsCounted)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 1; }));
  s.push(ws_fill("20", "0.001", now_ms() - 30000)); // 30 s old: REST should have it
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
  r.c->send(new msg::FillsReport(msg::FillsReport::RECONCILE, 0), nullptr);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 2; }));
  EXPECT_EQ(r.c->stats().missing_from_rest, 1u);
}

TEST(OkxFillsClient, ReconnectReconcilesAndDoesNotDuplicate)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes == 1; }));
  s.push(ws_fill("30"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
  s.drop();
  s.rest_rows = {ws_fill("30"), ws_fill("31")}; // 31 happened while down
  ASSERT_TRUE(wait_until([&] { return s.sessions == 2 && r.c->stats().passes == 2; }));
  r.settle();
  EXPECT_EQ(r.sink.size(), 2u);
  EXPECT_EQ(r.sink.at(1).trade_id, "BTC-USDT:31");
}

TEST(OkxFillsClient, FailedReconciliationIsCountedNotFatal)
{
  FakeServer s;
  s.fail_rest = true;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->stats().failed_passes == 1; }));
  s.push(ws_fill("40"));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() == 1; }));
}

TEST(OkxFillsClient, UnmappableFillIsCounted)
{
  FakeServer s;
  Rig r(s.factory());
  ASSERT_TRUE(wait_until([&] { return r.c->ready(); }));
  json f = ws_fill("50");
  f["instId"] = "DOGE-USDT";
  s.push(f);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().unmapped == 1; }));
  EXPECT_EQ(r.sink.size(), 0u);
}

TEST(OkxFillsClient, BadConfigGivesNull)
{
  auto c = config();
  c.ep.ws_host = "ws.okx.com"; // production host in demo
  EXPECT_EQ(create_OkxFillsClient(c, nullptr), nullptr);
  c = config();
  c.reconcile_lookback_s = 4 * 86400;
  EXPECT_EQ(create_OkxFillsClient(c, nullptr), nullptr);
  c = config();
  c.channel = "trades";
  EXPECT_EQ(create_OkxFillsClient(c, nullptr), nullptr);
}

// ---- simulator ---------------------------------------------------------------------------------

TEST(SimFills, DroppedFillsComeBackThroughReconciliation)
{
  SimFills::Config sc;
  sc.fill_every_ms = 10;
  sc.drop_every = 3;
  Rig r(SimFills::make_factory(sc));
  ASSERT_TRUE(wait_until([&] { return r.sink.size() >= 6; }));
  r.c->send(new msg::FillsReport(msg::FillsReport::RECONCILE, 0), nullptr);
  ASSERT_TRUE(wait_until([&] { return r.c->stats().passes >= 2; }));
  r.settle();
  // Every fill made is in the ledger exactly once.
  EXPECT_GE(r.c->stats().added_by_rest, 2u);
  EXPECT_EQ(r.c->ledger().live(), r.sink.size());
  EXPECT_EQ(r.c->ledger().net_qty("BTC-USDT") % 10000, 0);
}
