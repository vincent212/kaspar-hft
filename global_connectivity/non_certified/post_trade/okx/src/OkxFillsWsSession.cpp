/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The real FillsSession: OKX's private WebSocket (orders or fills channel)
// and REST GET /api/v5/trade/fills. NEVER RUN against OKX by its author: no
// demo keys were available (README.md). Demo requests carry
// x-simulated-trading: 1 or are not sent (oe::require_demo_header).
//
// Two threads: the WebSocket thread reads and answers OKX's keep-alive (OKX
// closes a connection with nothing for 30 s); a reconciliation pass, up to a
// thousand HTTPS requests, runs on the REST thread
// (crypto_common::RestWorker) so the socket never waits on it.

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "crypto_common/RestWorker.hpp"
#include "okx_md/OkxWs.hpp"
#include "okx_oe/OkxAuth.hpp"
#include "okx_pt/OkxFills.hpp"

namespace okx::pt
{
  using json = nlohmann::json;

  namespace
  {
    uint64_t epoch_ms()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }
  }

  class FillsWsSession : public FillsSession
  {
  public:
    FillsWsSession(const FillsConfig &c, FillsEvents &ev) : cfg_(c), ev_(ev) {}
    ~FillsWsSession() override
    {
      stop_ = true;
      rest_.stop();
      if (thread_.joinable())
        thread_.join();
    }
    void logon() override { thread_ = std::thread([this] { run(); }); }
    CallStatus reconcile(uint64_t begin_ms) override
    {
      if (!ready_)
        return CallStatus::Disconnected;
      rest_.post([this, begin_ms] { fetch(begin_ms); });
      return CallStatus::Success;
    }
    void logout() override { stop_ = true; }

  private:
    HttpResult rest_get(const std::string &path)
    {
      oe::Headers h = cfg_.ep.headers();
      std::string err;
      if (!oe::require_demo_header(cfg_.ep, h, err))
        throw std::runtime_error(err);
      const std::string ts = auth::iso_timestamp(epoch_ms());
      h["OK-ACCESS-KEY"] = keys_.api_key;
      h["OK-ACCESS-SIGN"] = auth::rest_sign(keys_.secret_key, ts, "GET", path, "");
      h["OK-ACCESS-TIMESTAMP"] = ts;
      h["OK-ACCESS-PASSPHRASE"] = keys_.passphrase;
      return https_request(cfg_.ep.rest_host, "GET", path, "", h);
    }

    void run()
    {
      std::string err;
      if (!cfg_.ep.check(err))
        return ev_.login_rejected("config", err);
      if (!auth::load_keys(cfg_.key_file, keys_, err))
        return ev_.login_rejected("keys", err);
      try
      {
        WsClient ws(cfg_.ep.ws_host, cfg_.ep.ws_port, "/ws/v5/private", cfg_.ep.headers());
        ws.connect();
        ws.send(auth::ws_login(keys_, epoch_ms() / 1000).dump());
        if (!await(ws, "login"))
          return;
        json arg = {{"channel", cfg_.channel}};
        if (cfg_.channel == "orders")
          arg["instType"] = "ANY";
        ws.send(json{{"op", "subscribe"}, {"args", json::array({arg})}}.dump());
        if (!await(ws, "subscribe"))
          return;
        rest_.start();
        ready_ = true;
        ev_.logged_in();
        auto last_rx = std::chrono::steady_clock::now();
        bool pinged = false;
        while (!stop_)
        {
          auto f = ws.read_for(std::chrono::milliseconds(100));
          const auto now = std::chrono::steady_clock::now();
          if (!f)
          {
            if (!pinged && now - last_rx > std::chrono::seconds(20))
            {
              ws.send("ping");
              pinged = true;
            }
            else if (pinged && now - last_rx > std::chrono::seconds(40))
              throw std::runtime_error("no pong");
            continue;
          }
          last_rx = now;
          pinged = false;
          if (*f == "pong")
            continue;
          auto j = json::parse(*f);
          const std::string ev = j.value("event", "");
          if (ev == "error" || ev == "notice")
            throw std::runtime_error("OKX " + ev + " " + j.value("code", "") + ": " + j.value("msg", ""));
          if (j.contains("data") && j.contains("arg"))
            for (const auto &d : j["data"])
              ev_.fill(d, false); // the client keeps only elements with fillSz and tradeId
        }
      }
      catch (const std::exception &e)
      {
        ready_ = false;
        if (!stop_)
          ev_.disconnected(e.what());
      }
    }

    bool await(WsClient &ws, const std::string &name)
    {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (std::chrono::steady_clock::now() < end && !stop_)
      {
        auto f = ws.read_for(std::chrono::milliseconds(100));
        if (!f || *f == "pong")
          continue;
        auto j = json::parse(*f);
        const std::string ev = j.value("event", "");
        if (ev == name)
          return true;
        if (ev == "error")
        {
          ev_.login_rejected(j.value("code", ""), j.value("msg", ""));
          return false;
        }
      }
      if (!stop_)
        ev_.disconnected("no " + name + " answer within 10 s");
      return false;
    }

    // GET /api/v5/trade/fills from begin_ms, newest first, 100 per page,
    // older pages by `after` = the last billId. 60 requests per 2 s allowed;
    // one page per 100 ms here. REST thread.
    void fetch(uint64_t begin_ms)
    {
      size_t rows = 0;
      std::string after;
      try
      {
        for (int page = 0; page < 1000 && !stop_; ++page)
        {
          std::string q = "/api/v5/trade/fills?begin=" + std::to_string(begin_ms) + "&limit=100";
          if (!after.empty())
            q += "&after=" + after;
          auto r = rest_get(q);
          auto j = json::parse(r.body);
          if (r.status != 200 || j.value("code", "") != "0")
            return ev_.reconciled(rows, false, "HTTP " + std::to_string(r.status) + " code " + j.value("code", ""));
          const auto &d = j["data"];
          for (const auto &f : d)
          {
            ev_.fill(f, true);
            ++rows;
            after = f.value("billId", "");
          }
          if (d.size() < 100)
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    auth::Keys keys_;
    std::atomic<bool> stop_{false}, ready_{false};
    crypto_common::RestWorker rest_;
    std::thread thread_;
  };

  FillsSessionFactory ws_fills_factory()
  {
    return [](const FillsConfig &c, FillsEvents &ev) { return std::make_unique<FillsWsSession>(c, ev); };
  }
}
