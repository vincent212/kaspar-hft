/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The real FillsSessions (KrakenFills.hpp). NEITHER HAS RUN against Kraken:
// the futures one needs demo keys that were not supplied, and the spot one is
// production only (README.md, "Testing ladder").
//
// Futures rate budget (500 per 10 s): the poll (fills without lastFillTime)
// costs 2; a reconciliation page (with lastFillTime) costs 25.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <set>
#include <thread>

#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_oe/KrakenSpotCodec.hpp"
#include "kraken_oe/WsConn.hpp"
#include "kraken_pt/KrakenFills.hpp"

namespace kraken::pt
{
  using json = nlohmann::json;

  namespace
  {
    uint64_t now_ns()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    // ns since the epoch -> "2026-11-18T10:00:00.123Z"
    std::string iso_ms(uint64_t ns)
    {
      const time_t s = time_t(ns / 1'000'000'000ull);
      std::tm tm{};
      gmtime_r(&s, &tm);
      char b[40];
      std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                    tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(ns / 1'000'000ull % 1000));
      return b;
    }

    struct Shared
    {
      std::mutex mu;
      std::set<std::string> seen; // fill_ids the poll reported (or history)
      uint64_t since_ns = 0;
    };

    class FuturesFills : public FillsSession
    {
    public:
      FuturesFills(const FillsConfig &c, oe::HttpFn http, std::shared_ptr<Shared> sh, FillsEvents &ev)
          : c_(c), http_(std::move(http)), sh_(std::move(sh)), ev_(ev)
      {
      }
      ~FuturesFills() override
      {
        stop_ = true;
        if (th_.joinable())
          th_.join();
      }
      void logon() override { th_ = std::thread([this] { run(); }); }
      bool reconcile(uint64_t start_ns) override
      {
        if (!ready_)
          return false;
        std::lock_guard<std::mutex> l(mu_);
        pending_.push_back(start_ns);
        return true;
      }
      void logout() override { stop_ = true; }

    private:
      json call(const std::string &params)
      {
        std::string err;
        if (!c_.ep.check(err))
          throw std::runtime_error(err);
        const std::string nonce = std::to_string(oe::next_nonce());
        auto h = oe::futures_headers(keys_.api_key, keys_.api_secret, "GET", "/api/v3/fills", params, nonce);
        auto r = http_("GET", c_.ep.rest_host, "/derivatives/api/v3/fills" + (params.empty() ? "" : "?" + params), h,
                       "");
        if (r.status != 200)
          throw std::runtime_error("fills: HTTP " + std::to_string(r.status));
        auto j = kraken::parse_raw(r.body);
        if (j.value("result", std::string()) != "success")
          throw std::runtime_error("fills: " + j.value("error", std::string("error")));
        return j;
      }

      void poll(bool first)
      {
        auto j = call("");
        auto rows = j.value("fills", json::array());
        std::vector<json> fresh;
        {
          std::lock_guard<std::mutex> l(sh_->mu);
          for (const auto &f : rows)
          {
            const std::string id = f.value("fill_id", std::string());
            if (id.empty() || !sh_->seen.insert(id).second)
              continue;
            if (kraken::parse_rfc3339(f.value("fillTime", std::string())) < sh_->since_ns)
              continue;
            fresh.push_back(f);
          }
        }
        std::sort(fresh.begin(), fresh.end(), [](const json &a, const json &b) {
          return a.value("fillTime", std::string()) < b.value("fillTime", std::string());
        });
        for (const auto &f : fresh)
          ev_.fill(f, "", Source::FuturesPoll);
        (void)first;
      }

      void pass(uint64_t start_ns)
      {
        size_t n = 0;
        std::string last = iso_ms(now_ns());
        try
        {
          for (int page = 0; page < 20 && !stop_; ++page)
          {
            auto j = call("lastFillTime=" + oe::url_encode(last));
            auto rows = j.value("fills", json::array());
            std::string oldest = last;
            for (const auto &f : rows)
            {
              const std::string t = f.value("fillTime", std::string());
              if (kraken::parse_rfc3339(t) < start_ns)
                continue;
              ++n;
              ev_.fill(f, "", Source::FuturesRest);
              oldest = std::min(oldest, t);
            }
            if (rows.size() < 100 || oldest == last || kraken::parse_rfc3339(oldest) < start_ns)
              break;
            last = oldest;
          }
          ev_.reconciled(n, true, "");
        }
        catch (const std::exception &e)
        {
          ev_.reconciled(n, false, e.what());
          throw;
        }
      }

      void run()
      {
        std::string err;
        if (!c_.ep.check(err))
          return ev_.login_failed(err);
        if (!oe::load_keys(c_.key_file, keys_, err))
          return ev_.login_failed(err);
        try
        {
          try
          {
            poll(true);
          }
          catch (const std::exception &e)
          {
            const std::string w = e.what();
            if (w.find("auth") != std::string::npos || w.find("HTTP 401") != std::string::npos ||
                w.find("HTTP 403") != std::string::npos)
              return ev_.login_failed(w);
            throw;
          }
          ready_ = true;
          ev_.logged_in();
          auto next = std::chrono::steady_clock::now() + std::chrono::milliseconds(c_.poll_ms);
          while (!stop_)
          {
            std::vector<uint64_t> p;
            {
              std::lock_guard<std::mutex> l(mu_);
              p.swap(pending_);
            }
            for (auto s : p)
              pass(s);
            if (std::chrono::steady_clock::now() >= next)
            {
              poll(false);
              next = std::chrono::steady_clock::now() + std::chrono::milliseconds(c_.poll_ms);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        }
        catch (const std::exception &e)
        {
          ready_ = false;
          if (!stop_)
            ev_.disconnected(e.what());
        }
      }

      FillsConfig c_;
      oe::HttpFn http_;
      std::shared_ptr<Shared> sh_;
      FillsEvents &ev_;
      oe::Keys keys_;
      std::mutex mu_;
      std::vector<uint64_t> pending_;
      std::atomic<bool> stop_{false}, ready_{false};
      std::thread th_;
    };

    class SpotFills : public FillsSession
    {
    public:
      SpotFills(const FillsConfig &c, oe::HttpFn http, FillsEvents &ev) : c_(c), http_(std::move(http)), ev_(ev) {}
      ~SpotFills() override
      {
        stop_ = true;
        if (th_.joinable())
          th_.join();
      }
      void logon() override { th_ = std::thread([this] { run(); }); }
      bool reconcile(uint64_t start_ns) override
      {
        if (!ready_)
          return false;
        std::lock_guard<std::mutex> l(mu_);
        pending_.push_back(start_ns);
        return true;
      }
      void logout() override { stop_ = true; }

    private:
      json private_call(const std::string &path, const std::string &extra)
      {
        const std::string nonce = std::to_string(oe::next_nonce());
        const std::string body = "nonce=" + nonce + (extra.empty() ? "" : "&" + extra);
        oe::HttpHeaders h{{"API-Key", keys_.api_key},
                          {"API-Sign", oe::spot_api_sign(path, nonce, body, keys_.api_secret)},
                          {"Content-Type", "application/x-www-form-urlencoded"}};
        auto r = http_("POST", c_.ep.rest_host, path, h, body);
        if (r.status != 200)
          throw std::runtime_error(path + ": HTTP " + std::to_string(r.status));
        auto j = kraken::parse_raw(r.body);
        if (j.contains("error") && !j["error"].empty())
          throw std::runtime_error(path + ": " + j["error"].dump());
        return j;
      }

      void pass(uint64_t start_ns)
      {
        size_t n = 0;
        try
        {
          // TradesHistory: 50 rows per page, newest first; ofs pages back.
          for (int ofs = 0; ofs < 50 * 40 && !stop_; ofs += 50)
          {
            auto j = private_call("/0/private/TradesHistory",
                                  "start=" + std::to_string(start_ns / 1'000'000'000ull) + "&ofs=" + std::to_string(ofs));
            const auto &trades = j.at("result").at("trades");
            for (auto it = trades.begin(); it != trades.end(); ++it)
            {
              ++n;
              ev_.fill(it.value(), it.key(), Source::SpotRest);
            }
            if (trades.size() < 50)
              break;
          }
          ev_.reconciled(n, true, "");
        }
        catch (const std::exception &e)
        {
          ev_.reconciled(n, false, e.what());
          throw;
        }
      }

      void run()
      {
        std::string err;
        if (!c_.ep.check(err))
          return ev_.login_failed(err);
        if (!oe::load_keys(c_.key_file, keys_, err))
          return ev_.login_failed(err);
        try
        {
          std::string token;
          try
          {
            token = private_call("/0/private/GetWebSocketsToken", "").at("result").at("token").get<std::string>();
          }
          catch (const std::exception &e)
          {
            return ev_.login_failed(e.what());
          }
          oe::WsConn ws;
          ws.connect(c_.ep.ws_host, "/v2");
          ws.send(oe::spot::subscribe_executions(token, false, true));
          uint64_t last_seq = 0;
          auto last_rx = std::chrono::steady_clock::now();
          bool pinged = false;
          while (!stop_)
          {
            std::vector<uint64_t> p;
            {
              std::lock_guard<std::mutex> l(mu_);
              p.swap(pending_);
            }
            for (auto s : p)
              pass(s);
            auto f = ws.read_for(std::chrono::milliseconds(50));
            const auto now = std::chrono::steady_clock::now();
            if (!f)
            {
              if (!pinged && now - last_rx > std::chrono::seconds(20))
              {
                ws.send(R"({"method":"ping"})");
                pinged = true;
              }
              else if (pinged && now - last_rx > std::chrono::seconds(40))
                throw std::runtime_error("no answer to ping");
              continue;
            }
            last_rx = now;
            pinged = false;
            auto j = kraken::parse_raw(*f);
            if (j.value("method", std::string()) == "subscribe")
            {
              if (!j.value("success", false))
                return ev_.login_failed("subscribe executions: " + j.value("error", std::string()));
              if (!ready_)
              {
                ready_ = true;
                ev_.logged_in();
              }
              continue;
            }
            if (j.value("channel", std::string()) != "executions")
              continue;
            if (auto s = j.find("sequence"); s != j.end() && s->is_number())
            {
              const uint64_t q = s->get<uint64_t>();
              if (last_seq && q != last_seq + 1)
                throw std::runtime_error("executions sequence gap: expected " + std::to_string(last_seq + 1) +
                                         ", got " + std::to_string(q));
              last_seq = q;
            }
            for (const auto &e : j.value("data", json::array()))
              if (e.value("exec_type", std::string()) == "trade")
                ev_.fill(e, "", Source::SpotWs);
          }
          ws.close();
        }
        catch (const std::exception &e)
        {
          ready_ = false;
          if (!stop_)
            ev_.disconnected(e.what());
        }
      }

      FillsConfig c_;
      oe::HttpFn http_;
      FillsEvents &ev_;
      oe::Keys keys_;
      std::mutex mu_;
      std::vector<uint64_t> pending_;
      std::atomic<bool> stop_{false}, ready_{false};
      std::thread th_;
    };
  }

  FillsSessionFactory futures_fills_factory(const FillsConfig &cfg, oe::HttpFn http, uint64_t since_ns)
  {
    auto sh = std::make_shared<Shared>();
    sh->since_ns = since_ns ? since_ns : now_ns();
    return [cfg, http, sh](FillsEvents &ev) -> std::unique_ptr<FillsSession> {
      return std::make_unique<FuturesFills>(cfg, http, sh, ev);
    };
  }

  FillsSessionFactory spot_fills_factory(const FillsConfig &cfg, oe::HttpFn http)
  {
    return [cfg, http](FillsEvents &ev) -> std::unique_ptr<FillsSession> {
      return std::make_unique<SpotFills>(cfg, http, ev);
    };
  }
}
