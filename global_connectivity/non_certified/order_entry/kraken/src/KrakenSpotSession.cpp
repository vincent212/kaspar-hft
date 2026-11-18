/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The Kraken spot session (KrakenSessions.hpp): WebSocket v2 on
// ws-auth.kraken.com. PRODUCTION ONLY (Kraken has no spot testnet) and
// therefore NEVER RUN by its author; it is compiled and its messages are
// tested (tests/test_spot_codec.cpp), nothing more.
//
//   1. POST /0/private/GetWebSocketsToken (API-Key, API-Sign) -> token
//   2. connect wss://ws-auth.kraken.com/v2; subscribe executions with
//      snap_orders and snap_trades (open orders and the last 50 trades)
//   3. add_order / cancel_order with req_id; responses and the executions
//      channel become Reports (KrakenSpotCodec)
//   4. executions `sequence` checked for gaps; a gap ends the session, and the
//      next one starts with a fresh snapshot
//   5. optional cancel_all_orders_after, renewed every timeout/3
//
// One thread owns the WebSocket; the actor thread only queues requests.

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_oe/KrakenSessions.hpp"
#include "kraken_oe/KrakenSpotCodec.hpp"
#include "kraken_oe/WsConn.hpp"

namespace kraken::oe
{
  using json = nlohmann::json;

  namespace
  {
    constexpr size_t kMaxQueued = 64;
    constexpr auto kPoll = std::chrono::milliseconds(50);
    constexpr auto kPingAfter = std::chrono::seconds(20);

    class SpotSession : public KrakenSession
    {
    public:
      SpotSession(const Settings &s, HttpFn http, SessionEvents &ev) : s_(s), http_(std::move(http)), ev_(ev) {}
      ~SpotSession() override
      {
        stop_ = true;
        if (thread_.joinable())
          thread_.join();
      }

      void logon() override { thread_ = std::thread([this] { run(); }); }

      CallStatus add_order(const NewOrder &o) override
      {
        if (!ready_)
          return CallStatus::Disconnected;
        std::lock_guard<std::mutex> l(mu_);
        const uint64_t id = ++req_;
        req_cl_[id] = o.cl_ord_id;
        return enqueue_locked(spot::add_order(o, token_, id));
      }

      CallStatus cancel_order(const std::string &order_id, const std::string &cl_ord_id) override
      {
        if (!ready_)
          return CallStatus::Disconnected;
        std::lock_guard<std::mutex> l(mu_);
        const uint64_t id = ++req_;
        req_cl_[id] = cl_ord_id;
        return enqueue_locked(spot::cancel_order(order_id, cl_ord_id, token_, id));
      }

      void logout() override { stop_ = true; }

    private:
      CallStatus enqueue_locked(std::string text)
      {
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

      bool get_token(std::string &err)
      {
        const std::string path = "/0/private/GetWebSocketsToken";
        const std::string nonce = std::to_string(next_nonce());
        const std::string body = "nonce=" + nonce;
        HttpHeaders h{{"API-Key", keys_.api_key},
                      {"API-Sign", spot_api_sign(path, nonce, body, keys_.api_secret)},
                      {"Content-Type", "application/x-www-form-urlencoded"}};
        auto r = http_("POST", s_.ep.rest_host, path, h, body);
        if (r.status != 200)
        {
          err = "GetWebSocketsToken: HTTP " + std::to_string(r.status);
          return false;
        }
        auto j = json::parse(r.body);
        if (j.contains("error") && !j["error"].empty())
        {
          err = "GetWebSocketsToken: " + j["error"].dump();
          return false;
        }
        std::lock_guard<std::mutex> l(mu_);
        token_ = j.at("result").at("token").get<std::string>();
        return true;
      }

      void run()
      {
        std::string err;
        if (!s_.ep.check(err))
          return ev_.login_failed(err);
        if (!load_keys(s_.key_file, keys_, err))
          return ev_.login_failed(err);
        try
        {
          if (!get_token(err))
            return ev_.login_failed(err);
          WsConn ws;
          ws.connect(s_.ep.ws_host, "/v2");
          std::string tok;
          {
            std::lock_guard<std::mutex> l(mu_);
            tok = token_;
          }
          ws.send(spot::subscribe_executions(tok, true, true));
          auto lookup = [this](uint64_t id) {
            std::lock_guard<std::mutex> l(mu_);
            auto it = req_cl_.find(id);
            return it == req_cl_.end() ? std::string() : it->second;
          };
          auto last_rx = std::chrono::steady_clock::now();
          auto next_dms = last_rx;
          bool pinged = false;
          while (!stop_)
          {
            std::deque<std::string> out;
            bool was_busy;
            {
              std::lock_guard<std::mutex> l(mu_);
              out.swap(out_);
              was_busy = busy_;
              busy_ = false;
            }
            for (const auto &t : out)
              ws.send(t);
            if (was_busy)
              ev_.available();
            const auto now = std::chrono::steady_clock::now();
            if (ready_ && s_.cancel_after_s > 0 && now >= next_dms)
            {
              ws.send(spot::cancel_after(s_.cancel_after_s, tok));
              next_dms = now + std::chrono::seconds(std::max(1, s_.cancel_after_s / 3));
            }
            auto f = ws.read_for(kPoll);
            if (!f)
            {
              if (!pinged && now - last_rx > kPingAfter)
              {
                ws.send(R"({"method":"ping"})");
                pinged = true;
              }
              else if (pinged && now - last_rx > 2 * kPingAfter)
                throw std::runtime_error("no answer to ping");
              continue;
            }
            last_rx = std::chrono::steady_clock::now();
            pinged = false;
            auto p = spot::parse(kraken::parse_raw(*f), lookup);
            if (!p.subscribe_error.empty())
              return ev_.login_failed("subscribe executions: " + p.subscribe_error);
            if (p.subscribed_executions && !ready_)
            {
              ready_ = true;
              ev_.logged_in();
            }
            if (p.sequence)
            {
              if (last_seq_ && *p.sequence != last_seq_ + 1)
              {
                ready_ = false;
                ev_.gap(last_seq_ + 1, *p.sequence);
                return;
              }
              last_seq_ = *p.sequence;
            }
            for (const auto &r : p.reports)
              ev_.report(r);
          }
          ready_ = false;
          ws.close();
        }
        catch (const std::exception &e)
        {
          ready_ = false;
          if (!stop_)
            ev_.disconnected(e.what());
        }
      }

      Settings s_;
      HttpFn http_;
      SessionEvents &ev_;
      Keys keys_;
      std::mutex mu_;
      std::string token_;                      // under mu_
      std::deque<std::string> out_;            // under mu_
      std::map<uint64_t, std::string> req_cl_; // under mu_
      uint64_t req_ = 0;                       // under mu_
      bool busy_ = false;                      // under mu_
      uint64_t last_seq_ = 0;
      std::atomic<bool> stop_{false}, ready_{false};
      std::thread thread_;
    };
  }

  SessionFactory spot_session_factory(const Settings &s, HttpFn http)
  {
    return [s, http](SessionEvents &ev) -> std::unique_ptr<KrakenSession> {
      return std::make_unique<SpotSession>(s, http, ev);
    };
  }
}
