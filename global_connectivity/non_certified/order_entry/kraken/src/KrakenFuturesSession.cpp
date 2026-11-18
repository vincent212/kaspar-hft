/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The Kraken Futures session (KrakenSessions.hpp). NEVER RUN against Kraken by
// its author: no demo keys were supplied (README.md, "Testing ladder").
//
// One thread: it sends queued sendorder and cancelorder requests, and every
// poll_ms reads GET /fills and POST /orders/status for the orders it tracks.
// Tracking and the fills already reported live in a Tracker shared by every
// session the factory makes, so a session made after a reconnect goes on
// polling the orders the last one placed, and reports a fill made while
// disconnected exactly once.
//
// Rate budget (exchange/guides/futures/ratelimits: 500 per 10 s): sendorder
// and cancelorder 10 each, fills (no lastFillTime) 2, orders/status 1,
// cancelallordersafter 25. Polling at 1 s spends 30 per 10 s.

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "kraken_md/KrakenJson.hpp"
#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_oe/KrakenFuturesCodec.hpp"
#include "kraken_oe/KrakenSessions.hpp"

namespace kraken::oe
{
  using json = nlohmann::json;

  HttpFn real_http()
  {
    return [](const std::string &method, const std::string &host, const std::string &target, const HttpHeaders &h,
              const std::string &body) { return kraken::https_request(method, host, target, h, body); };
  }

  HttpHeaders futures_headers(const std::string &api_key, const std::string &secret_b64, const std::string &method,
                              const std::string &endpoint_path, const std::string &post_data, const std::string &nonce)
  {
    HttpHeaders h{{"APIKey", api_key},
                  {"Nonce", nonce},
                  {"Authent", futures_authent(post_data, nonce, endpoint_path, secret_b64)}};
    if (method == "POST")
      h.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    return h;
  }

  namespace
  {
    constexpr size_t kMaxQueued = 64;
    constexpr int kAbsentPolls = 3; // polls an order may be missing from orders/status before it is ended

    uint64_t now_ns()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    struct Tracked
    {
      std::string order_id;
      std::string last_status; // last status reported, to report changes only
      int absent = 0;
    };

    // Shared by the sessions of one factory.
    struct Tracker
    {
      std::mutex mu;
      std::map<std::string, Tracked> orders; // cl_ord_id -> state, until it ends
      std::set<std::string> fills_seen;      // fill_ids reported (or history)
      uint64_t since_ns = 0;
    };

    struct Request
    {
      bool cancel = false;
      NewOrder order;
      std::string order_id, cl_ord_id;
    };

    class FuturesSession : public KrakenSession
    {
    public:
      FuturesSession(const Settings &s, HttpFn http, std::shared_ptr<Tracker> t, SessionEvents &ev)
          : s_(s), http_(std::move(http)), t_(std::move(t)), ev_(ev)
      {
      }
      ~FuturesSession() override
      {
        stop_ = true;
        if (thread_.joinable())
          thread_.join();
      }

      void logon() override { thread_ = std::thread([this] { run(); }); }

      CallStatus add_order(const NewOrder &o) override
      {
        Request r;
        r.order = o;
        r.cl_ord_id = o.cl_ord_id;
        return enqueue(std::move(r));
      }

      CallStatus cancel_order(const std::string &order_id, const std::string &cl_ord_id) override
      {
        Request r;
        r.cancel = true;
        r.order_id = order_id;
        r.cl_ord_id = cl_ord_id;
        return enqueue(std::move(r));
      }

      void logout() override { stop_ = true; }

    private:
      CallStatus enqueue(Request r)
      {
        if (!ready_)
          return CallStatus::Disconnected;
        std::lock_guard<std::mutex> l(mu_);
        if (queue_.size() >= kMaxQueued)
        {
          if (!busy_)
          {
            busy_ = true;
            ev_.busy();
          }
          return CallStatus::Busy;
        }
        queue_.push_back(std::move(r));
        return CallStatus::Success;
      }

      // One signed request. Throws on a network failure or a host other than
      // the configured one.
      json call(const std::string &method, const std::string &endpoint, const std::string &params)
      {
        std::string err;
        if (!s_.ep.check(err))
          throw std::runtime_error(err);
        const std::string path = "/derivatives" + endpoint;
        const std::string nonce = std::to_string(next_nonce());
        auto h = futures_headers(keys_.api_key, keys_.api_secret, method, endpoint, params, nonce);
        const std::string target = method == "GET" && !params.empty() ? path + "?" + params : path;
        auto r = http_(method, s_.ep.rest_host, target, h, method == "GET" ? std::string() : params);
        if (r.status == 401 || r.status == 403)
          return json{{"result", "error"}, {"error", "HTTP " + std::to_string(r.status)}};
        if (r.status != 200)
          throw std::runtime_error(endpoint + ": HTTP " + std::to_string(r.status));
        return kraken::parse_raw(r.body);
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
          // Logon: one authenticated read. A key or permission error ends
          // here; anything else is a lost connection and is retried.
          auto r = call("GET", "/api/v3/fills", "");
          if (auto e = futures::error_of(r); !e.empty())
          {
            if (e.find("auth") != std::string::npos || e.find("HTTP 40") != std::string::npos ||
                e.find("apiKey") != std::string::npos)
              return ev_.login_failed("fills: " + e);
            return ev_.disconnected("fills: " + e);
          }
          on_fills(r);
          ready_ = true;
          ev_.logged_in();
          loop();
        }
        catch (const std::exception &e)
        {
          ready_ = false;
          if (!stop_)
            ev_.disconnected(e.what());
        }
      }

      void loop()
      {
        auto next_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds(s_.poll_ms);
        auto next_dms = std::chrono::steady_clock::now();
        while (!stop_)
        {
          std::deque<Request> q;
          bool was_busy;
          {
            std::lock_guard<std::mutex> l(mu_);
            q.swap(queue_);
            was_busy = busy_;
            busy_ = false;
          }
          for (const auto &r : q)
            r.cancel ? do_cancel(r) : do_send(r);
          if (was_busy)
            ev_.available();
          const auto now = std::chrono::steady_clock::now();
          if (s_.cancel_after_s > 0 && now >= next_dms)
          {
            // Dead man's switch: Kraken cancels every order if this is not
            // renewed within the timeout.
            auto r = call("POST", "/api/v3/cancelallordersafter", "timeout=" + std::to_string(s_.cancel_after_s));
            if (auto e = futures::error_of(r); !e.empty())
              throw std::runtime_error("cancelallordersafter: " + e);
            next_dms = now + std::chrono::seconds(std::max(1, s_.cancel_after_s / 3));
          }
          if (now >= next_poll)
          {
            poll();
            next_poll = now + std::chrono::milliseconds(s_.poll_ms);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ready_ = false;
      }

      void do_send(const Request &rq)
      {
        {
          std::lock_guard<std::mutex> l(t_->mu);
          t_->orders[rq.cl_ord_id]; // tracked from now: orders/status by cliOrdId finds it if the answer is lost
        }
        auto resp = call("POST", "/api/v3/sendorder", futures::send_order_body(rq.order));
        for (auto &r : futures::parse_send(resp, rq.cl_ord_id))
        {
          {
            std::lock_guard<std::mutex> l(t_->mu);
            auto &tr = t_->orders[rq.cl_ord_id];
            if (!r.order_id.empty())
              tr.order_id = r.order_id;
            tr.last_status = r.kind == Report::Accepted ? "ENTERED_BOOK" : "REJECTED";
            if (r.kind == Report::Rejected)
              t_->orders.erase(rq.cl_ord_id);
          }
          ev_.report(r);
        }
      }

      void do_cancel(const Request &rq)
      {
        auto resp = call("POST", "/api/v3/cancelorder", futures::cancel_order_body(rq.order_id, rq.cl_ord_id));
        for (auto &r : futures::parse_cancel(resp, rq.cl_ord_id))
        {
          if (r.kind == Report::Canceled)
          {
            // The fills made before the cancel come first.
            poll_fills();
            std::lock_guard<std::mutex> l(t_->mu);
            t_->orders.erase(rq.cl_ord_id);
          }
          ev_.report(r);
        }
      }

      void on_fills(const json &resp)
      {
        auto fills = futures::parse_fills(resp);
        // Oldest first.
        std::sort(fills.begin(), fills.end(), [](const Report &a, const Report &b) { return a.time_ns < b.time_ns; });
        std::vector<Report> fresh;
        {
          std::lock_guard<std::mutex> l(t_->mu);
          for (auto &f : fills)
          {
            if (f.exec_id.empty() || !t_->fills_seen.insert(f.exec_id).second)
              continue;
            if (f.time_ns && f.time_ns < t_->since_ns)
              continue; // history: before this process
            fresh.push_back(f);
          }
        }
        for (const auto &f : fresh)
          ev_.report(f);
      }

      void poll_fills()
      {
        auto r = call("GET", "/api/v3/fills", "");
        if (auto e = futures::error_of(r); !e.empty())
          throw std::runtime_error("fills: " + e);
        on_fills(r);
      }

      void poll()
      {
        poll_fills();
        std::vector<std::string> ids, cls;
        {
          std::lock_guard<std::mutex> l(t_->mu);
          for (auto &[cl, tr] : t_->orders)
            (tr.order_id.empty() ? cls : ids).push_back(tr.order_id.empty() ? cl : tr.order_id);
        }
        if (ids.empty() && cls.empty())
          return;
        auto r = call("POST", "/api/v3/orders/status", futures::orders_status_body(ids, cls));
        if (auto e = futures::error_of(r); !e.empty())
          throw std::runtime_error("orders/status: " + e);
        auto rows = futures::parse_orders_status(r);
        std::vector<Report> out;
        {
          std::lock_guard<std::mutex> l(t_->mu);
          std::set<std::string> present;
          for (auto &row : rows)
          {
            // Find the tracked order by client id, else by venue id.
            auto it = row.cl_ord_id.empty() ? t_->orders.end() : t_->orders.find(row.cl_ord_id);
            if (it == t_->orders.end())
              for (auto j = t_->orders.begin(); j != t_->orders.end(); ++j)
                if (!row.order_id.empty() && j->second.order_id == row.order_id)
                  it = j;
            if (it == t_->orders.end())
              continue;
            present.insert(it->first);
            if (row.cl_ord_id.empty())
              row.cl_ord_id = it->first;
            it->second.order_id = row.order_id.empty() ? it->second.order_id : row.order_id;
            it->second.absent = 0;
            if (row.status != it->second.last_status)
            {
              it->second.last_status = row.status;
              out.push_back(row);
            }
            if (row.kind == Report::Canceled || row.kind == Report::Rejected || row.kind == Report::Filled)
              t_->orders.erase(it);
          }
          // orders/status returns open orders and those ended in the last
          // 5 s. One absent for kAbsentPolls polls ended earlier; the fills
          // already polled carry what it executed.
          for (auto it = t_->orders.begin(); it != t_->orders.end();)
          {
            if (present.count(it->first) || ++it->second.absent < kAbsentPolls)
            {
              ++it;
              continue;
            }
            Report e;
            e.cl_ord_id = it->first;
            e.order_id = it->second.order_id;
            e.kind = it->second.order_id.empty() ? Report::Rejected : Report::Canceled;
            e.reason = "absent from orders/status for " + std::to_string(kAbsentPolls) + " polls";
            out.push_back(e);
            it = t_->orders.erase(it);
          }
        }
        for (const auto &o : out)
          ev_.report(o);
      }

      Settings s_;
      HttpFn http_;
      std::shared_ptr<Tracker> t_;
      SessionEvents &ev_;
      Keys keys_;
      std::mutex mu_;
      std::deque<Request> queue_;
      bool busy_ = false; // under mu_
      std::atomic<bool> stop_{false}, ready_{false};
      std::thread thread_;
    };
  }

  SessionFactory futures_session_factory(const Settings &s, HttpFn http, uint64_t since_ns)
  {
    auto t = std::make_shared<Tracker>();
    t->since_ns = since_ns ? since_ns : now_ns();
    return [s, http, t](SessionEvents &ev) -> std::unique_ptr<KrakenSession> {
      return std::make_unique<FuturesSession>(s, http, t, ev);
    };
  }
}
