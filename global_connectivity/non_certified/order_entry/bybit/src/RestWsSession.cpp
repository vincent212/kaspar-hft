/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// RestWsSession: BybitSession over signed REST and the private WebSocket.
// NOT CERTIFIED; never connected to Bybit by the agent that wrote it (no
// testnet keys were available). Two threads:
//   stream  connects /v5/private, authenticates, subscribes to `order` and
//           `execution`, then delivers each data element; pings every 20 s
//   rest    sends queued requests in order over one keep-alive connection
//           and reports each answer; list answers (queries) are delivered as
//           order() / execution() events

#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_oe/BybitSession.hpp"

namespace bybit::oe
{

std::string PlaceRequest::body() const
{
  nlohmann::ordered_json j;
  j["category"] = category;
  j["symbol"] = symbol;
  j["side"] = side;
  j["orderType"] = order_type;
  j["qty"] = qty;
  if (!price.empty())
    j["price"] = price;
  j["timeInForce"] = time_in_force;
  j["orderLinkId"] = order_link_id;
  if (category == "linear")
    j["positionIdx"] = position_idx;
  if (reduce_only)
    j["reduceOnly"] = true;
  return j.dump();
}

std::string CancelRequest::body() const
{
  nlohmann::ordered_json j;
  j["category"] = category;
  j["symbol"] = symbol;
  j["orderLinkId"] = order_link_id;
  return j.dump();
}

namespace
{
  class RestWsSession : public BybitSession
  {
  public:
    RestWsSession(const SessionConfig &c, SessionEvents &ev) : cfg_(c), ev_(ev)
    {
      priv::guard(c.testnet, c.rest_host, c.ws_host);
    }

    ~RestWsSession() override { stop(); }

    void logon() override
    {
      rest_ = std::thread([this] { rest_loop(); });
      stream_ = std::thread([this] { stream_loop(); });
    }

    CallStatus place(const PlaceRequest &r) override
    {
      return enqueue({RequestKind::Place, r.order_link_id, "/v5/order/create", r.body(), true});
    }
    CallStatus cancel(const CancelRequest &r) override
    {
      return enqueue({RequestKind::Cancel, r.order_link_id, "/v5/order/cancel", r.body(), true});
    }
    CallStatus query_order(const std::string &cat, const std::string &link) override
    {
      return enqueue({RequestKind::QueryOrder, link, "/v5/order/realtime",
                      "category=" + cat + "&orderLinkId=" + link, false});
    }
    CallStatus query_executions(const std::string &cat, const std::string &link) override
    {
      return enqueue({RequestKind::QueryExecutions, link, "/v5/execution/list",
                      "category=" + cat + "&limit=100&orderLinkId=" + link, false});
    }
    void logout() override { stop(); }

  private:
    struct Req
    {
      RequestKind kind;
      std::string link;
      std::string path;
      std::string payload; // body (POST) or query string (GET)
      bool post;
    };

    SessionConfig cfg_;
    SessionEvents &ev_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Req> q_;
    std::atomic<bool> stopping_{false}, ready_{false}, down_{false};
    std::thread rest_, stream_;
    std::unique_ptr<priv::PrivateWs> ws_;
    std::mutex ws_mu_;

    CallStatus enqueue(Req r)
    {
      if (!ready_ || down_)
        return CallStatus::Disconnected;
      {
        std::lock_guard<std::mutex> l(mu_);
        if (q_.size() >= cfg_.max_queue)
          return CallStatus::Busy;
        q_.push_back(std::move(r));
      }
      cv_.notify_all();
      return CallStatus::Success;
    }

    void stop()
    {
      stopping_ = true;
      cv_.notify_all();
      {
        std::lock_guard<std::mutex> l(ws_mu_);
        if (ws_)
          ws_->close();
      }
      if (stream_.joinable())
        stream_.join();
      if (rest_.joinable())
        rest_.join();
    }

    void lost(const std::string &why)
    {
      bool was = down_.exchange(true);
      if (!was && !stopping_)
        ev_.disconnected(why);
    }

    void stream_loop()
    {
      try
      {
        {
          std::lock_guard<std::mutex> l(ws_mu_);
          ws_ = std::make_unique<priv::PrivateWs>(cfg_.credentials, cfg_.ws_host, cfg_.testnet);
        }
        auto err = ws_->open({"order", "execution"});
        if (!err.empty())
        {
          down_ = true;
          ev_.login_rejected(err);
          return;
        }
        ready_ = true;
        ev_.logged_in();
        while (!stopping_)
        {
          auto j = nlohmann::json::parse(ws_->read());
          const auto topic = j.value("topic", std::string());
          if (topic.rfind("order", 0) == 0)
            for (const auto &d : j.at("data"))
              ev_.order(d.dump());
          else if (topic.rfind("execution", 0) == 0)
            for (const auto &d : j.at("data"))
              ev_.execution(d.dump());
        }
      }
      catch (const std::exception &e)
      {
        lost(std::string("private stream: ") + e.what());
      }
    }

    void rest_loop()
    {
      std::unique_ptr<priv::RestClient> rc;
      try
      {
        rc = std::make_unique<priv::RestClient>(cfg_.credentials, cfg_.rest_host, cfg_.testnet,
                                                cfg_.recv_window_ms);
      }
      catch (const std::exception &e)
      {
        lost(std::string("REST: ") + e.what());
        return;
      }
      bool was_busy = false;
      while (true)
      {
        Req r;
        {
          std::unique_lock<std::mutex> l(mu_);
          cv_.wait(l, [&] { return stopping_ || !q_.empty(); });
          if (stopping_)
            return;
          r = std::move(q_.front());
          q_.pop_front();
          if (was_busy && q_.size() < cfg_.max_queue / 2)
          {
            was_busy = false;
            ev_.available();
          }
        }
        auto rep = r.post ? rc->post(r.path, r.payload) : rc->get(r.path, r.payload);
        if (rep.ret_code == 10006)
        {
          was_busy = true;
          ev_.busy();
        }
        if (r.kind == RequestKind::QueryOrder || r.kind == RequestKind::QueryExecutions)
        {
          if (rep.ok() && rep.result.contains("list"))
            for (const auto &d : rep.result["list"])
              r.kind == RequestKind::QueryOrder ? ev_.order(d.dump()) : ev_.execution(d.dump());
        }
        ev_.response(r.kind, r.link, rep.http == 200 ? rep.ret_code : -1, rep.ret_msg);
      }
    }
  };
}

SessionFactory rest_ws_session_factory()
{
  return [](const SessionConfig &c, SessionEvents &ev) -> std::unique_ptr<BybitSession> {
    return std::make_unique<RestWsSession>(c, ev);
  };
}

}
