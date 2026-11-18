/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeDeribit: a stand-in for Deribit's JSON-RPC WebSocket behind
// DeribitSession, driven by the test thread (which plays the session's
// thread).
//
// FakeDeribitServer keeps what Deribit would know about each order (order_id,
// trades, state) across connections, so recover() answers as the REST-style
// recovery calls would after a reconnect. Responses and notifications have
// the shapes of the documentation's examples (private/buy response,
// user.orders, user.trades).

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "deribit_oe/DeribitSession.hpp"

namespace deribit::oe::test
{
  using json = nlohmann::json;

  class FakeDeribitServer
  {
  public:
    std::atomic<bool> auto_login{true};
    std::atomic<bool> reject_login{false};
    std::atomic<CallStatus> call_status{CallStatus::Success};

    struct Req
    {
      int64_t id = 0;
      std::string method;
      json params;
      std::string label() const { return params.value("label", ""); }
    };

    std::vector<Req> placed()
    {
      std::lock_guard<std::mutex> l(mu_);
      return placed_;
    }
    std::vector<Req> cancels()
    {
      std::lock_guard<std::mutex> l(mu_);
      return cancels_;
    }
    std::vector<std::string> recoveries()
    {
      std::lock_guard<std::mutex> l(mu_);
      return recoveries_;
    }
    int sessions()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sessions_;
    }

    SessionFactory factory()
    {
      return [this](const DeribitOMConfig &, SessionEvents &ev) { return std::make_unique<Session>(*this, ev); };
    }

    // ---- driving, from the test thread ----

    // The buy/sell result: the order with `order_id` in `state`, and the
    // trades given (already made by fill(..., false)).
    void accept(const Req &r, const std::string &order_id, const std::string &state = "open",
                const std::vector<json> &trades = {})
    {
      std::lock_guard<std::mutex> l(mu_);
      auto &o = orders_[r.label()];
      o.req = r;
      o.order_id = order_id;
      o.state = state;
      for (const auto &t : trades)
        o.trades.push_back(t);
      if (ev_)
        ev_->rpc_result({{"jsonrpc", "2.0"},
                         {"id", r.id},
                         {"result", {{"order", order(r.label())}, {"trades", trades}}}});
    }

    void reject(const Req &r, int code, const std::string &message)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        ev_->rpc_result({{"jsonrpc", "2.0"}, {"id", r.id}, {"error", {{"code", code}, {"message", message}}}});
    }

    // private/cancel answered with the cancelled order.
    void cancel_ok(const Req &c, const std::string &label)
    {
      std::lock_guard<std::mutex> l(mu_);
      orders_[label].state = "cancelled";
      if (ev_)
        ev_->rpc_result({{"jsonrpc", "2.0"}, {"id", c.id}, {"result", order(label)}});
    }

    // cancel_by_label answered with a count.
    void cancel_count(const Req &c, int n)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        ev_->rpc_result({{"jsonrpc", "2.0"}, {"id", c.id}, {"result", n}});
    }

    void cancel_error(const Req &c, int code, const std::string &message)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        ev_->rpc_result({{"jsonrpc", "2.0"}, {"id", c.id}, {"error", {{"code", code}, {"message", message}}}});
    }

    // A user.orders notification with the order's new state.
    void state(const std::string &label, const std::string &st, const std::string &cancel_reason = "")
    {
      std::lock_guard<std::mutex> l(mu_);
      orders_[label].state = st;
      json o = order(label);
      if (!cancel_reason.empty())
        o["cancel_reason"] = cancel_reason;
      if (ev_ && up_)
        ev_->order_update(o);
    }

    // A trade on the order. delivered=false: made while the connection is
    // down (or to be returned in a response), so only recover() reports it.
    json fill(const std::string &label, double amount, double price, const std::string &trade_id,
              const std::string &state_after, bool delivered = true)
    {
      std::lock_guard<std::mutex> l(mu_);
      auto &o = orders_[label];
      o.state = state_after;
      json t = {{"trade_id", trade_id},
                {"trade_seq", 1966056},
                {"timestamp", 1795026561364},
                {"order_id", o.order_id},
                {"label", label},
                {"instrument_name", o.req.params.value("instrument_name", "BTC-PERPETUAL")},
                {"direction", o.req.method == "private/sell" ? "sell" : "buy"},
                {"amount", amount},
                {"price", price},
                {"state", state_after},
                {"liquidity", "M"},
                {"fee", 0},
                {"fee_currency", "BTC"}};
      o.trades.push_back(t);
      if (delivered && ev_ && up_)
        ev_->trade(t);
      return t;
    }

    void push_order(const json &o)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_ && up_)
        ev_->order_update(o);
    }
    void push_trade(const json &t)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_ && up_)
        ev_->trade(t);
    }

    void fail_connection()
    {
      std::lock_guard<std::mutex> l(mu_);
      up_ = false;
      if (ev_)
        ev_->disconnected("test: connection dropped");
    }

  private:
    struct Order
    {
      Req req;
      std::string order_id, state;
      std::vector<json> trades;
    };

    json order(const std::string &label)
    {
      const auto &o = orders_[label];
      return {{"order_id", o.order_id},
              {"order_state", o.state},
              {"order_type", "limit"},
              {"label", label},
              {"instrument_name", o.req.params.value("instrument_name", "")},
              {"direction", o.req.method == "private/sell" ? "sell" : "buy"},
              {"amount", o.req.params.value("amount", 0.0)},
              {"price", o.req.params.value("price", 0.0)},
              {"time_in_force", o.req.params.value("time_in_force", "")},
              {"api", true}};
    }

    class Session : public DeribitSession
    {
    public:
      Session(FakeDeribitServer &s, SessionEvents &ev) : s_(s)
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        s_.ev_ = &ev;
        s_.up_ = false;
        ++s_.sessions_;
      }
      ~Session() override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        s_.ev_ = nullptr;
        s_.up_ = false;
      }
      void logon() override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.reject_login)
          s_.ev_->login_rejected("13009", "unauthorized");
        else if (s_.auto_login)
        {
          s_.up_ = true;
          s_.ev_->logged_in();
        }
      }
      CallStatus call(int64_t id, const std::string &method, const json &params) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (!s_.up_ || !order_method(method))
          return CallStatus::Disconnected;
        if (s_.call_status != CallStatus::Success)
          return s_.call_status;
        Req r{id, method, params};
        if (method == "private/buy" || method == "private/sell")
        {
          s_.placed_.push_back(r);
          s_.orders_[r.label()].req = r;
        }
        else
          s_.cancels_.push_back(r);
        return CallStatus::Success;
      }
      CallStatus recover(const std::string &order_id, const std::string &label, const std::string &) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (!s_.up_)
          return CallStatus::Disconnected;
        s_.recoveries_.push_back(order_id.empty() ? "label:" + label : order_id);
        auto it = s_.orders_.find(label);
        if (it == s_.orders_.end() || it->second.order_id.empty())
          return CallStatus::Success;   // never reached Deribit
        for (const auto &t : it->second.trades)
          s_.ev_->trade(t);
        s_.ev_->order_update(s_.order(label));
        return CallStatus::Success;
      }
      void logout() override {}

    private:
      FakeDeribitServer &s_;
    };

    std::mutex mu_;
    SessionEvents *ev_ = nullptr;
    bool up_ = false;
    int sessions_ = 0;
    std::vector<Req> placed_, cancels_;
    std::vector<std::string> recoveries_;
    std::map<std::string, Order> orders_;   // by label
  };
}
