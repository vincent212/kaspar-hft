/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitFillsWsSession: the real FillsSession. NEVER RUN against Deribit by
// its author: no test keys were available (README.md).
//
// One thread: public/auth (client_signature), private/subscribe
// user.trades.any.any.raw, public/set_heartbeat, then read. A reconciliation
// pass is run on the same thread between reads, over HTTPS:
//   GET /api/v2/private/get_user_trades_by_currency_and_time?currency=..&
//       start_timestamp=..&end_timestamp=..&count=1000&sorting=asc
//   Authorization: deri-hmac-sha256 id=..,ts=..,nonce=..,sig=..
// one currency after another, following has_more (at most 20 pages per
// currency per pass, at least 100 ms apart: far below the non-matching
// limit of about 20 requests a second, feed.md 2).

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "deribit_oe/RpcWs.hpp"
#include "deribit_oe/Signing.hpp"
#include "deribit_pt/DeribitFills.hpp"

namespace deribit::pt
{
  using json = nlohmann::json;
  using namespace std::chrono_literals;

  namespace
  {
    uint64_t epoch_ms()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    json request(int64_t id, const std::string &method, json params)
    {
      return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
    }
  }

  class DeribitFillsWsSession : public FillsSession
  {
  public:
    DeribitFillsWsSession(const FillsConfig &cfg, FillsEvents &ev) : cfg_(cfg), ev_(ev) {}
    ~DeribitFillsWsSession() override
    {
      stop_ = true;
      if (thread_.joinable())
        thread_.join();
    }

    void logon() override { thread_ = std::thread([this] { run(); }); }

    CallStatus reconcile(uint64_t begin_ms, uint64_t end_ms) override
    {
      if (!ready_)
        return CallStatus::Disconnected;
      std::lock_guard<std::mutex> l(mu_);
      pass_ = true;
      begin_ = begin_ms;
      end_ = end_ms;
      return CallStatus::Success;
    }

    void logout() override { stop_ = true; }

  private:
    json await_result(oe::RpcWs &ws, int64_t id)
    {
      const auto end = std::chrono::steady_clock::now() + 10s;
      while (std::chrono::steady_clock::now() < end && !stop_)
      {
        auto f = ws.read_for(50ms);
        if (!f)
          continue;
        auto j = json::parse(*f);
        if (j.value("id", int64_t(-1)) == id)
          return j;
        handle(ws, j);
      }
      throw std::runtime_error("no answer to request " + std::to_string(id) + " within 10 s");
    }

    void run()
    {
      std::string err;
      if (!cfg_.ep.check(err))
        return ev_.login_rejected("config", err);
      if (!oe::load_keys(cfg_.key_file, keys_, err))
        return ev_.login_rejected("keys", err);
      try
      {
        oe::RpcWs ws(cfg_.ep.host);
        ws.connect();
        ws.send(request(1, "public/auth",
                        oe::auth_params(keys_.client_id, keys_.client_secret, epoch_ms(), oe::make_nonce()))
                    .dump());
        auto a = await_result(ws, 1);
        if (a.contains("error"))
          return ev_.login_rejected(std::to_string(a["error"].value("code", 0)), a["error"].value("message", ""));
        ws.send(request(2, "private/subscribe", {{"channels", json::array({"user.trades.any.any.raw"})}}).dump());
        auto s = await_result(ws, 2);
        if (s.contains("error"))
          return ev_.login_rejected(std::to_string(s["error"].value("code", 0)), s["error"].value("message", ""));
        ws.send(request(3, "public/set_heartbeat", {{"interval", cfg_.heartbeat_s}}).dump());
        await_result(ws, 3);
        ready_ = true;
        ev_.logged_in();
        auto last_rx = std::chrono::steady_clock::now();
        while (!stop_)
        {
          bool pass = false;
          uint64_t b = 0, e = 0;
          {
            std::lock_guard<std::mutex> l(mu_);
            std::swap(pass, pass_);
            b = begin_;
            e = end_;
          }
          if (pass)
            run_pass(b, e);
          auto f = ws.read_for(50ms);
          if (!f)
          {
            if (std::chrono::steady_clock::now() - last_rx > std::chrono::seconds(3 * cfg_.heartbeat_s))
              throw std::runtime_error("no frame within three heartbeat intervals");
            continue;
          }
          last_rx = std::chrono::steady_clock::now();
          handle(ws, json::parse(*f));
        }
      }
      catch (const std::exception &e)
      {
        ready_ = false;
        if (!stop_)
          ev_.disconnected(e.what());
      }
    }

    void handle(oe::RpcWs &ws, const json &j)
    {
      const std::string method = j.value("method", "");
      if (method == "heartbeat")
      {
        if (j.contains("params") && j["params"].value("type", "") == "test_request")
          ws.send(request(next_id_++, "public/test", json::object()).dump());
        return;
      }
      if (method == "subscription" && j["params"].value("channel", "").rfind("user.trades.", 0) == 0)
      {
        const json &d = j["params"]["data"];
        if (d.is_array())
          for (const auto &t : d)
            ev_.fill(t, false);
        else
          ev_.fill(d, false);
      }
    }

    // A signed REST GET; the endpoint was checked in run().
    json rest_get(const std::string &target)
    {
      const uint64_t ts = epoch_ms();
      const std::string nonce = oe::make_nonce();
      oe::Headers h = {{"Authorization", oe::rest_authorization(keys_.client_id, keys_.client_secret, ts, nonce,
                                                                "GET", target, "")}};
      auto r = oe::https_request(cfg_.ep.host, "GET", target, "", h);
      auto j = json::parse(r.body);
      if (r.status != 200 || j.contains("error"))
        throw std::runtime_error("HTTP " + std::to_string(r.status) + " " +
                                 (j.contains("error") ? j["error"].value("message", "") : std::string()));
      return j["result"];
    }

    void run_pass(uint64_t begin_ms, uint64_t end_ms)
    {
      size_t rows = 0;
      try
      {
        for (const auto &cur : cfg_.currencies())
        {
          uint64_t start = begin_ms;
          for (int page = 0; page < 20 && !stop_; ++page)
          {
            if (page)
              std::this_thread::sleep_for(100ms);
            const std::string target = "/api/v2/private/get_user_trades_by_currency_and_time?currency=" + cur +
                                       "&start_timestamp=" + std::to_string(start) +
                                       "&end_timestamp=" + std::to_string(end_ms) + "&count=1000&sorting=asc";
            const json res = rest_get(target);
            const json &trades = res.contains("trades") ? res["trades"] : json::array();
            for (const auto &t : trades)
            {
              ev_.fill(t, true);
              ++rows;
              start = std::max<uint64_t>(start, uint64_t(t.value("timestamp", 0.0)));
            }
            // The next page starts at the last trade's millisecond; the
            // trades of that millisecond come again and are duplicates.
            if (!res.value("has_more", false) || trades.empty())
              break;
          }
        }
        ev_.reconciled(rows, true, "");
      }
      catch (const std::exception &e)
      {
        ev_.reconciled(rows, false, e.what());
      }
    }

    FillsConfig cfg_;
    FillsEvents &ev_;
    oe::Keys keys_;
    std::mutex mu_;
    bool pass_ = false;
    uint64_t begin_ = 0, end_ = 0;
    std::atomic<bool> stop_{false}, ready_{false};
    int64_t next_id_ = 100;
    std::thread thread_;
  };

  FillsSessionFactory ws_fills_factory()
  {
    return [](const FillsConfig &cfg, FillsEvents &ev) { return std::make_unique<DeribitFillsWsSession>(cfg, ev); };
  }
}
