/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// kraken_demo_check -- step 3 of the testing ladder (crypto.md) on the Kraken
// Futures DEMO environment only. NEVER RUN by its author: no demo keys.
//
//   kraken_demo_check --config ../order_entry/kraken/config/kraken_futures_demo_example.ini
//                     [--fills-config ../../post_trade/kraken/config/kraken_fills_futures_demo_example.ini]
//
// Refuses to start unless the configuration is product = futures,
// testnet = true, rest_host = demo-futures.kraken.com, and the key file exists.
// Then, with KrakenOM and KrakenFillsClient running as in kaspr:
//   1. logon (both)
//   2. a limit buy of one unit 20% below the bid: Ack expected; then its cancel:
//      CancAck expected
//   3. an IOC buy of one unit 1% through the ask: Fill(s) expected
//   4. an IOC sell of one unit 1% through the bid (flattens): Fill(s) expected
//   5. the OM is destroyed and recreated (a reconnect with new client ids)
//   6. GET /fills (signed): for each order of steps 3-4, the venue's fill sizes
//      and fill_ids against the SOM Fill messages and against the TradeCaptures
//      the fills client emitted; every fill_id must reach TradeCapture once
// The report is printed as JSON; exit status 0 only if every check passed.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_oe/KrakenSessions.hpp"
#include "kraken_pt/if/KrakenFillsClient.hpp"

#include "TestUtil.hpp"

using json = nlohmann::json;
using namespace kraken::oe;
using namespace kraken::oe::test;
namespace som = frame::som::msg;

namespace
{
  struct Sink : actors::Actor
  {
    std::mutex mu;
    std::vector<post_trade::msg::TradeCapture> got;
    Sink() { strncpy(name, "tc_sink", sizeof(name) - 1); }
    const char *get_name() const override { return name; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      if (auto t = dynamic_cast<const post_trade::msg::TradeCapture *>(m))
      {
        std::lock_guard<std::mutex> l(mu);
        got.push_back(*t);
      }
      delete m;
    }
  };

  json public_get(const std::string &host, const std::string &path)
  {
    auto r = kraken::https_request("GET", host, path);
    if (r.status != 200)
      throw std::runtime_error(path + ": HTTP " + std::to_string(r.status));
    return kraken::parse_raw(r.body);
  }

  double num(const json &j, const char *k) { return std::stod(kraken::num_text(j.at(k))); }
}

int main(int argc, char **argv)
{
  std::string cfg_path, fills_path;
  for (int i = 1; i + 1 < argc; i += 2)
  {
    std::string k = argv[i];
    if (k == "--config")
      cfg_path = argv[i + 1];
    else if (k == "--fills-config")
      fills_path = argv[i + 1];
  }
  if (cfg_path.empty())
  {
    std::cerr << "usage: kraken_demo_check --config FILE [--fills-config FILE]\n";
    return 2;
  }
  Settings s;
  std::string err;
  if (!load_settings(cfg_path, s, err))
  {
    std::cerr << "config: " << err << std::endl;
    return 2;
  }
  if (s.sim || s.ep.product != Product::Futures || !s.ep.testnet || s.ep.rest_host != kFuturesDemoHost)
  {
    std::cerr << "refused: this check runs only with product = futures, testnet = true, rest_host = "
              << kFuturesDemoHost << std::endl;
    return 2;
  }
  if (!std::filesystem::exists(s.key_file))
  {
    std::cerr << "no key file " << s.key_file << ": step 3 not run (README.md says what it must contain)"
              << std::endl;
    return 3;
  }
  Keys keys;
  if (!load_keys(s.key_file, keys, err))
  {
    std::cerr << err << std::endl;
    return 2;
  }
  const std::string venue_sym = s.om.symbols.begin()->second;
  const std::string asset_name = s.om.symbols.begin()->first;

  json report = {{"environment", s.ep.rest_host}, {"symbol", venue_sym}};
  bool ok = true;
  auto check = [&](const std::string &what, bool pass) {
    report["checks"][what] = pass;
    ok = ok && pass;
    std::cout << (pass ? "PASS " : "FAIL ") << what << std::endl;
  };

  // Instrument and market from the demo's public endpoints.
  double tick = 0, bid = 0, ask = 0;
  for (const auto &i : public_get(s.ep.rest_host, "/derivatives/api/v3/instruments").at("instruments"))
    if (i.value("symbol", std::string()) == venue_sym)
      tick = num(i, "tickSize");
  auto t = public_get(s.ep.rest_host, "/derivatives/api/v3/tickers/" + venue_sym).at("ticker");
  bid = num(t, "bid");
  ask = num(t, "ask");
  report["tick"] = tick;
  report["bid"] = bid;
  report["ask"] = ask;
  if (!(tick > 0 && bid > 0 && ask > 0))
  {
    std::cerr << "no tick or market for " << venue_sym << std::endl;
    return 1;
  }

  frame::ref::RefData::set_universe("build/kraken_demo_universe_seed.csv", "");
  frame::ref::RefData::inst();
  auto *asset = frame::ref::RefData::add_future_asset(asset_name, en::x::KRAKENF, 990001, "", venue_sym, tick);
  const uint sym = uint(asset->id);

  Recorder somr{"SOM"};
  Sink tc;
  const auto since = sim::wsnet::now_ns();
  auto factory = futures_session_factory(s, real_http(), since);
  actors::Manager mgr("demo");
  auto *om = new KrakenOM(s.om, &somr, factory, en::x::KRAKENF);
  mgr.manage_and_start(om);
  kraken::pt::FillsConfig fc;
  if (!fills_path.empty() && !kraken::pt::FillsConfig::from_file(fills_path, fc, err))
  {
    std::cerr << "fills config: " << err << std::endl;
    return 2;
  }
  if (fills_path.empty())
  {
    fc.ep = s.ep;
    fc.key_file = s.key_file;
    kraken::pt::PtInstrument pi;
    pi.symbol = venue_sym;
    pi.asset = asset_name;
    pi.lot = KrakenOM::decimal(1, s.om.qty_unit);
    pi.venue = en::x::KRAKENF;
    pi.sym = int(sym);
    fc.instruments = {pi};
  }
  auto *fills = new kraken::pt::KrakenFillsClient(fc, &tc, kraken::pt::futures_fills_factory(fc, real_http(), since));
  mgr.manage_and_start(fills);
  check("om logon", wait_until([&] { return om->ready(); }, std::chrono::seconds(15)));
  check("fills logon", wait_until([&] { return fills->ready(); }, std::chrono::seconds(15)));

  auto order = [&](int oid, en::bs side, double px, som::Order::TimeInForce tif) {
    som::Order o("KR", en::x::KRAKENF, sym, 1, int(std::llround(px / tick)), side, 4);
    o.oid = oid;
    o.tif = tif;
    om->send(new som::Order(o), &somr);
  };

  // 2. far from the market, then cancel
  order(1, en::bs::BUY, bid * 0.8, som::Order::TimeInForce::GTC);
  check("far order acked", somr.wait_for<som::Ack>(0, std::chrono::seconds(15)) != nullptr);
  om->send(new som::Cancel(1), &somr);
  check("far order cancelled", somr.wait_for<som::CancAck>(0, std::chrono::seconds(15)) != nullptr);

  // 3-4. a fill and its flattening
  order(2, en::bs::BUY, ask * 1.01, som::Order::TimeInForce::IOC);
  check("buy filled", wait_until([&] {
          int n = 0;
          for (auto f : somr.all<som::Fill>())
            if (f->id == 2)
              n += int(f->sz);
          return n == 1;
        }, std::chrono::seconds(20)));
  order(3, en::bs::SEL, bid * 0.99, som::Order::TimeInForce::IOC);
  check("sell filled", wait_until([&] {
          int n = 0;
          for (auto f : somr.all<som::Fill>())
            if (f->id == 3)
              n += int(f->sz);
          return n == 1;
        }, std::chrono::seconds(20)));

  // 5. reconnect: the OM is shut down and a new one (new session, new client
  // ids) starts on the same factory
  const std::string cl2 = om->client_order_id(2), cl3 = om->client_order_id(3);
  mgr.unmanage(om);
  auto *om2 = new KrakenOM(s.om, &somr, factory, en::x::KRAKENF);
  mgr.manage_and_start(om2);
  check("second om logon", wait_until([&] { return om2->ready(); }, std::chrono::seconds(15)));

  // 6. the venue's fills against SOM and TradeCapture
  std::this_thread::sleep_for(std::chrono::seconds(5));
  const std::string nonce = std::to_string(next_nonce());
  auto r = kraken::https_request("GET", s.ep.rest_host, "/derivatives/api/v3/fills",
                                 futures_headers(keys.api_key, keys.api_secret, "GET", "/api/v3/fills", "", nonce));
  auto venue_fills = kraken::parse_raw(r.body).value("fills", json::array());
  std::map<std::string, int> venue_units; // cliOrdId -> units
  std::set<std::string> venue_ids;
  for (const auto &f : venue_fills)
  {
    if (kraken::parse_rfc3339(f.value("fillTime", std::string())) < since)
      continue;
    venue_ids.insert(f.value("fill_id", std::string()));
    int64_t q = 0;
    kraken::to_fixed(kraken::num_text(f.at("size")), q);
    venue_units[f.value("cliOrdId", std::string())] +=
        int(std::llround(double(q) / (s.om.qty_unit * double(kraken::kOne))));
  }
  std::map<int, int> som_units;
  for (auto f : somr.all<som::Fill>())
    som_units[int(f->id)] += int(f->sz);
  check("venue fill units = SOM fill units (buy)", venue_units[cl2] == som_units[2]);
  check("venue fill units = SOM fill units (sell)", venue_units[cl3] == som_units[3]);
  std::set<std::string> tc_ids;
  size_t tc_dups = 0;
  {
    std::lock_guard<std::mutex> l(tc.mu);
    for (auto &x : tc.got)
      if (!tc_ids.insert(x.trade_id).second)
        ++tc_dups;
  }
  check("every venue fill_id reached TradeCapture", std::includes(tc_ids.begin(), tc_ids.end(), venue_ids.begin(),
                                                                  venue_ids.end()));
  check("no TradeCapture duplicates", tc_dups == 0);
  report["venue_fills"] = venue_ids.size();
  report["trade_captures"] = tc_ids.size();
  report["som_replies"] = somr.replies();
  report["fills_stats"] = {{"stream", fills->stats().stream_fills}, {"rest_rows", fills->stats().rest_rows},
                           {"added_by_rest", fills->stats().added_by_rest},
                           {"missing_from_rest", fills->stats().missing_from_rest}};
  std::cout << report.dump(2) << std::endl;
  mgr.end();
  std::_Exit(ok ? 0 : 1);
}
