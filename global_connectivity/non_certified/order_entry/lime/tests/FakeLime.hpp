/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeLime: a stand-in for one Lime account's trading session behind
// LimeSession, driven by the test.
//
// FakeLimeServer is the Lime platform for one trading day: it numbers every
// event it produces with an eventId, keeps them, and replays from the
// eventId a new session is constructed with (TO 3: 0 none, 1 the whole
// day). Tests produce events (accept, fill, cancel, reject, ...) from the
// test thread, which plays the API's callback thread. Delivery happens under
// the server's lock, and a session's destructor takes it, so nothing is
// delivered to a session after its destructor returns.

#include <atomic>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "lime_om/LimeApi.hpp"

namespace lime::test
{
  class FakeLimeServer
  {
  public:
    std::atomic<bool> auto_login{true};
    std::string login_failure;  // non-empty: answer the login with login_failed(this)
    std::atomic<CallStatus> place_status{CallStatus::Success};
    std::atomic<CallStatus> cancel_status{CallStatus::Success};
    std::atomic<bool> throw_on_place{false};
    std::atomic<bool> throw_on_construct{false};
    std::atomic<uint64_t> replay_from_override{0}; // non-zero: replay from here, whatever was asked

    struct Session
    {
      LoginParams login;
      std::thread::id created_on;
    };

    std::vector<NewOrder> orders()
    {
      std::lock_guard<std::mutex> l(mu_);
      return orders_;
    }
    std::vector<OrderId> cancels()
    {
      std::lock_guard<std::mutex> l(mu_);
      return cancels_;
    }
    std::vector<Session> sessions()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sessions_;
    }
    uint64_t last_event_id()
    {
      std::lock_guard<std::mutex> l(mu_);
      return log_.size();
    }
    bool connected()
    {
      std::lock_guard<std::mutex> l(mu_);
      return live_ != nullptr;
    }

    // ---- session events --------------------------------------------------------------
    void login()
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_)
        do_login(*live_);
    }
    void busy() { session_event([](SessionEvents &e) { e.busy(); }); }
    void available() { session_event([](SessionEvents &e) { e.available(); }); }
    // The connection drops: the session is dead (TO 3).
    void connection_failed(const std::string &why = "fake: connection reset")
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!live_)
        return;
      live_->connection_failed(why);
      live_ = nullptr;
    }
    // Events Lime numbers while no session is connected (missed; replayed on
    // the next login from the right eventId).
    void disconnect_silently()
    {
      std::lock_guard<std::mutex> l(mu_);
      live_ = nullptr;
    }

    // ---- trading events --------------------------------------------------------------
    void accept(OrderId id, OrderId lime_id = 9'000'000'001ULL)
    {
      emit([=](SessionEvents &e, uint64_t n) { e.accepted(id, lime_id, n); });
    }
    void fill(OrderId id, int qty, ScaledPrice px, int left, const std::string &exec,
              ExecType t = ExecType::Fill)
    {
      FillInfo f;
      f.symbol = "MSFT";
      f.side = Side::Buy;
      f.last_shares = qty;
      f.last_price = px;
      f.left_qty = left;
      f.liquidity = 1;
      f.transact_time_ms = 1'700'000'000'123ULL;
      f.transact_time_ns = 1'700'000'000'123'456'789ULL;
      f.exec_type = t;
      f.exec_id = exec;
      f.last_market = "XNAS";
      emit([=](SessionEvents &e, uint64_t n) { e.filled(id, f, n); });
    }
    void cancel(OrderId id, const std::string &reason = "")
    {
      emit([=](SessionEvents &e, uint64_t n) { e.canceled(id, reason, n); });
    }
    void reject(OrderId id, const std::string &reason = "fake: rejected")
    {
      emit([=](SessionEvents &e, uint64_t n) { e.rejected(id, reason, n); });
    }
    // A reject generated in the API itself: eventId 0, never replayed.
    void api_reject(OrderId id, const std::string &reason = "fake: API reject")
    {
      session_event([=](SessionEvents &e) { e.rejected(id, reason, 0); });
    }
    void cancel_reject(OrderId id, const std::string &reason = "fake: too late to cancel")
    {
      emit([=](SessionEvents &e, uint64_t n) { e.cancel_rejected(id, reason, n); });
    }
    void partial_cancel(OrderId id, int left)
    {
      emit([=](SessionEvents &e, uint64_t n) { e.partial_canceled(id, left, n); });
    }
    void replace(OrderId id, OrderId rid)
    {
      emit([=](SessionEvents &e, uint64_t n) { e.replaced(id, rid, rid + 1, n); });
    }
    void replace_reject(OrderId id, OrderId rid)
    {
      emit([=](SessionEvents &e, uint64_t n) { e.replace_rejected(id, rid, "fake", n); });
    }
    void manual_order(OrderId id)
    {
      emit([=](SessionEvents &e, uint64_t n) {
        e.manual_order(id, "MSFT", "INET", 100, 4000000, Side::Sell, n);
      });
    }
    void manual_replace(OrderId id, OrderId rid)
    {
      emit([=](SessionEvents &e, uint64_t n) { e.manual_replace(id, rid, 50, 4000100, n); });
    }
    // Deliver event n again, with its own eventId (a duplicate).
    void redeliver(uint64_t n)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_ && logged_in_ && n >= 1 && n <= log_.size())
        log_[n - 1](*live_, n);
    }

    // ---- the API ----------------------------------------------------------------------
    SessionFactory factory()
    {
      return [this](const LoginParams &p, SessionEvents &ev) -> std::unique_ptr<LimeSession> {
        if (throw_on_construct)
          throw std::runtime_error("fake: TradingApiException");
        std::lock_guard<std::mutex> l(mu_);
        sessions_.push_back({p, std::this_thread::get_id()});
        live_ = &ev;
        live_login_ = p;
        logged_in_ = false;
        if (auto_login)
          do_login(ev);
        return std::make_unique<FakeSession>(*this, &ev);
      };
    }

  private:
    class FakeSession : public LimeSession
    {
    public:
      FakeSession(FakeLimeServer &s, SessionEvents *ev) : s_(s), ev_(ev) {}
      ~FakeSession() override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.live_ == ev_)
          s_.live_ = nullptr;
      }
      CallStatus place(const NewOrder &o) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.throw_on_place)
          throw std::runtime_error("fake: TradingApiException on placeOrder");
        if (s_.live_ != ev_ || !s_.logged_in_)
          return CallStatus::ConnectionError;
        auto st = s_.place_status.load();
        if (st == CallStatus::Success)
          s_.orders_.push_back(o);
        return st;
      }
      CallStatus cancel(OrderId id) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.live_ != ev_ || !s_.logged_in_)
          return CallStatus::ConnectionError;
        auto st = s_.cancel_status.load();
        if (st == CallStatus::Success)
          s_.cancels_.push_back(id);
        return st;
      }
      CallStatus cancel_all() override { return CallStatus::Success; }

    private:
      FakeLimeServer &s_;
      SessionEvents *ev_;
    };

    void do_login(SessionEvents &ev) // under mu_
    {
      if (!login_failure.empty())
      {
        ev.login_failed(login_failure);
        live_ = nullptr;
        return;
      }
      logged_in_ = true;
      ev.login_accepted(log_.size());
      uint64_t from = replay_from_override ? replay_from_override.load() : live_login_.event_id;
      if (from > 0)
        for (uint64_t n = from; n <= log_.size(); ++n)
          log_[n - 1](ev, n);
    }
    void emit(std::function<void(SessionEvents &, uint64_t)> f)
    {
      std::lock_guard<std::mutex> l(mu_);
      log_.push_back(f);
      if (live_ && logged_in_)
        f(*live_, log_.size());
    }
    void session_event(std::function<void(SessionEvents &)> f)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_)
        f(*live_);
    }

    std::mutex mu_;
    SessionEvents *live_ = nullptr;
    LoginParams live_login_;
    bool logged_in_ = false;
    std::vector<std::function<void(SessionEvents &, uint64_t)>> log_; // eventId = index + 1
    std::vector<NewOrder> orders_;
    std::vector<OrderId> cancels_;
    std::vector<Session> sessions_;
  };
}
