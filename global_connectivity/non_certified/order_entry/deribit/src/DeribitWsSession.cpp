/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitWsSession: the real DeribitSession over Deribit's JSON-RPC
// WebSocket. NEVER RUN against Deribit by its author: no test keys were
// available (README.md, "Testing ladder").
//
// One thread owns the WebSocket: it authenticates, subscribes, sets the
// heartbeat, then alternates between writing queued requests and reading for
// up to 50 ms. Requests from the actor thread only go into the queue.
// Keep-alive: the server's test_request is answered with public/test; three
// heartbeat intervals without a frame is a lost connection. The connection
// is re-authenticated (public/auth again) at 80% of the token's lifetime.

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "deribit_oe/DeribitConfig.hpp"
#include "deribit_oe/DeribitSession.hpp"
#include "deribit_oe/RpcWs.hpp"
#include "deribit_oe/Signing.hpp"

namespace deribit::oe
{
  using json = nlohmann::json;

  namespace
  {
    constexpr size_t kMaxQueued = 512;
    constexpr auto kPoll = std::chrono::milliseconds(50);
    constexpr int64_t kInternal = int64_t(1) << 40;   // ids of the session's own requests

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

  class DeribitWsSession : public DeribitSession
  {
  public:
    DeribitWsSession(const DeribitOMConfig &cfg, SessionEvents &ev) : cfg_(cfg), ev_(ev) {}
    ~DeribitWsSession() override
    {
      stop_ = true;
      if (thread_.joinable())
        thread_.join();
    }

    void logon() override { thread_ = std::thread([this] { run(); }); }

    CallStatus call(int64_t id, const std::string &method, const json &params) override
    {
      if (!order_method(method) || id >= kInternal)
        return CallStatus::Disconnected;
      return enqueue(request(id, method, params).dump());
    }

    CallStatus recover(const std::string &order_id, const std::string &label, const std::string &currency) override
    {
      if (!ready_)
        return CallStatus::Disconnected;
      std::lock_guard<std::mutex> l(mu_);
      recover_.push_back({order_id, label, currency});
      return CallStatus::Success;
    }

    void logout() override { stop_ = true; }

  private:
    struct Recovery
    {
      std::string order_id, label, currency;
    };

    // A recovery request in flight.
    struct Step
    {
      enum Kind { BY_LABEL, TRADES, STATE } kind;
      std::string order_id;
      json order;   // TRADES after BY_LABEL: the state to emit after the trades
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

    void send_auth(RpcWs &ws, int64_t id)
    {
      const std::string nonce = make_nonce();
      ws.send(request(id, "public/auth", auth_params(keys_.client_id, keys_.client_secret, epoch_ms(), nonce)).dump());
    }

    // Reads until the response to `id`; other frames are handled as usual.
    // Returns the response; throws when none comes within 10 s.
    json await_result(RpcWs &ws, int64_t id)
    {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (std::chrono::steady_clock::now() < end && !stop_)
      {
        auto f = ws.read_for(kPoll);
        if (!f)
          continue;
        auto j = json::parse(*f);
        if (j.contains("id") && j["id"].is_number_integer() && j["id"].get<int64_t>() == id)
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
      if (!load_keys(cfg_.key_file, keys_, err))
        return ev_.login_rejected("keys", err);
      try
      {
        RpcWs ws(cfg_.ep.host);
        ws.connect();
        send_auth(ws, next_internal_++);
        auto a = await_result(ws, next_internal_ - 1);
        if (a.contains("error"))
          return ev_.login_rejected(std::to_string(a["error"].value("code", 0)), a["error"].value("message", ""));
        schedule_reauth(a["result"]);
        const int64_t sid = next_internal_++;
        ws.send(request(sid, "private/subscribe",
                        {{"channels", json::array({"user.orders.any.any.raw", "user.trades.any.any.raw"})}})
                    .dump());
        auto s = await_result(ws, sid);
        if (s.contains("error"))
          return ev_.login_rejected(std::to_string(s["error"].value("code", 0)), s["error"].value("message", ""));
        const int64_t hid = next_internal_++;
        ws.send(request(hid, "public/set_heartbeat", {{"interval", cfg_.heartbeat_s}}).dump());
        await_result(ws, hid);
        if (cfg_.cancel_on_disconnect)
        {
          const int64_t cid = next_internal_++;
          ws.send(request(cid, "private/enable_cancel_on_disconnect", {{"scope", "connection"}}).dump());
          auto c = await_result(ws, cid);
          if (c.contains("error"))
            return ev_.login_rejected(std::to_string(c["error"].value("code", 0)), c["error"].value("message", ""));
        }
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

    void schedule_reauth(const json &result)
    {
      const int64_t s = result.value("expires_in", int64_t(0));
      if (s > 0 && s < 365 * 86400)
        reauth_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(s * 8 / 10);
      else
        reauth_at_ = std::chrono::steady_clock::time_point::max();
    }

    void loop(RpcWs &ws)
    {
      auto last_rx = std::chrono::steady_clock::now();
      const auto silence = std::chrono::seconds(3 * cfg_.heartbeat_s);
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
          start_recovery(ws, r);
        const auto now = std::chrono::steady_clock::now();
        if (now >= reauth_at_)
        {
          reauth_id_ = next_internal_++;
          send_auth(ws, reauth_id_);
          reauth_at_ = std::chrono::steady_clock::time_point::max();
        }
        auto f = ws.read_for(kPoll);
        if (!f)
        {
          if (std::chrono::steady_clock::now() - last_rx > silence)
            throw std::runtime_error("no frame within three heartbeat intervals");
          continue;
        }
        last_rx = std::chrono::steady_clock::now();
        handle(ws, json::parse(*f));
      }
      ready_ = false;
    }

    void handle(RpcWs &ws, const json &j)
    {
      const std::string method = j.value("method", "");
      if (method == "heartbeat")
      {
        if (j.contains("params") && j["params"].value("type", "") == "test_request")
          ws.send(request(next_internal_++, "public/test", json::object()).dump());
        return;
      }
      if (method == "subscription")
      {
        const auto &p = j["params"];
        const std::string ch = p.value("channel", "");
        const json &d = p["data"];
        if (ch.rfind("user.orders.", 0) == 0)
        {
          if (d.is_array())
            for (const auto &o : d)
              ev_.order_update(o);
          else
            ev_.order_update(d);
        }
        else if (ch.rfind("user.trades.", 0) == 0)
        {
          if (d.is_array())
            for (const auto &t : d)
              ev_.trade(t);
          else
            ev_.trade(d);
        }
        return;
      }
      if (!j.contains("id") || !j["id"].is_number_integer())
        return;
      const int64_t id = j["id"].get<int64_t>();
      if (id < kInternal)
        return ev_.rpc_result(j);
      if (id == reauth_id_)
      {
        if (j.contains("error"))
          throw std::runtime_error("re-authentication refused: " + j["error"].value("message", ""));
        schedule_reauth(j["result"]);
        return;
      }
      auto it = steps_.find(id);
      if (it == steps_.end())
        return;   // public/test and other answers
      Step st = std::move(it->second);
      steps_.erase(it);
      on_step(ws, st, j);
    }

    void start_recovery(RpcWs &ws, const Recovery &r)
    {
      if (r.order_id.empty())
      {
        const int64_t id = next_internal_++;
        steps_[id] = {Step::BY_LABEL, "", json()};
        ws.send(request(id, "private/get_order_state_by_label", {{"currency", r.currency}, {"label", r.label}}).dump());
        return;
      }
      trades_of(ws, r.order_id, json());
    }

    void trades_of(RpcWs &ws, const std::string &order_id, json order)
    {
      const int64_t id = next_internal_++;
      steps_[id] = {Step::TRADES, order_id, std::move(order)};
      ws.send(request(id, "private/get_user_trades_by_order", {{"order_id", order_id}}).dump());
    }

    void on_step(RpcWs &ws, Step &st, const json &j)
    {
      if (j.contains("error"))
        return;   // the order is unknown to Deribit (never reached it); the OM keeps waiting
      const json &res = j["result"];
      switch (st.kind)
      {
      case Step::BY_LABEL:
        if (res.is_array())
          for (const auto &o : res)
            if (!o.value("order_id", "").empty())
              trades_of(ws, o["order_id"].get<std::string>(), o);
        return;
      case Step::TRADES:
        if (res.is_array())
          for (const auto &t : res)
            ev_.trade(t);
        if (st.order.is_object())
          ev_.order_update(st.order);
        else
        {
          const int64_t id = next_internal_++;
          steps_[id] = {Step::STATE, st.order_id, json()};
          ws.send(request(id, "private/get_order_state", {{"order_id", st.order_id}}).dump());
        }
        return;
      case Step::STATE:
        if (res.is_object())
          ev_.order_update(res);
        return;
      }
    }

    DeribitOMConfig cfg_;
    SessionEvents &ev_;
    Keys keys_;
    std::mutex mu_;
    std::deque<std::string> out_;
    std::deque<Recovery> recover_;
    std::atomic<bool> stop_{false}, ready_{false};
    bool busy_ = false;   // under mu_
    // session thread only
    int64_t next_internal_ = kInternal;
    int64_t reauth_id_ = -1;
    std::chrono::steady_clock::time_point reauth_at_ = std::chrono::steady_clock::time_point::max();
    std::map<int64_t, Step> steps_;
    std::thread thread_;
  };

  SessionFactory ws_session_factory()
  {
    return [](const DeribitOMConfig &cfg, SessionEvents &ev) { return std::make_unique<DeribitWsSession>(cfg, ev); };
  }
}
