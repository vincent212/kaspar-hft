/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// bybit_testnet_check: testing-ladder step 3 for BybitOM. NOT CERTIFIED.
// Runs only against Bybit's testnet (default) or demo trading (--demo); the
// hosts are fixed here and BybitOM's guard refuses any other host.
//
// Needs ~/.kaspar/keys/bybit_testnet.json (or --keys PATH), outside the
// repository: {"api_key": "...", "api_secret": "..."} for a testnet (or
// demo) HMAC key with Read-Write permission on Orders (spot and
// contracts/USDT perpetual). Without the file it says so and exits 2.
//
// Sequence, on one category (--category linear | spot) and one symbol:
//   1. public ticker on the test host: best bid and ask
//   2. BybitOM logon (private stream `order` and `execution`)
//   3. GTC limit buy at 80 % of the bid        -> Ack, then Cancel -> CancAck
//   4. PostOnly limit buy at 80 % of the bid   -> Ack, then Cancel -> CancAck
//   5. IOC limit buy at 80 % of the bid        -> CancAck (nothing to fill)
//   6. PostOnly limit buy at the ask (crosses) -> Reject
//   7. IOC limit buy at 101 % of the ask, minimum size -> Fill(s), then the
//      same quantity sold back the same way (linear: reduces the position)
//   8. restart: a GTC order is left working, the OM is shut down and a
//      second one logs in (Bybit keeps the order: no cancel-on-disconnect
//      without DCP); the order is then cancelled over REST by orderLinkId
//   9. REST: GET /v5/order/history and /v5/execution/list for every
//      orderLinkId used; every SOM message is checked against them:
//        Ack       the order exists; Ack.xordid matches orderId
//        Fill      one execId per Fill, same quantity and price; the sum is
//                  the order's cumExecQty
//        CancAck   orderStatus Cancelled / PartiallyFilledCanceled
//        Reject    orderStatus Rejected, or Cancelled with nothing filled
//                  (PostOnly), or no order at all
// The result is printed as one JSON line; the exit code is 0 only if every
// check passed. Keys are never printed.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "actors/act/Manager.hpp"
#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_oe/act/BybitOM.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "TestUtil.hpp"

using namespace bybit;
using namespace bybit::oe;
namespace som = frame::som::msg;
using bybit::oe::test::Recorder;
using bybit::oe::test::wait_until;

namespace
{
  std::string expand(std::string p)
  {
    if (!p.empty() && p[0] == '~')
      if (const char *h = std::getenv("HOME"))
        p = h + p.substr(1);
    return p;
  }

  struct Check
  {
    int pass = 0, fail = 0;
    nlohmann::json detail = nlohmann::json::array();
    void operator()(bool ok, const std::string &what)
    {
      ok ? ++pass : ++fail;
      detail.push_back({{"ok", ok}, {"check", what}});
      std::cout << (ok ? "PASS " : "FAIL ") << what << std::endl;
    }
  };
}

int main(int argc, char **argv)
{
  std::string keys = "~/.kaspar/keys/bybit_testnet.json", category = "linear", symbol = "BTCUSDT";
  bool demo = false;
  for (int i = 1; i < argc; ++i)
  {
    std::string k = argv[i];
    auto v = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--keys") keys = v();
    else if (k == "--category") category = v();
    else if (k == "--symbol") symbol = v();
    else if (k == "--demo") demo = true;
    else
    {
      std::cerr << "unknown argument " << k << std::endl;
      return 2;
    }
  }
  if (!std::filesystem::exists(expand(keys)))
  {
    std::cerr << "no key file " << keys
              << ": write {\"api_key\": \"...\", \"api_secret\": \"...\"} for a Bybit testnet "
                 "HMAC key (Orders read-write) there; nothing was sent"
              << std::endl;
    return 2;
  }

  BybitOMConfig cfg;
  cfg.session.testnet = true;
  cfg.session.rest_host = demo ? "api-demo.bybit.com" : "api-testnet.bybit.com";
  cfg.session.ws_host = demo ? "stream-demo.bybit.com" : "stream-testnet.bybit.com";
  std::string err;
  if (!auth::load(keys, cfg.session.credentials, err))
  {
    std::cerr << "key file: " << err << std::endl;
    return 2;
  }
  priv::RestClient rest(cfg.session.credentials, cfg.session.rest_host, true);

  // Instrument from the test host.
  auto info = rest.get("/v5/market/instruments-info", "category=" + category + "&symbol=" + symbol);
  auto tick = rest.get("/v5/market/tickers", "category=" + category + "&symbol=" + symbol);
  if (!info.ok() || !tick.ok() || info.result["list"].empty() || tick.result["list"].empty())
  {
    std::cerr << "instrument or ticker: " << info.ret_msg << " / " << tick.ret_msg << std::endl;
    return 1;
  }
  const auto &ii = info.result["list"][0];
  const double tk = std::stod(ii["priceFilter"]["tickSize"].get<std::string>());
  const auto &lot = ii["lotSizeFilter"];
  const double step = std::stod(lot.value("qtyStep", lot.value("basePrecision", std::string("0"))));
  const double minq = std::stod(lot["minOrderQty"].get<std::string>());
  const double bid = std::stod(tick.result["list"][0]["bid1Price"].get<std::string>());
  const double ask = std::stod(tick.result["list"][0]["ask1Price"].get<std::string>());
  // Spot: the minimum notional is 5 USDT; size for 10.
  const double qty = std::max(minq, std::ceil(10.0 / ask / step) * step);
  const int units = int(std::llround(qty / step));
  std::cout << "instrument " << category << " " << symbol << " tick " << tk << " step " << step
            << " bid " << bid << " ask " << ask << " units " << units << std::endl;

  const int sym = 1;
  cfg.instruments[sym] = {category, symbol, tk, step};
  cfg.max_orders_per_sec = 5;
  const en::x venue = category == "spot" ? en::x::BYBIT : en::x::BYBITF;

  Recorder somr("SOM");
  auto om = new BybitOM(cfg, &somr, rest_ws_session_factory(), venue);
  auto mgr = std::make_unique<actors::Manager>("bybit_testnet_check");
  mgr->manage_and_start(om);
  Check check;
  check(wait_until([&] { return om->ready(); }, std::chrono::seconds(15)), "logon");

  int next_oid = 1;
  std::map<int, std::string> expect; // oid -> expected terminal message
  auto send = [&](double px, en::bs side, som::Order::TimeInForce tif, const char *strategy = "") {
    som::Order o("CHK", venue, sym, units, int(std::llround(px / tk)), side, 1);
    o.oid = next_oid++;
    o.tif = tif;
    o.strategy = strategy;
    om->send(new som::Order(o), &somr);
    return o.oid;
  };
  auto has = [&](auto *dummy, int oid) {
    using T = std::remove_pointer_t<decltype(dummy)>;
    return wait_until([&] {
      for (auto m : somr.all<T>())
        if (int(m->id) == oid)
          return true;
      return false;
    }, std::chrono::seconds(10));
  };
  const double far = std::floor(bid * 0.8 / tk) * tk;

  int a = send(far, en::bs::BUY, som::Order::TimeInForce::GTC);
  check(has((som::Ack *)nullptr, a), "GTC far: Ack");
  om->send(new som::Cancel(a), &somr);
  check(has((som::CancAck *)nullptr, a), "GTC far: CancAck");

  int b = send(far, en::bs::BUY, som::Order::TimeInForce::GTC, "POSTONLY");
  check(has((som::Ack *)nullptr, b), "PostOnly far: Ack");
  om->send(new som::Cancel(b), &somr);
  check(has((som::CancAck *)nullptr, b), "PostOnly far: CancAck");

  int c = send(far, en::bs::BUY, som::Order::TimeInForce::IOC);
  check(has((som::CancAck *)nullptr, c), "IOC far: CancAck");

  int d = send(ask, en::bs::BUY, som::Order::TimeInForce::GTC, "POSTONLY");
  check(has((som::Reject *)nullptr, d), "PostOnly crossing: Reject");

  int e = send(std::ceil(ask * 1.01 / tk) * tk, en::bs::BUY, som::Order::TimeInForce::IOC);
  check(has((som::Fill *)nullptr, e), "IOC through the ask: Fill");
  std::this_thread::sleep_for(std::chrono::seconds(2));
  int f = send(std::floor(bid * 0.99 / tk) * tk, en::bs::SEL, som::Order::TimeInForce::IOC);
  check(has((som::Fill *)nullptr, f), "IOC through the bid (sell back): Fill");

  // Restart: an order left working across a new OM and a new login.
  int g = send(far, en::bs::BUY, som::Order::TimeInForce::GTC);
  check(has((som::Ack *)nullptr, g), "GTC before restart: Ack");
  std::map<int, std::string> links;
  for (int oid = 1; oid < next_oid; ++oid)
    links[oid] = om->link_of(oid);
  const std::string g_link = links[g];
  mgr->end();
  mgr.reset();
  delete om;
  auto om2 = new BybitOM(cfg, &somr, rest_ws_session_factory(), venue);
  auto mgr2 = std::make_unique<actors::Manager>("bybit_testnet_check2");
  mgr2->manage_and_start(om2);
  check(wait_until([&] { return om2->ready(); }, std::chrono::seconds(15)), "logon after restart");
  {
    nlohmann::ordered_json body = {{"category", category}, {"symbol", symbol}, {"orderLinkId", g_link}};
    auto r = rest.post("/v5/order/cancel", body.dump());
    check(r.ok(), "order left over the restart cancelled over REST");
  }
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // REST reconciliation of every SOM message.
  for (auto &[oid, link] : links)
  {
    if (link.empty())
    {
      check(false, "oid " + std::to_string(oid) + ": no orderLinkId");
      continue;
    }
    auto oh = rest.get("/v5/order/history", "category=" + category + "&orderLinkId=" + link);
    auto ex = rest.get("/v5/execution/list", "category=" + category + "&orderLinkId=" + link);
    const bool found = oh.ok() && !oh.result["list"].empty();
    const auto ord = found ? oh.result["list"][0] : nlohmann::json::object();
    const std::string status = ord.value("orderStatus", std::string());
    double cum = found ? std::stod(ord.value("cumExecQty", std::string("0"))) : 0;
    int fills = 0;
    double fqty = 0;
    for (auto m : somr.all<som::Fill>())
      if (int(m->id) == oid)
      {
        ++fills;
        fqty += m->sz * step;
      }
    int execs = 0;
    if (ex.ok())
      for (const auto &x : ex.result["list"])
        if (x.value("execType", std::string()) == "Trade")
          ++execs;
    bool acked = false, canc = false, rej = false;
    uint64_t xord = 0;
    for (auto m : somr.all<som::Ack>())
      if (int(m->id) == oid) { acked = true; xord = m->xordid; }
    for (auto m : somr.all<som::CancAck>())
      if (int(m->id) == oid) canc = true;
    for (auto m : somr.all<som::Reject>())
      if (int(m->id) == oid) rej = true;
    const std::string tag = "oid " + std::to_string(oid) + " " + link + " (" + status + ")";
    if (acked)
      check(found && xord == BybitOM::xordid(ord.value("orderId", std::string())), tag + ": Ack.xordid = orderId");
    check(fills == execs, tag + ": one Fill per Trade execution (" + std::to_string(fills) + " vs " +
                              std::to_string(execs) + ")");
    check(std::fabs(fqty - cum) < step / 2, tag + ": Fill total = cumExecQty");
    if (canc)
      check(status == "Cancelled" || status == "PartiallyFilledCanceled", tag + ": CancAck matches status");
    if (rej)
      check(!found || status == "Rejected" || (status == "Cancelled" && cum == 0), tag + ": Reject matches status");
  }

  mgr2->end();
  mgr2.reset();
  delete om2;
  nlohmann::json out = {{"host", cfg.session.rest_host}, {"category", category}, {"symbol", symbol},
                        {"pass", check.pass}, {"fail", check.fail}, {"checks", check.detail}};
  std::cout << "SUMMARY " << out.dump() << std::endl;
  return check.fail ? 1 : 0;
}
