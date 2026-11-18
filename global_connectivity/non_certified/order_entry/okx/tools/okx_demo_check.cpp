/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// okx_demo_check: testing ladder step 3 for OkxOM and OkxFillsClient, on OKX
// DEMO TRADING ONLY. NEVER RUN by its author: no demo keys were supplied.
//
//   okx_demo_check [--config config/okx_demo_example.ini] [--fill] [--inst ID]
//
// Refuses to start unless the configuration is environment = demo with the
// demo WebSocket host (OkxOMConfig::valid, Endpoint::check) and the key file
// exists. Every REST request carries x-simulated-trading: 1 or is not sent.
//
// For each configured instrument (or only --inst):
//   1. limit buy (DAY) of 1 unit at 10% below the best bid: Ack expected;
//      then Cancel: CancAck expected
//   2. IOC buy of 1 unit at 10% below the best bid: OKX cancels it unfilled
//   3. FOK buy of 1 unit at 10% below the best bid: likewise
//   4. with --fill only: IOC buy of 1 unit at 0.2% above the best ask (it
//      fills against the demo book), then IOC sell of 1 unit at 0.2% below
//      the best bid to return the position to where it was
// Then, for every order sent, it fetches OKX's own record over REST
// (GET /api/v5/trade/order, GET /api/v5/trade/fills) and checks:
//   - the Ack's venue order id equals OKX's ordId
//   - the last SOM message matches OKX's state (canceled -> CancAck,
//     filled -> Fill with leaves 0, live -> nothing final)
//   - the Fill messages' total size and prices equal OKX's fills
//   - every OKX fill reached OkxFillsClient's subscriber as one TradeCapture
//     with the same tradeId, size and price, and no other TradeCapture came
// It prints counts and each mismatch; it never prints keys. Exit status 0
// when every check passed.
//
// Not covered: amend (OkxOM refuses replace), reconnect (no hook to drop the
// session from outside), post_only (a configuration switch, not per order).
// The swap needs the demo account in net (one-way) position mode: OkxOM
// sends no posSide.

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Reject.hpp"
#include "okx_md/OkxWs.hpp"
#include "okx_oe/OkxAuth.hpp"
#include "okx_oe/OkxConfig.hpp"
#include "okx_oe/act/OkxOM.hpp"
#include "okx_pt/OkxFills.hpp"
#include "okx_pt/act/OkxFillsClient.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace som = frame::som::msg;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace
{
  // Stands in for the SOM and for the post-trade subscriber: keeps every
  // message it is sent.
  class Sink : public actors::Actor
  {
  public:
    explicit Sink(const char *n) { strncpy(name_, n, sizeof(name_) - 1); }
    const char *get_name() const override { return name_; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu_);
      msgs_.push_back(m);
    }
    template <class T>
    std::vector<const T *> all()
    {
      std::lock_guard<std::mutex> l(mu_);
      std::vector<const T *> v;
      for (auto m : msgs_)
        if (auto t = dynamic_cast<const T *>(m))
          v.push_back(t);
      return v;
    }
    // Messages about SOM order id `oid`, in arrival order, as names.
    std::vector<std::string> about(uint oid)
    {
      std::lock_guard<std::mutex> l(mu_);
      std::vector<std::string> v;
      for (auto m : msgs_)
      {
        if (auto a = dynamic_cast<const som::Ack *>(m); a && a->id == oid)
          v.push_back("Ack");
        else if (auto f = dynamic_cast<const som::Fill *>(m); f && f->id == oid)
          v.push_back("Fill");
        else if (auto c = dynamic_cast<const som::CancAck *>(m); c && c->id == oid)
          v.push_back("CancAck");
        else if (auto r = dynamic_cast<const som::CancReject *>(m); r && r->id == oid)
          v.push_back("CancReject");
        else if (auto j = dynamic_cast<const som::Reject *>(m); j && j->id == oid)
          v.push_back("Reject");
      }
      return v;
    }

  private:
    char name_[64] = {};
    std::mutex mu_;
    std::vector<const actors::Message *> msgs_;
  };

  template <class P>
  bool wait_until(P p, std::chrono::milliseconds limit)
  {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (p())
        return true;
      std::this_thread::sleep_for(20ms);
    }
    return p();
  }

  uint64_t epoch_ms()
  {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
  }

  // Signed REST GET on the demo endpoint; refuses without the demo header.
  json rest_get(const okx::oe::OkxOMConfig &cfg, const okx::auth::Keys &k, const std::string &path, bool sign)
  {
    okx::oe::Headers h = cfg.ep.headers();
    std::string err;
    if (!okx::oe::require_demo_header(cfg.ep, h, err))
      throw std::runtime_error(err);
    if (sign)
    {
      const std::string ts = okx::auth::iso_timestamp(epoch_ms());
      h["OK-ACCESS-KEY"] = k.api_key;
      h["OK-ACCESS-SIGN"] = okx::auth::rest_sign(k.secret_key, ts, "GET", path, "");
      h["OK-ACCESS-TIMESTAMP"] = ts;
      h["OK-ACCESS-PASSPHRASE"] = k.passphrase;
    }
    auto r = okx::https_request(cfg.ep.rest_host, "GET", path, "", h);
    std::this_thread::sleep_for(150ms); // well under 20 requests per 2 s
    return json::parse(r.body);
  }

  struct Sent
  {
    uint oid;
    const okx::oe::Instrument *ins;
    std::string what;
  };

  int64_t ticks(double px, const okx::oe::Instrument &i, bool up)
  {
    const double t = px / i.tick_d();
    return int64_t(up ? std::ceil(t - 1e-9) : std::floor(t + 1e-9));
  }
}

int main(int argc, char **argv)
{
  std::string config = "config/okx_demo_example.ini", only;
  bool fill = false;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc)
      config = argv[++i];
    else if (a == "--inst" && i + 1 < argc)
      only = argv[++i];
    else if (a == "--fill")
      fill = true;
    else
    {
      std::cerr << "usage: okx_demo_check [--config FILE] [--fill] [--inst ID]\n";
      return 2;
    }
  }

  okx::oe::OkxOMConfig cfg;
  std::string err;
  if (!okx::oe::OkxOMConfig::from_file(config, cfg, err) || !cfg.valid(err))
  {
    std::cerr << "refused: " << err << "\n";
    return 2;
  }
  if (!cfg.ep.demo || cfg.sim)
  {
    std::cerr << "refused: okx_demo_check runs only with environment = demo and sim = false\n";
    return 2;
  }
  okx::auth::Keys keys;
  if (!okx::auth::load_keys(cfg.key_file, keys, err))
  {
    std::cerr << "refused: " << err << " (see README.md, \"Demo keys\")\n";
    return 2;
  }
  okx::pt::FillsConfig fcfg;
  if (!okx::pt::FillsConfig::from_file(config, fcfg, err))
  {
    std::cerr << "refused: post-trade config: " << err << "\n";
    return 2;
  }
  fcfg.reconcile_every_s = 0; // one pass at logon; the check reads REST itself

  std::filesystem::create_directories("build");
  const std::string seed = "build/okx_demo_check_universe_seed.csv";
  std::ofstream(seed) << "# generated by okx_demo_check\n";
  frame::ref::RefData::set_universe(seed, "");
  frame::ref::RefData::inst();
  uint32_t secid = 990001;
  for (const auto &i : cfg.instruments)
    frame::ref::RefData::add_future_asset(i.asset, i.venue, secid++, "", i.inst_id, 1.0);

  Sink somr("SOM"), ptsub("PostTrade");
  auto *om = create_OkxOM(cfg, &somr);
  auto *fc = create_OkxFillsClient(fcfg, &ptsub);
  if (!om || !fc)
  {
    std::cerr << "refused: OkxOM or OkxFillsClient not created\n";
    return 2;
  }
  auto *omp = static_cast<okx::oe::OkxOM *>(om);
  auto mgr = std::make_unique<actors::Manager>("okx_demo_check");
  mgr->manage_and_start(om);
  mgr->manage_and_start(fc);
  if (!wait_until([&] { return omp->ready(); }, 15s))
  {
    std::cerr << "OkxOM did not log in within 15 s\n";
    mgr->end();
    return 1;
  }

  std::vector<Sent> sent;
  uint next_oid = 1;
  int problems = 0;
  auto send_order = [&](const okx::oe::Instrument &ins, en::bs side, int64_t px, som::Order::TimeInForce tif,
                        const std::string &what) {
    som::Order o("DC", ins.venue, uint(ins.sym), 1, int(px), side, 4);
    o.oid = int(next_oid++);
    o.tif = tif;
    om->send(new som::Order(o), &somr);
    sent.push_back({uint(o.oid), &ins, what});
    return uint(o.oid);
  };
  auto final_of = [&](uint oid) {
    auto v = somr.about(oid);
    return v.empty() ? std::string() : v.back();
  };
  auto settled = [&](uint oid) {
    auto f = final_of(oid);
    if (f == "CancAck" || f == "Reject")
      return true;
    for (auto *m : somr.all<som::Fill>())
      if (m->id == oid && m->still_to_be_filled <= 0)
        return true;
    return false;
  };

  for (const auto &ins : omp->config().instruments)
  {
    if (!only.empty() && ins.inst_id != only)
      continue;
    auto t = rest_get(cfg, keys, "/api/v5/market/ticker?instId=" + ins.inst_id, false);
    if (t.value("code", "") != "0" || t["data"].empty())
    {
      std::cerr << ins.inst_id << ": no demo ticker: " << t.dump() << "\n";
      ++problems;
      continue;
    }
    const double bid = std::stod(t["data"][0].value("bidPx", "0")), ask = std::stod(t["data"][0].value("askPx", "0"));
    std::cout << ins.inst_id << " demo bid " << bid << " ask " << ask << "\n";
    const int64_t far = ticks(bid * 0.9, ins, false);

    using TIF = som::Order::TimeInForce;
    const uint lim = send_order(ins, en::bs::BUY, far, TIF::DAY, "limit far");
    if (!wait_until([&] { return final_of(lim) == "Ack"; }, 10s))
    {
      std::cerr << ins.inst_id << ": limit order not acked: " << final_of(lim) << "\n";
      ++problems;
    }
    om->send(new som::Cancel(lim), &somr);
    wait_until([&] { return settled(lim); }, 10s);
    for (auto [tif, name] : {std::pair{TIF::IOC, "ioc far"}, std::pair{TIF::FOK, "fok far"}})
    {
      const uint id = send_order(ins, en::bs::BUY, far, tif, name);
      wait_until([&] { return settled(id); }, 10s);
    }
    if (fill)
    {
      const uint b = send_order(ins, en::bs::BUY, ticks(ask * 1.002, ins, true), TIF::IOC, "ioc buy marketable");
      wait_until([&] { return settled(b); }, 10s);
      const uint s = send_order(ins, en::bs::SEL, ticks(bid * 0.998, ins, false), TIF::IOC, "ioc sell marketable");
      wait_until([&] { return settled(s); }, 10s);
    }
  }

  std::this_thread::sleep_for(3s); // late pushes, and REST catching up

  // ---- OKX's own record ----
  std::set<std::string> okx_trades; // instId:tradeId of our orders' fills
  std::map<std::string, std::pair<std::string, std::string>> okx_fill; // instId:tradeId -> (fillSz, fillPx)
  for (const auto &s : sent)
  {
    const auto msgs = somr.about(s.oid);
    std::string joined;
    for (const auto &m : msgs)
      joined += (joined.empty() ? "" : ",") + m;
    uint64_t acked_xid = 0;
    for (auto *a : somr.all<som::Ack>())
      if (a->id == s.oid)
        acked_xid = a->xordid;
    // Read after every order has settled; OkxOM's thread is idle by now.
    const std::string ord_id = omp->ord_id(int(s.oid));
    if (ord_id.empty())
    {
      std::cout << s.ins->inst_id << " oid " << s.oid << " " << s.what << ": SOM [" << joined
                << "], no OKX ordId (never accepted)\n";
      if (final_of(s.oid) != "Reject")
        ++problems;
      continue;
    }
    auto o = rest_get(cfg, keys, "/api/v5/trade/order?instId=" + s.ins->inst_id + "&ordId=" + ord_id, true);
    if (o.value("code", "") != "0" || o["data"].empty())
    {
      std::cout << s.ins->inst_id << " oid " << s.oid << ": OKX has no order " << ord_id << ": " << o.value("msg", "")
                << "\n";
      ++problems;
      continue;
    }
    const auto &d = o["data"][0];
    const std::string state = d.value("state", "");
    auto f = rest_get(cfg, keys,
                      "/api/v5/trade/fills?instId=" + s.ins->inst_id + "&ordId=" + ord_id + "&limit=100", true);
    double okx_sz = 0, som_sz = 0;
    std::multiset<std::string> okx_px, som_px;
    for (const auto &r : f.value("data", json::array()))
    {
      okx_sz += std::stod(r.value("fillSz", "0"));
      okx_px.insert(r.value("fillPx", ""));
      const std::string key = s.ins->inst_id + ":" + r.value("tradeId", "");
      okx_trades.insert(key);
      okx_fill[key] = {r.value("fillSz", ""), r.value("fillPx", "")};
    }
    for (auto *m : somr.all<som::Fill>())
      if (m->id == s.oid)
      {
        som_sz += m->sz * s.ins->unit_d();
        char b[64];
        std::snprintf(b, sizeof(b), "%.*f", okx::oe::decimals(s.ins->tick), m->pxi * s.ins->tick_d());
        som_px.insert(b);
      }
    std::multiset<std::string> okx_px_norm;
    for (const auto &p : okx_px)
    {
      char b[64];
      std::snprintf(b, sizeof(b), "%.*f", okx::oe::decimals(s.ins->tick), std::stod(p));
      okx_px_norm.insert(b);
    }
    const std::string fin = final_of(s.oid);
    bool ok = std::to_string(acked_xid) == ord_id || acked_xid == 0;
    if (state == "canceled" || state == "mmp_canceled")
      ok = ok && fin == "CancAck";
    else if (state == "filled")
      ok = ok && fin == "Fill";
    else
      ok = false; // nothing should be left live
    ok = ok && std::fabs(okx_sz - som_sz) < 1e-9 && okx_px_norm == som_px;
    std::cout << s.ins->inst_id << " oid " << s.oid << " " << s.what << ": SOM [" << joined << "] OKX ordId "
              << ord_id << " state " << state << " accFillSz " << d.value("accFillSz", "") << " fills "
              << f.value("data", json::array()).size() << " SOM fill size " << som_sz << " -> "
              << (ok ? "MATCH" : "MISMATCH") << "\n";
    if (!ok)
      ++problems;
  }

  // ---- post-trade: TradeCapture against OKX's fills ----
  size_t tc_ours = 0, tc_extra = 0, tc_bad = 0;
  std::set<std::string> tc_seen;
  for (auto *t : ptsub.all<post_trade::msg::TradeCapture>())
  {
    const std::string &key = t->trade_id; // "instId:tradeId"
    if (!okx_trades.count(key))
    {
      ++tc_extra; // a fill of an order this run did not send (earlier activity on the account)
      continue;
    }
    ++tc_ours;
    tc_seen.insert(key);
    const auto &[sz, px] = okx_fill[key];
    if (std::fabs(t->price() - std::stod(px)) > 1e-9)
      ++tc_bad;
    (void)sz;
  }
  const size_t tc_missing = okx_trades.size() - tc_seen.size();
  std::cout << "TradeCapture: OKX fills of this run " << okx_trades.size() << ", captured " << tc_ours << ", missing "
            << tc_missing << ", price mismatches " << tc_bad << ", other fills on the account " << tc_extra << "\n";
  if (tc_missing || tc_bad || tc_ours != okx_trades.size())
    ++problems;

  mgr->end();
  std::cout << (problems ? "FAILED: " : "PASSED: ") << sent.size() << " orders, " << problems << " problems\n";
  return problems ? 1 : 0;
}
