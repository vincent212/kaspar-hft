/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// bybit_fills_check: testing-ladder step 3 for BybitFillsClient. NOT
// CERTIFIED. Testnet (default) or demo (--demo) only; the hosts are fixed
// here and the client's guard refuses any other.
//
// Needs ~/.kaspar/keys/bybit_testnet.json (or --keys PATH):
// {"api_key": "...", "api_secret": "..."}, a testnet or demo HMAC key with
// read permission on Orders (trade history). Without it, exits 2.
//
// Runs the client for --minutes (default 10) while orders are filled on the
// account (order_entry/bybit's bybit_testnet_check, run at the same time,
// makes some), then fetches GET /v5/execution/list itself for the same
// window and checks:
//   - every Trade-type execution in REST was captured exactly once, with
//     the same side, quantity, price and orderId;
//   - nothing was captured that REST does not have;
//   - the subscriber received each execId once (stream + REST copies merged).
// One JSON summary line; exit code 0 only if every check passed. Keys are
// never printed.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"
#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_post_trade/act/BybitFillsClient.hpp"

using namespace bybit_pt;
using post_trade::msg::TradeCapture;

namespace
{
  class Sub : public actors::Actor
  {
  public:
    const char *get_name() const override { return "sub"; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      if (auto t = dynamic_cast<const TradeCapture *>(m))
      {
        std::lock_guard<std::mutex> l(mu);
        ++count[t->trade_id];
        got[t->trade_id] = *t;
      }
      delete m;
    }
    std::mutex mu;
    std::map<std::string, int> count;
    std::map<std::string, TradeCapture> got;
  };

  uint64_t now_ms()
  {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
  }
}

int main(int argc, char **argv)
{
  std::string keys = "~/.kaspar/keys/bybit_testnet.json";
  bool demo = false;
  double minutes = 10;
  for (int i = 1; i < argc; ++i)
  {
    std::string k = argv[i];
    auto v = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--keys") keys = v();
    else if (k == "--demo") demo = true;
    else if (k == "--minutes") minutes = std::stod(v());
    else
    {
      std::cerr << "unknown argument " << k << std::endl;
      return 2;
    }
  }
  std::string path = keys;
  if (!path.empty() && path[0] == '~')
    if (const char *h = std::getenv("HOME"))
      path = h + path.substr(1);
  if (!std::filesystem::exists(path))
  {
    std::cerr << "no key file " << keys
              << ": write {\"api_key\": \"...\", \"api_secret\": \"...\"} for a Bybit testnet "
                 "HMAC key there; nothing was sent"
              << std::endl;
    return 2;
  }

  ClientConfig cfg;
  cfg.source.testnet = true;
  cfg.source.rest_host = demo ? "api-demo.bybit.com" : "api-testnet.bybit.com";
  cfg.source.ws_host = demo ? "stream-demo.bybit.com" : "stream-testnet.bybit.com";
  std::string err;
  if (!bybit::auth::load(keys, cfg.source.credentials, err))
  {
    std::cerr << "key file: " << err << std::endl;
    return 2;
  }
  cfg.instruments[{"linear", "BTCUSDT"}] = {"linear", "BTCUSDT", 1, 0.001};
  cfg.instruments[{"spot", "BTCUSDT"}] = {"spot", "BTCUSDT", 2, 0.000001};
  cfg.reconcile_every_s = 60;
  const uint64_t t0 = now_ms() - 3600 * 1000; // one hour back
  cfg.since_ms = t0;

  Sub sub;
  auto client = create_BybitFillsClient(cfg, &sub);
  if (!client)
    return 1;
  auto mgr = std::make_unique<actors::Manager>("bybit_fills_check");
  mgr->manage_and_start(client);
  std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(minutes * 60000)));
  mgr->end();
  mgr.reset();
  auto *c = static_cast<BybitFillsClient *>(client);
  const auto st = c->stats();
  delete client;
  const uint64_t t1 = now_ms();

  // Bybit's own record of the window.
  bybit::priv::RestClient rest(cfg.source.credentials, cfg.source.rest_host, true);
  int pass = 0, fail = 0, venue_trades = 0;
  auto check = [&](bool ok, const std::string &what) {
    ok ? ++pass : ++fail;
    if (!ok)
      std::cout << "FAIL " << what << std::endl;
  };
  std::map<std::string, bool> seen;
  for (std::string cat : {"linear", "spot"})
  {
    std::string cursor;
    do
    {
      std::string q = "category=" + cat + "&startTime=" + std::to_string(t0) + "&endTime=" + std::to_string(t1) +
                      "&limit=100" + (cursor.empty() ? "" : "&cursor=" + cursor);
      auto r = rest.get("/v5/execution/list", q);
      if (!r.ok())
      {
        check(false, "execution/list " + cat + ": " + r.ret_msg);
        break;
      }
      for (const auto &e : r.result.value("list", nlohmann::json::array()))
      {
        if (classify(e.value("execType", std::string())) != ExecKind::Position ||
            e.value("symbol", std::string()) != "BTCUSDT")
          continue;
        ++venue_trades;
        const std::string id = e.value("execId", std::string());
        seen[id] = true;
        std::lock_guard<std::mutex> l(sub.mu);
        auto it = sub.got.find(id);
        if (it == sub.got.end())
        {
          check(false, cat + " execution " + id + " not captured");
          continue;
        }
        check(sub.count[id] == 1, id + " captured once");
        TradeCapture want;
        std::string why;
        to_capture(e, cat, cfg.instruments, true, want, why);
        const auto &g = it->second;
        check(g.side == want.side && g.qty == want.qty && g.price_mantissa == want.price_mantissa &&
                  g.price_decimals == want.price_decimals && g.venue_order_id == want.venue_order_id,
              id + " economics match REST");
      }
      cursor = r.result.value("nextPageCursor", std::string());
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (!cursor.empty());
  }
  {
    std::lock_guard<std::mutex> l(sub.mu);
    for (const auto &[id, t] : sub.got)
      check(seen.count(id) > 0, id + " captured but not in REST");
  }
  nlohmann::json out = {{"host", cfg.source.rest_host}, {"minutes", minutes},
                        {"venue_trades", venue_trades}, {"captured", sub.got.size()},
                        {"executions_delivered", st.executions}, {"from_stream", st.from_stream},
                        {"from_rest", st.from_rest}, {"duplicates", st.duplicates},
                        {"reconciliations", st.reconciliations}, {"reconcile_failures", st.reconcile_failures},
                        {"logins", st.logins}, {"pass", pass}, {"fail", fail}};
  std::cout << "SUMMARY " << out.dump() << std::endl;
  return fail ? 1 : 0;
}
