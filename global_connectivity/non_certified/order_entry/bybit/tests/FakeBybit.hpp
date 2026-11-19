#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FakeBybit: the test double for BybitSession. Records every request; the
// test plays Bybit by calling order(), execution(), response() and the
// connection events, which reach the current session's receiver.

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "bybit_oe/BybitSession.hpp"

namespace bybit::oe::test
{
  class FakeBybit
  {
  public:
    struct Call
    {
      RequestKind kind;
      std::string category, link;
      PlaceRequest place;
    };

    bool auto_login = true;
    bool refuse_calls = false; // calls return Busy
    int sessions = 0;

    SessionFactory factory()
    {
      return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<BybitSession> {
        std::lock_guard<std::mutex> l(mu_);
        ++sessions;
        ev_ = &ev;
        return std::make_unique<Session>(*this, ev);
      };
    }

    std::vector<Call> calls()
    {
      std::lock_guard<std::mutex> l(mu_);
      return calls_;
    }
    std::vector<Call> calls(RequestKind k)
    {
      std::vector<Call> v;
      for (auto &c : calls())
        if (c.kind == k)
          v.push_back(c);
      return v;
    }

    // Bybit's side.
    void logged_in() { with([](SessionEvents &e) { e.logged_in(); }); }
    void disconnect() { with([](SessionEvents &e) { e.disconnected("test"); }); }
    void busy() { with([](SessionEvents &e) { e.busy(); }); }
    void available() { with([](SessionEvents &e) { e.available(); }); }
    void order(const nlohmann::json &j) { with([&](SessionEvents &e) { e.order(j.dump()); }); }
    void execution(const nlohmann::json &j) { with([&](SessionEvents &e) { e.execution(j.dump()); }); }
    void response(RequestKind k, const std::string &link, int code, const std::string &m = "")
    {
      with([&](SessionEvents &e) { e.response(k, link, code, m); });
    }

  private:
    class Session : public BybitSession
    {
    public:
      Session(FakeBybit &f, SessionEvents &ev) : f_(f), ev_(ev) {}
      ~Session() override
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        if (f_.ev_ == &ev_)
          f_.ev_ = nullptr;
      }
      void logon() override
      {
        if (f_.auto_login)
          ev_.logged_in();
      }
      CallStatus place(const PlaceRequest &r) override { return rec({RequestKind::Place, r.category, r.order_link_id, r}); }
      CallStatus cancel(const CancelRequest &r) override { return rec({RequestKind::Cancel, r.category, r.order_link_id, {}}); }
      CallStatus query_order(const std::string &c, const std::string &l) override
      {
        return rec({RequestKind::QueryOrder, c, l, {}});
      }
      CallStatus query_executions(const std::string &c, const std::string &l) override
      {
        return rec({RequestKind::QueryExecutions, c, l, {}});
      }
      void logout() override {}

    private:
      CallStatus rec(Call c)
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        if (f_.refuse_calls)
          return CallStatus::Busy;
        f_.calls_.push_back(std::move(c));
        return CallStatus::Success;
      }
      FakeBybit &f_;
      SessionEvents &ev_;
    };

    template <class F>
    void with(F f)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (ev_)
        f(*ev_);
    }

    std::mutex mu_;
    SessionEvents *ev_ = nullptr;
    std::vector<Call> calls_;
  };

  // Bybit messages in the documentation's field names.
  inline nlohmann::json order_update(const std::string &link, const std::string &status,
                                     const std::string &cum = "0", const std::string &order_id = "1321003749386327552",
                                     const std::string &reject = "EC_NoError",
                                     const std::string &cancel_type = "UNKNOWN")
  {
    return {{"category", "linear"}, {"symbol", "BTCUSDT"}, {"orderId", order_id}, {"orderLinkId", link},
            {"orderStatus", status}, {"cumExecQty", cum}, {"rejectReason", reject},
            {"cancelType", cancel_type}, {"side", "Buy"}, {"price", "63000.0"}, {"qty", "0.005"}};
  }

  inline nlohmann::json exec(const std::string &link, const std::string &exec_id, const std::string &qty,
                             const std::string &px = "63000.0", const std::string &type = "Trade",
                             const std::string &order_id = "1321003749386327552")
  {
    return {{"category", "linear"}, {"symbol", "BTCUSDT"}, {"orderId", order_id}, {"orderLinkId", link},
            {"side", "Buy"}, {"execId", exec_id}, {"execPrice", px}, {"execQty", qty},
            {"execType", type}, {"execTime", "1795027104327"}, {"isMaker", true},
            {"leavesQty", "0"}, {"seq", 1}};
  }
}
