/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxWsSession: the real OkxSession over OKX's private WebSocket, with REST
// for recovery and Cancel All After. NEVER RUN against OKX by its author: no
// demo keys were available (README.md, "Testing ladder").
//
// One thread owns the WebSocket: it logs in, subscribes to `orders`, then
// alternates between writing queued requests and reading for up to 50 ms.
// Requests from the actor thread only go into the queue. Keep-alive: the
// text "ping" after 20 s without a frame; no answer within 20 s is a lost
// connection.

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include "okx_md/OkxWs.hpp"
#include "okx_oe/OkxAuth.hpp"
#include "okx_oe/OkxConfig.hpp"
#include "okx_oe/OkxSession.hpp"

namespace okx::oe
{
  using json = nlohmann::json;

  namespace
  {
    constexpr size_t kMaxQueued = 512;
    constexpr auto kPoll = std::chrono::milliseconds(50);
    constexpr auto kPingAfter = std::chrono::seconds(20);

    uint64_t epoch_ms()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }
  }

  class OkxWsSession : public OkxSession
  {
  public:
    OkxWsSession(const OkxOMConfig &cfg, SessionEvents &ev) : cfg_(cfg), ev_(ev) {}
    ~OkxWsSession() override
    {
      stop_ = true;
      if (thread_.joinable())
        thread_.join();
    }

    void logon() override { thread_ = std::thread([this] { run(); }); }

    CallStatus place(const std::string &req_id, const json &arg) override
    {
      return enqueue(json{{"id", req_id}, {"op", "order"}, {"args", json::array({arg})}}.dump());
    }

    CallStatus cancel(const std::string &req_id, const json &arg) override
    {
      return enqueue(json{{"id", req_id}, {"op", "cancel-order"}, {"args", json::array({arg})}}.dump());
    }

    CallStatus recover(const std::string &inst_id, const std::string &ord_id, const std::string &cl) override
    {
      if (!ready_)
        return CallStatus::Disconnected;
      std::lock_guard<std::mutex> l(mu_);
      recover_.push_back({inst_id, ord_id, cl});
      return CallStatus::Success;
    }

    void logout() override { stop_ = true; }

  private:
    struct Recovery
    {
      std::string inst_id, ord_id, cl;
    };

    CallStatus enqueue(std::string text)
    {
      if (!ready_)
        return CallStatus::Disconnected;
      std::lock_guard<std::mutex> l(mu_);
      if (out_.size() >= kMaxQueued)
      {
        if (!busy_)
        {
          busy_ = true;
          ev_.busy();
        }
        return CallStatus::Busy;
      }
      out_.push_back(std::move(text));
      return CallStatus::Success;
    }

    // A signed REST request; demo requests carry x-simulated-trading: 1 or
    // are not sent.
    HttpResult rest(const std::string &method, const std::string &path, const std::string &body = {})
    {
      Headers h = cfg_.ep.headers();
      std::string err;
      if (!require_demo_header(cfg_.ep, h, err))
        throw std::runtime_error(err);
      const std::string ts = auth::iso_timestamp(epoch_ms());
      h["OK-ACCESS-KEY"] = keys_.api_key;
      h["OK-ACCESS-SIGN"] = auth::rest_sign(keys_.secret_key, ts, method, path, body);
      h["OK-ACCESS-TIMESTAMP"] = ts;
      h["OK-ACCESS-PASSPHRASE"] = keys_.passphrase;
      return https_request(cfg_.ep.rest_host, method, path, body, h);
    }

    void run()
    {
      std::string err;
      if (!cfg_.ep.check(err))
        return ev_.login_rejected("config", err);
      if (!auth::load_keys(cfg_.key_file, keys_, err))
        return ev_.login_rejected("keys", err);
      const Headers h = cfg_.ep.headers();
      if (!require_demo_header(cfg_.ep, h, err))
        return ev_.login_rejected("config", err);
      try
      {
        WsClient ws(cfg_.ep.ws_host, cfg_.ep.ws_port, "/ws/v5/private", h);
        ws.connect();
        ws.send(auth::ws_login(keys_, epoch_ms() / 1000).dump());
        if (!await_event(ws, "login"))
          return;
        ws.send(json{{"op", "subscribe"}, {"args", json::array({{{"channel", "orders"}, {"instType", "ANY"}}})}}.dump());
        if (!await_event(ws, "subscribe"))
          return;
        ready_ = true;
        ev_.logged_in();
        loop(ws);
      }
      catch (const std::exception &e)
      {
        ready_ = false;
        if (!stop_)
          ev_.disconnected(e.what());
      }
    }

    // Reads until the event `name` (true) or an error event (false, reported).
    bool await_event(WsClient &ws, const std::string &name)
    {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (std::chrono::steady_clock::now() < end && !stop_)
      {
        auto f = ws.read_for(kPoll);
        if (!f || *f == "pong")
          continue;
        auto j = json::parse(*f);
        const std::string ev = j.value("event", "");
        if (ev == name && j.value("code", "0") == "0")
          return true;
        if (ev == "error" || (ev == name && j.value("code", "0") != "0"))
        {
          ev_.login_rejected(j.value("code", ""), j.value("msg", ""));
          return false;
        }
      }
      if (!stop_)
        ev_.disconnected("no " + name + " answer within 10 s");
      return false;
    }

    void loop(WsClient &ws)
    {
      auto last_rx = std::chrono::steady_clock::now();
      bool pinged = false;
      auto next_caa = std::chrono::steady_clock::now();
      while (!stop_)
      {
        std::deque<std::string> out;
        std::deque<Recovery> rec;
        bool was_busy = false;
        {
          std::lock_guard<std::mutex> l(mu_);
          out.swap(out_);
          rec.swap(recover_);
          was_busy = busy_;
          busy_ = false;
        }
        for (const auto &t : out)
          ws.send(t);
        if (was_busy)
          ev_.available();
        for (const auto &r : rec)
          recover_one(r);
        if (cfg_.cancel_all_after_s > 0 && std::chrono::steady_clock::now() >= next_caa)
        {
          // Dead man's switch: OKX cancels everything if this is not
          // refreshed within cancel_all_after_s (docs-v5, Cancel All After).
          auto r = rest("POST", "/api/v5/trade/cancel-all-after",
                        json{{"timeOut", std::to_string(cfg_.cancel_all_after_s)}}.dump());
          if (r.status != 200)
            ev_.disconnected("cancel-all-after refused: HTTP " + std::to_string(r.status));
          next_caa = std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.cancel_all_after_s / 3);
        }

        auto f = ws.read_for(kPoll);
        const auto now = std::chrono::steady_clock::now();
        if (!f)
        {
          if (!pinged && now - last_rx > kPingAfter)
          {
            ws.send("ping");
            pinged = true;
          }
          else if (pinged && now - last_rx > 2 * kPingAfter)
            throw std::runtime_error("no pong");
          continue;
        }
        last_rx = now;
        pinged = false;
        if (*f == "pong")
          continue;
        handle(json::parse(*f));
      }
      ready_ = false;
    }

    void handle(const json &j)
    {
      if (j.contains("op") && j.contains("id"))
        return ev_.op_result(j);
      const std::string ev = j.value("event", "");
      if (ev == "notice")
        throw std::runtime_error("OKX notice " + j.value("code", "") + ": " + j.value("msg", ""));
      if (ev == "error")
        throw std::runtime_error("OKX error " + j.value("code", "") + ": " + j.value("msg", ""));
      if (!ev.empty())
        return; // subscribe, channel-conn-count
      auto arg = j.find("arg");
      auto data = j.find("data");
      if (arg != j.end() && data != j.end() && arg->value("channel", "") == "orders")
        for (const auto &d : *data)
          ev_.order_update(d);
    }

    void recover_one(const Recovery &r)
    {
      std::string ord = r.ord_id;
      json state;
      {
        const std::string q = "/api/v5/trade/order?instId=" + r.inst_id +
                              (ord.empty() ? "&clOrdId=" + r.cl : "&ordId=" + ord);
        auto res = rest("GET", q);
        auto j = json::parse(res.body);
        if (j.value("code", "") != "0" || j["data"].empty())
          return; // unknown at OKX (never reached it); the OM keeps waiting
        state = j["data"][0];
        ord = state.value("ordId", ord);
      }
      std::string after;
      for (int page = 0; page < 20; ++page)
      {
        std::string q = "/api/v5/trade/fills?instId=" + r.inst_id + "&ordId=" + ord + "&limit=100";
        if (!after.empty())
          q += "&after=" + after;
        auto j = json::parse(rest("GET", q).body);
        if (j.value("code", "") != "0")
          break;
        const auto &d = j["data"];
        for (const auto &f : d)
        {
          json u = {{"instId", f.value("instId", "")}, {"ordId", f.value("ordId", "")},
                    {"clOrdId", f.value("clOrdId", r.cl)}, {"tradeId", f.value("tradeId", "")},
                    {"fillSz", f.value("fillSz", "0")},   {"fillPx", f.value("fillPx", "0")},
                    {"fillTime", f.value("fillTime", "")}, {"execType", f.value("execType", "")},
                    {"state", ""},                         {"source", "rest"}};
          ev_.order_update(u);
          after = f.value("billId", "");
        }
        if (d.size() < 100)
          break;
      }
      // The state last, with its own last-fill fields cleared: the fills
      // above already carry them.
      state["fillSz"] = "0";
      state["tradeId"] = "";
      state["source"] = "rest";
      ev_.order_update(state);
    }

    OkxOMConfig cfg_;
    SessionEvents &ev_;
    auth::Keys keys_;
    std::mutex mu_;
    std::deque<std::string> out_;
    std::deque<Recovery> recover_;
    std::atomic<bool> stop_{false}, ready_{false};
    bool busy_ = false; // under mu_
    std::thread thread_;
  };

  SessionFactory ws_session_factory()
  {
    return [](const OkxOMConfig &cfg, SessionEvents &ev) { return std::make_unique<OkxWsSession>(cfg, ev); };
  }
}
