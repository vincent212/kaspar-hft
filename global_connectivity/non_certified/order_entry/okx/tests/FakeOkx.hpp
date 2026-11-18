/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeOkx: a stand-in for OKX's private WebSocket behind OkxSession, driven
// by the test thread (which plays the session's thread).
//
// FakeOkxServer keeps what OKX would know about each order (ordId, fills,
// state) across connections, so recover() answers as REST would after a
// reconnect. Responses and updates have the shapes of the V5 documentation's
// examples (WS / Place order, WS / Cancel order, WS / Order channel).

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "okx_oe/OkxSession.hpp"

namespace okx::oe::test
{
  using json = nlohmann::json;

  class FakeOkxServer
  {
  public:
    std::atomic<bool> auto_login{true};
    std::atomic<bool> reject_login{false};
    std::atomic<CallStatus> place_status{CallStatus::Success};
    std::atomic<CallStatus> cancel_status{CallStatus::Success};

    struct Req
    {
      std::string id;
      json arg;
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
      return [this](const OkxOMConfig &, SessionEvents &ev) { return std::make_unique<Session>(*this, ev); };
    }

    // ---- driving, from the test thread ----

    void login()
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
      {
        up_ = true;
        ev_->logged_in();
      }
    }

    // op "order" succeeded: ordId assigned.
    void accept(const Req &r, const std::string &ord_id)
    {
      std::lock_guard<std::mutex> l(mu_);
      const std::string cl = r.arg.value("clOrdId", "");
      orders_[cl].ord_id = ord_id;
      orders_[cl].arg = r.arg;
      if (orders_[cl].state.empty())
        orders_[cl].state = "live";
      if (ev_)
        ev_->op_result({{"id", r.id},
                        {"op", "order"},
                        {"code", "0"},
                        {"msg", ""},
                        {"data", json::array({{{"clOrdId", cl}, {"ordId", ord_id}, {"sCode", "0"}, {"sMsg", ""}}})}});
    }

    void op_reject(const Req &r, const std::string &scode, const std::string &smsg = "rejected")
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        ev_->op_result(
            {{"id", r.id},
             {"op", "order"},
             {"code", "1"},
             {"msg", ""},
             {"data", json::array({{{"clOrdId", r.arg.value("clOrdId", "")}, {"ordId", ""}, {"sCode", scode}, {"sMsg", smsg}}})}});
    }

    void op_format_error(const Req &r)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        ev_->op_result({{"id", r.id}, {"op", "order"}, {"code", "60013"}, {"msg", "Invalid args"}, {"data", json::array()}});
    }

    void cancel_result(const Req &r, const std::string &scode)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        ev_->op_result({{"id", r.id},
                        {"op", "cancel-order"},
                        {"code", scode == "0" ? "0" : "1"},
                        {"msg", ""},
                        {"data", json::array({{{"clOrdId", r.arg.value("clOrdId", "")},
                                               {"ordId", r.arg.value("ordId", "")},
                                               {"sCode", scode},
                                               {"sMsg", scode == "0" ? "" : "Cancellation failed"}}})}});
    }

    // An orders-channel push with a state change.
    void state(const std::string &cl, const std::string &st, const std::string &cancel_source = "")
    {
      std::lock_guard<std::mutex> l(mu_);
      orders_[cl].state = st;
      json u = base(cl);
      if (!cancel_source.empty())
        u["cancelSource"] = cancel_source;
      if (ev_ && up_)
        ev_->order_update(u);
    }

    // A fill. delivered=false: it happens while the connection is down, so
    // only recover() reports it.
    void fill(const std::string &cl, const std::string &sz, const std::string &px, const std::string &trade_id,
              const std::string &st, bool delivered = true)
    {
      std::lock_guard<std::mutex> l(mu_);
      auto &o = orders_[cl];
      o.state = st;
      json u = base(cl);
      u["fillSz"] = sz;
      u["fillPx"] = px;
      u["tradeId"] = trade_id;
      u["fillTime"] = "1795026561364";
      u["execType"] = "M";
      o.fills.push_back(u);
      if (delivered && ev_ && up_)
        ev_->order_update(u);
    }

    // Any orders-channel element, as given.
    void push(const json &u)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_ && up_)
        ev_->order_update(u);
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
      json arg = json::object();
      std::string ord_id, state;
      std::vector<json> fills;
    };

    json base(const std::string &cl)
    {
      const auto &o = orders_[cl];
      return {{"instId", o.arg.value("instIdCode", int64_t(0)) == 3 ? "BTC-USDT" : "BTC-USDT-SWAP"},
              {"ordId", o.ord_id},
              {"clOrdId", cl},
              {"state", o.state},
              {"fillSz", "0"},
              {"tradeId", ""},
              {"accFillSz", "0"}};
    }

    class Session : public OkxSession
    {
    public:
      Session(FakeOkxServer &s, SessionEvents &ev) : s_(s)
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
          s_.ev_->login_rejected("60009", "Login failed.");
        else if (s_.auto_login)
        {
          s_.up_ = true;
          s_.ev_->logged_in();
        }
      }
      CallStatus place(const std::string &id, const json &arg) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (!s_.up_)
          return CallStatus::Disconnected;
        if (s_.place_status != CallStatus::Success)
          return s_.place_status;
        s_.placed_.push_back({id, arg});
        return CallStatus::Success;
      }
      CallStatus cancel(const std::string &id, const json &arg) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (!s_.up_)
          return CallStatus::Disconnected;
        if (s_.cancel_status != CallStatus::Success)
          return s_.cancel_status;
        s_.cancels_.push_back({id, arg});
        return CallStatus::Success;
      }
      CallStatus recover(const std::string &, const std::string &, const std::string &cl) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (!s_.up_)
          return CallStatus::Disconnected;
        s_.recoveries_.push_back(cl);
        auto it = s_.orders_.find(cl);
        if (it == s_.orders_.end())
          return CallStatus::Success;
        for (const auto &f : it->second.fills)
        {
          json r = f;
          r["state"] = "";
          r["source"] = "rest";
          s_.ev_->order_update(r);
        }
        json st = s_.base(cl);
        st["source"] = "rest";
        s_.ev_->order_update(st);
        return CallStatus::Success;
      }
      void logout() override {}

    private:
      FakeOkxServer &s_;
    };

    std::mutex mu_;
    SessionEvents *ev_ = nullptr;
    bool up_ = false;
    int sessions_ = 0;
    std::vector<Req> placed_, cancels_;
    std::vector<std::string> recoveries_;
    std::map<std::string, Order> orders_;
  };
}
