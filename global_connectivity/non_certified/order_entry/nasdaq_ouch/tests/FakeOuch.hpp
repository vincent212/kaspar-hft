/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeOuch: a stand-in for one Nasdaq OUCH port behind OuchSession, driven
// by the test.
//
// FakeOuchServer is the port for one trading day: it numbers every report
// as SoupBinTCP numbers Sequenced Data, keeps them in a log, and replays the
// log to a session that logs in at an earlier sequence number. It answers
// Account Query with the UserRefNum after the highest one ever entered, as
// Nasdaq would. Tests produce reports (accept, fill, cancel, reject, ...)
// from the test thread, which plays the session's thread.
//
// The reports are OUCH 5.0 messages in bytes, so OuchOM decodes them exactly
// as it decodes the real ones.

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "nasdaq_ouch/OuchCodec.hpp"
#include "nasdaq_ouch/OuchSession.hpp"

namespace nasdaq::ouch::test
{
  namespace from = from_nasdaq;

  class FakeOuchServer
  {
  public:
    // Behaviour knobs.
    std::atomic<bool> auto_login{true};
    std::atomic<char> login_reject{0}; // 'A' or 'S': reject the login
    std::atomic<bool> answer_account_query{true};
    std::atomic<CallStatus> enter_status{CallStatus::Success};
    std::atomic<CallStatus> cancel_status{CallStatus::Success};
    std::optional<uint64_t> replay_from_override; // log in at an earlier sequence than asked

    struct Entered
    {
      to_nasdaq::EnterOrder msg;
      std::string appendage;
    };
    struct Session
    {
      LogonParams logon;
      std::thread::id created_on;
    };

    std::vector<Entered> entered()
    {
      std::lock_guard<std::mutex> l(mu_);
      return entered_;
    }
    std::vector<to_nasdaq::CancelOrderRequest> cancels()
    {
      std::lock_guard<std::mutex> l(mu_);
      return cancels_;
    }
    std::vector<Session> sessions()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sessions_;
    }
    int account_queries()
    {
      std::lock_guard<std::mutex> l(mu_);
      return account_queries_;
    }
    uint64_t last_sequence()
    {
      std::lock_guard<std::mutex> l(mu_);
      return log_.size();
    }
    bool connected()
    {
      std::lock_guard<std::mutex> l(mu_);
      return live_ != nullptr;
    }

    // ---- reports -------------------------------------------------------------------------
    void accept(uint32_t urn, uint64_t ref = 77001, char state = 'L', std::optional<uint32_t> shares = {})
    {
      Entered e = find(urn);
      from::OrderAccepted a{};
      a.Timestamp = 34'200'000'000'000ULL; // 9:30
      a.UserRefNum = urn;
      a.Side = e.msg.Side;
      a.Quantity = shares ? *shares : uint32_t(e.msg.Quantity);
      std::memcpy(a.Symbol, e.msg.Symbol, sizeof(a.Symbol));
      a.Price = e.msg.Price;
      a.TimeInForce = e.msg.TimeInForce;
      a.Display = e.msg.Display;
      a.OrderReferenceNumber = ref;
      a.Capacity = e.msg.Capacity;
      a.InterMarketSweepEligibility = e.msg.InterMarketSweepEligibility;
      a.CrossType = e.msg.CrossType;
      a.OrderState = state;
      std::memcpy(a.ClOrdID, e.msg.ClOrdID, sizeof(a.ClOrdID));
      report(to_bytes(a));
    }
    void fill(uint32_t urn, uint32_t qty, uint64_t px, uint64_t match)
    {
      from::OrderExecuted e{};
      e.Timestamp = 34'260'000'000'000ULL; // 9:31
      e.UserRefNum = urn;
      e.Quantity = qty;
      e.Price = px;
      e.LiquidityFlag = 'A';
      e.MatchNumber = match;
      report(to_bytes(e));
    }
    void cancel(uint32_t urn, uint32_t qty, char reason = 'U')
    {
      from::OrderCanceled c{};
      c.Timestamp = 34'300'000'000'000ULL;
      c.UserRefNum = urn;
      c.Quantity = qty;
      c.Reason = reason;
      report(to_bytes(c));
    }
    void aiq_cancel(uint32_t urn, uint32_t qty)
    {
      from::AIQCanceled c{};
      c.UserRefNum = urn;
      c.DecrementShares = qty;
      c.Reason = 'Q';
      c.QuantityPreventedFromTrading = qty;
      c.LiquidityFlag = 'R';
      c.AIQStrategy = 'O';
      report(to_bytes(c));
    }
    void reject(uint32_t urn, uint16_t code)
    {
      from::Rejected r{};
      r.UserRefNum = urn;
      r.Reason = code;
      r.set_ClOrdID("x");
      report(to_bytes(r));
    }
    void cancel_reject(uint32_t urn)
    {
      from::CancelReject r{};
      r.UserRefNum = urn;
      report(to_bytes(r));
    }
    void cancel_pending(uint32_t urn)
    {
      from::CancelPending r{};
      r.UserRefNum = urn;
      report(to_bytes(r));
    }
    void broken(uint32_t urn, uint64_t match)
    {
      from::BrokenTrade b{};
      b.UserRefNum = urn;
      b.MatchNumber = match;
      b.Reason = 'E';
      b.set_ClOrdID("x");
      report(to_bytes(b));
    }
    void replaced(uint32_t orig, uint32_t urn)
    {
      from::OrderReplaced r{};
      r.OrigUserRefNum = orig;
      r.UserRefNum = urn;
      r.Side = 'B';
      r.Quantity = 100;
      r.set_Symbol("ZZZZ");
      r.Price = 10'0000;
      r.OrderState = 'L';
      report(to_bytes(r));
    }
    void system_event(char code)
    {
      from::SystemEvent s{};
      s.EventCode = code;
      report(to_bytes(s));
    }
    void raw(std::string bytes) { report(std::move(bytes)); }

    // ---- session state -------------------------------------------------------------------
    void busy() { with_live([](SessionEvents &ev) { ev.busy(); }); }
    void available() { with_live([](SessionEvents &ev) { ev.available(); }); }
    void end_of_session() { with_live([](SessionEvents &ev) { ev.end_of_session(); }); }

    // The connection drops: the session reports it and is dead.
    void fail_connection()
    {
      std::lock_guard<std::mutex> l(mu_);
      auto ev = dead_ ? nullptr : live_;
      dead_ = true;
      if (ev)
        ev->disconnected("connection reset by peer");
    }

    SessionFactory factory()
    {
      return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<OuchSession> {
        return std::make_unique<FakeSession>(*this, ev);
      };
    }

  private:
    class FakeSession : public OuchSession
    {
    public:
      FakeSession(FakeOuchServer &s, SessionEvents &ev) : s_(s), ev_(ev) {}
      ~FakeSession() override { s_.close(&ev_); }
      void logon(const LogonParams &p) override { s_.open(ev_, p); }
      CallStatus enter(const to_nasdaq::EnterOrder &m, std::string_view app) override { return s_.on_enter(&ev_, m, app); }
      CallStatus cancel(const to_nasdaq::CancelOrderRequest &m) override { return s_.on_cancel(&ev_, m); }
      CallStatus account_query() override { return s_.on_account_query(&ev_); }
      void logout() override { s_.close(&ev_); }

    private:
      FakeOuchServer &s_;
      SessionEvents &ev_;
    };

    void open(SessionEvents &ev, const LogonParams &p)
    {
      std::lock_guard<std::mutex> l(mu_);
      sessions_.push_back({p, std::this_thread::get_id()});
      if (!auto_login)
      {
        pending_ = &ev; // connected, never answered
        return;
      }
      if (login_reject)
      {
        ev.login_rejected(login_reject);
        return;
      }
      live_ = &ev;
      dead_ = false;
      uint64_t from = p.sequence == 0 ? log_.size() + 1 : p.sequence;
      if (replay_from_override)
        from = *replay_from_override;
      from = std::min<uint64_t>(from, log_.size() + 1);
      ev.logged_in("FAKESESS01", from);
      for (uint64_t s = from; s <= log_.size(); ++s)
        ev.message(s, log_[s - 1].data(), log_[s - 1].size());
    }

    void close(SessionEvents *ev)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_ == ev)
        live_ = nullptr;
      if (pending_ == ev)
        pending_ = nullptr;
    }

    bool usable(SessionEvents *ev) const { return live_ == ev && !dead_; }

    CallStatus on_enter(SessionEvents *ev, const to_nasdaq::EnterOrder &m, std::string_view app)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      auto s = enter_status.load();
      if (s == CallStatus::Success)
      {
        entered_.push_back({m, std::string(app)});
        highest_urn_ = std::max<uint32_t>(highest_urn_, m.UserRefNum);
      }
      return s;
    }

    CallStatus on_cancel(SessionEvents *ev, const to_nasdaq::CancelOrderRequest &m)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      auto s = cancel_status.load();
      if (s == CallStatus::Success)
        cancels_.push_back(m);
      return s;
    }

    CallStatus on_account_query(SessionEvents *ev)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      ++account_queries_;
      if (answer_account_query)
      {
        from::AccountQueryResponse q{};
        q.NextUserRefNum = highest_urn_ + 1;
        append(to_bytes(q));
      }
      return CallStatus::Success;
    }

    Entered find(uint32_t urn)
    {
      std::lock_guard<std::mutex> l(mu_);
      for (auto &e : entered_)
        if (uint32_t(e.msg.UserRefNum) == urn)
          return e;
      Entered none{};
      none.msg.set_Symbol("UNKNOWN");
      return none;
    }

    // Logged always; delivered only to a live session (otherwise it waits in
    // the log for the next login's replay). Under mu_.
    void append(std::string bytes)
    {
      log_.push_back(std::move(bytes));
      if (live_ && !dead_)
        live_->message(log_.size(), log_.back().data(), log_.back().size());
    }

    void report(std::string bytes)
    {
      std::lock_guard<std::mutex> l(mu_);
      append(std::move(bytes));
    }

    template <class F>
    void with_live(F f)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_ && !dead_)
        f(*live_);
    }

    std::mutex mu_;
    SessionEvents *live_ = nullptr;
    SessionEvents *pending_ = nullptr;
    bool dead_ = false;
    std::vector<std::string> log_; // sequence number = index + 1
    uint32_t highest_urn_ = 0;
    int account_queries_ = 0;
    std::vector<Entered> entered_;
    std::vector<to_nasdaq::CancelOrderRequest> cancels_;
    std::vector<Session> sessions_;
  };
}
