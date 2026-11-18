/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// deribit_testnet_check: testing ladder step 3 on test.deribit.com ONLY.
// NEVER RUN: no test keys were supplied (README.md).
//
//   deribit_testnet_check [--config config/deribit_test_example.ini]
//
// Refuses to start unless the configuration is environment = test with host
// test.deribit.com, and unless the key file exists. Then, with DeribitOM and
// DeribitFillsClient running against the test host and a recorder in place of
// the SOM, on the first configured instrument, one unit (one contract):
//   1. logon (OM and fills client)
//   2. GTC buy 30% below the best bid; cancel                 Ack, CancAck
//   3. IOC buy 30% below the best bid                          Ack, CancAck
//   4. FOK buy 30% below the best bid                          Ack, CancAck or Reject
//   5. DAY sell 30% above the best ask; drop the connection;
//      after the reconnect, cancel                             Ack, CancAck
//   6. IOC buy 1% above the best ask (fills against the test book, if it
//      has liquidity), then IOC sell 1% below the best bid to flatten
//                                                              Ack, Fill(s), CancAck for any rest
// and then fetches, over signed REST on the test host,
//   private/get_order_history_by_instrument (include_unfilled, include_old)
//   private/get_user_trades_by_instrument_and_time over the run
// and checks every SOM message against them: each order's final SOM message
// against its Deribit order_state; the sum of its Fills against
// filled_amount; each Fill against a Deribit trade of that order (amount,
// price); and the set of TradeCaptures against the set of Deribit trades.
// Prints the counts and exits 0 only when everything matches.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "deribit_oe/RpcWs.hpp"
#include "deribit_oe/Signing.hpp"
#include "deribit_oe/act/DeribitOM.hpp"
#include "deribit_pt/act/DeribitFillsClient.hpp"

using namespace deribit;
using json = nlohmann::json;
namespace som = frame::som::msg;
using namespace std::chrono_literals;

namespace
{
  uint64_t epoch_ms()
  {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
  }

  class TcSink : public actors::Actor
  {
  public:
    TcSink() { strncpy(name, "tc", sizeof(name) - 1); }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu);
      if (auto t = dynamic_cast<const post_trade::msg::TradeCapture *>(m))
        ids.insert(t->trade_id.substr(t->trade_id.find(':') + 1));
      delete m;
    }
    std::set<std::string> get()
    {
      std::lock_guard<std::mutex> l(mu);
      return ids;
    }

  private:
    std::mutex mu;
    std::set<std::string> ids;
  };

  json get(const std::string &host, const oe::Keys &k, const std::string &target)
  {
    const auto h = oe::rest_authorization(k.client_id, k.client_secret, epoch_ms(), oe::make_nonce(), "GET", target, "");
    auto r = oe::https_request(host, "GET", target, "", {{"Authorization", h}});
    auto j = json::parse(r.body);
    if (r.status != 200 || j.contains("error"))
      throw std::runtime_error(target + ": HTTP " + std::to_string(r.status));
    return j["result"];
  }
}

int main(int argc, char **argv)
{
  std::string path = "config/deribit_test_example.ini";
  for (int i = 1; i + 1 < argc; ++i)
    if (std::string(argv[i]) == "--config")
      path = argv[i + 1];

  oe::DeribitOMConfig cfg;
  std::string err;
  if (!oe::DeribitOMConfig::from_file(path, cfg, err))
  {
    std::cerr << "cannot read " << path << ": " << err << std::endl;
    return 2;
  }
  if (!cfg.ep.test || cfg.ep.host != oe::kTestHost || cfg.sim)
  {
    std::cerr << "refused: this check runs only with environment = test and host " << oe::kTestHost << std::endl;
    return 2;
  }
  if (!std::filesystem::exists(cfg.key_file))
  {
    std::cerr << "no key file " << cfg.key_file
              << ".\nIt must hold the client id and secret of an API key created on test.deribit.com with scope "
                 "trade:read_write:\n  {\"client_id\": \"...\", \"client_secret\": \"...\"}\n";
    return 3;
  }
  oe::Keys keys;
  if (!oe::load_keys(cfg.key_file, keys, err))
  {
    std::cerr << err << std::endl;
    return 3;
  }
  if (!cfg.valid(err))
  {
    std::cerr << "bad config: " << err << std::endl;
    return 2;
  }

  const std::string seed = "build/deribit_testnet_universe_seed.csv";
  std::filesystem::create_directories("build");
  std::ofstream(seed) << "# generated by deribit_testnet_check\n";
  frame::ref::RefData::set_universe(seed, "");
  frame::ref::RefData::inst();
  int sid = 990001;
  for (auto &i : cfg.instruments)
    i.sym = int(frame::ref::RefData::add_future_asset(i.asset, en::x::DERIBIT, sid++, "", i.name, 1.0)->id);
  const auto ins = cfg.instruments[0];

  pt::FillsConfig fc;
  fc.ep = cfg.ep;
  fc.key_file = cfg.key_file;
  fc.reconcile_every_s = 0;
  for (const auto &i : cfg.instruments)
    fc.instruments.push_back({i.name, i.currency, i.asset, i.qty_unit, i.venue, i.sym});

  oe::test::Recorder somr("SOM");
  TcSink tcs;
  auto *om = static_cast<oe::DeribitOM *>(create_DeribitOM(cfg, &somr));
  auto *fills = static_cast<pt::DeribitFillsClient *>(create_DeribitFillsClient(fc, &tcs));
  if (!om || !fills)
    return 2;
  actors::Manager mgr("check");
  mgr.manage_and_start(om);
  mgr.manage_and_start(fills);
  const uint64_t t_start = epoch_ms();

  auto fail = [&](const std::string &why) {
    std::cerr << "FAILED: " << why << std::endl;
    mgr.end();
    return 1;
  };
  if (!oe::test::wait_until([&] { return om->ready() && fills->ready(); }, 20s))
    return fail("no logon within 20 s");
  std::cout << "1 logon: OM and fills client logged in to " << cfg.ep.host << std::endl;

  const json tk = json::parse(oe::https_request(cfg.ep.host, "GET",
                                                "/api/v2/public/ticker?instrument_name=" + ins.name).body)["result"];
  const double bid = tk.value("best_bid_price", 0.0), ask = tk.value("best_ask_price", 0.0);
  if (!(bid > 0) || !(ask > 0))
    return fail("no best bid/ask on the test book: " + tk.dump());
  const double tick = ins.tick_d();
  auto ticks = [&](double px) { return int(std::llround(px / tick)); };
  int oid = 1;
  std::map<int, std::string> step_of;
  auto send = [&](en::bs side, double px, som::Order::TimeInForce tif, const std::string &step) {
    auto o = new som::Order("CHK", en::x::DERIBIT, uint(ins.sym), 1, ticks(px), side, 4);
    o->oid = oid;
    o->tif = tif;
    step_of[oid] = step;
    om->send(o, &somr);
    return oid++;
  };
  auto done = [&](int id) {
    for (auto *a : somr.all<som::CancAck>())
      if (int(a->id) == id)
        return true;
    for (auto *r : somr.all<som::Reject>())
      if (int(r->id) == id)
        return true;
    for (auto *f : somr.all<som::Fill>())
      if (int(f->id) == id && f->still_to_be_filled == 0)
        return true;
    return false;
  };
  auto acked = [&](int id) {
    for (auto *a : somr.all<som::Ack>())
      if (int(a->id) == id)
        return true;
    return false;
  };

  using TIF = som::Order::TimeInForce;
  int a = send(en::bs::BUY, bid * 0.7, TIF::GTC, "2 GTC far");
  if (!oe::test::wait_until([&] { return acked(a); }, 10s))
    return fail("step 2: no Ack");
  om->send(new som::Cancel(a), &somr);
  if (!oe::test::wait_until([&] { return done(a); }, 10s))
    return fail("step 2: no CancAck");
  int b = send(en::bs::BUY, bid * 0.7, TIF::IOC, "3 IOC far");
  int c = send(en::bs::BUY, bid * 0.7, TIF::FOK, "4 FOK far");
  if (!oe::test::wait_until([&] { return done(b) && done(c); }, 10s))
    return fail("steps 3-4: IOC/FOK did not end");
  int d = send(en::bs::SEL, ask * 1.3, TIF::DAY, "5 DAY far, reconnect");
  if (!oe::test::wait_until([&] { return acked(d); }, 10s))
    return fail("step 5: no Ack");
  const int sessions = om->sessions_created();
  om->send(new oe::msg::DeribitReport(oe::msg::DeribitReport::DROP_CONNECTION, 0), nullptr);
  if (!oe::test::wait_until([&] { return om->sessions_created() > sessions && om->ready(); }, 20s))
    return fail("step 5: no reconnect");
  if (done(d))
    std::cout << "5 the order ended during the reconnect (cancel_on_disconnect = "
              << cfg.cancel_on_disconnect << ")" << std::endl;
  else
  {
    om->send(new som::Cancel(d), &somr);
    if (!oe::test::wait_until([&] { return done(d); }, 10s))
      return fail("step 5: no CancAck after the reconnect");
  }
  int e = send(en::bs::BUY, ask * 1.01, TIF::IOC, "6 IOC through the ask");
  if (!oe::test::wait_until([&] { return done(e); }, 10s))
    return fail("step 6: buy did not end");
  int f = send(en::bs::SEL, bid * 0.99, TIF::IOC, "6 IOC through the bid");
  if (!oe::test::wait_until([&] { return done(f); }, 10s))
    return fail("step 6: sell did not end");
  std::this_thread::sleep_for(5s);   // let notifications and REST catch up
  const uint64_t t_end = epoch_ms();

  // ---- the exchange's own records ----
  json orders, trades;
  try
  {
    orders = get(cfg.ep.host, keys,
                 "/api/v2/private/get_order_history_by_instrument?instrument_name=" + ins.name +
                     "&count=100&include_old=true&include_unfilled=true");
    std::this_thread::sleep_for(200ms);
    trades = get(cfg.ep.host, keys,
                 "/api/v2/private/get_user_trades_by_instrument_and_time?instrument_name=" + ins.name +
                     "&start_timestamp=" + std::to_string(t_start - 1000) + "&end_timestamp=" +
                     std::to_string(t_end) + "&count=1000&sorting=asc")["trades"];
  }
  catch (const std::exception &x)
  {
    return fail(std::string("REST: ") + x.what());
  }

  int checked = 0, mismatches = 0;
  std::set<std::string> rest_trade_ids;
  for (const auto &t : trades)
    rest_trade_ids.insert(t.value("trade_id", ""));
  for (int id = 1; id < oid; ++id)
  {
    const auto *e2 = om->book().find_oid(id);
    if (!e2)
      continue;
    const std::string label = std::to_string(e2->cid);
    json ro;
    for (const auto &o : orders)
      if (o.value("label", "") == label)
        ro = o;
    std::string som_final = "none";
    double som_filled = 0;
    std::vector<const som::Fill *> fs;
    for (auto *x : somr.all<som::Fill>())
      if (int(x->id) == id)
      {
        som_filled += x->sz;
        fs.push_back(x);
        som_final = "filled";
      }
    for (auto *x : somr.all<som::CancAck>())
      if (int(x->id) == id)
        som_final = "cancelled";
    for (auto *x : somr.all<som::Reject>())
      if (int(x->id) == id)
        som_final = "rejected";
    if (!fs.empty() && fs.back()->still_to_be_filled > 0 && som_final == "filled")
      som_final = "partial";
    const std::string state = ro.is_object() ? ro.value("order_state", "") : "missing";
    const double filled = ro.is_object() ? ro.value("filled_amount", 0.0) / ins.unit_d() : -1;
    bool ok = ro.is_object() || som_final == "rejected";
    if (ro.is_object())
      ok = ok && state == (som_final == "partial" ? "cancelled" : som_final) && std::fabs(filled - som_filled) < 1e-9;
    for (auto *x : fs)
    {
      bool found = false;
      for (const auto &t : trades)
        if (t.value("order_id", "") == ro.value("order_id", "") &&
            std::llround(t.value("price", 0.0) / tick) == x->pxi)
          found = true;
      ok = ok && found;
    }
    ++checked;
    if (!ok)
      ++mismatches;
    std::cout << step_of[id] << ": oid " << id << " label " << label << " SOM " << som_final << " filled "
              << som_filled << " | Deribit " << state << " filled " << filled << (ok ? "  ok" : "  MISMATCH")
              << std::endl;
  }
  const auto tc = fills->ledger().live();
  const auto tc_ids = tcs.get();
  const bool tc_ok = tc_ids == rest_trade_ids;
  std::cout << "SOM messages: " << somr.replies().size() << "; orders checked " << checked << ", mismatches "
            << mismatches << "\nTradeCapture: " << tc_ids.size() << " trades (ledger " << tc
            << "), Deribit REST: " << rest_trade_ids.size() << " trades: " << (tc_ok ? "equal" : "DIFFERENT")
            << std::endl;
  mgr.end();
  return mismatches == 0 && tc_ok ? 0 : 1;
}
