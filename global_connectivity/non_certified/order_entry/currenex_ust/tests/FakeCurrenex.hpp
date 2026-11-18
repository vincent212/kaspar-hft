/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeCurrenex: a stand-in for one Currenex UST OUCH session behind
// CxSession, driven by the test.
//
// FakeCurrenexServer is the persistent session for one trading week: it
// numbers every outbound message (the Logon response and Heartbeats
// included), keeps them in a log, and answers a ResendRequest by replaying
// the log up to the current Logon response, with a GapFill in place of each
// run of Logon and Heartbeat messages (UST OUCH 5.5). With persistence off,
// every Logon starts the numbering at 1 again. Tests produce reports (ack,
// trade, cancel, reject, ...) from the test thread, which plays the
// session's thread.
//
// The reports are Currenex messages in bytes, so CurrenexOM decodes them
// exactly as it decodes the real ones.

#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "currenex_ust/CxCodec.hpp"
#include "currenex_ust/CxSession.hpp"

namespace currenex::ouch::test
{
  class FakeCurrenexServer
  {
  public:
    // Behaviour knobs.
    std::atomic<bool> auto_login{true};
    std::string logon_logout;            // non-empty: answer the Logon with a Logout with this reason
    std::atomic<bool> persistence{true};
    std::atomic<bool> answer_instruments{true};
    std::atomic<CallStatus> order_status{CallStatus::Success};
    std::atomic<CallStatus> cancel_status{CallStatus::Success};
    std::optional<uint32_t> resend_from_override; // replay from here, whatever was asked
    std::vector<std::pair<std::string, int16_t>> instruments{{"91282CJY8", 7}, {"91282CKA8", 3}};
    std::vector<std::pair<std::string, int32_t>> routed_users;
    int32_t session_id = 1307;

    struct Session
    {
      LogonParams logon;
      std::thread::id created_on;
    };

    std::vector<to::NewOrder> orders()
    {
      std::lock_guard<std::mutex> l(mu_);
      return orders_;
    }
    std::vector<to::OrderCancelRequest> cancels()
    {
      std::lock_guard<std::mutex> l(mu_);
      return cancels_;
    }
    std::vector<Session> sessions()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sessions_;
    }
    std::vector<uint32_t> resends()
    {
      std::lock_guard<std::mutex> l(mu_);
      return resends_;
    }
    int instrument_requests()
    {
      std::lock_guard<std::mutex> l(mu_);
      return instrument_requests_;
    }
    int routing_requests()
    {
      std::lock_guard<std::mutex> l(mu_);
      return routing_requests_;
    }
    uint32_t last_sequence()
    {
      std::lock_guard<std::mutex> l(mu_);
      return uint32_t(log_.size());
    }
    bool connected()
    {
      std::lock_guard<std::mutex> l(mu_);
      return live_ != nullptr;
    }

    // ---- reports -------------------------------------------------------------------------
    void ack(int32_t clid, int64_t order_id = 26'000'001)
    {
      from::NewOrderAck a{};
      a.ClOrderID = clid;
      a.OrderID = order_id;
      a.Status = 'C';
      a.ErrorCode = 0;
      a.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(a));
    }
    void reject(int32_t clid, int16_t code)
    {
      from::NewOrderAck a{};
      a.ClOrderID = clid;
      a.OrderID = -1;
      a.Status = 'R';
      a.ErrorCode = code;
      a.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(a));
    }
    // Amounts in dollars, rates as decimals.
    void trade(int32_t clid, double amt, double rate, const std::string &exec, double leaves,
               int64_t transact_ms = 1'700'000'000'123LL)
    {
      from::Trade t{};
      t.ClOrderID = clid;
      t.OrderID = 26'000'001;
      t.InstrumentIndex = 7;
      t.Side = 'B';
      t.FillAmt = cents(amt);
      t.FillRate = rate9(rate);
      t.set_ExecBroker("NA");
      t.set_ExecutionID(exec);
      t.ExecType = '1';
      t.TransactTime = transact_ms;
      t.LeavesAmt = cents(leaves);
      t.set_SecurityID("91282CJY8");
      t.PriceType = '1';
      t.AggressorFlag = '2';
      t.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(t));
    }
    void pending(int32_t clid, int32_t link, double amt, double rate)
    {
      from::PendingFill u{};
      u.ClOrderID = clid;
      u.OrderID = 26'000'001;
      u.TradeLinkID = link;
      u.FillAmt = cents(amt);
      u.FillRate = rate9(rate);
      u.AggressorFlag = '2';
      u.set_ExecBroker("NA");
      u.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(u));
    }
    void pending_cancel(int32_t clid, int32_t link, double amt, double rate)
    {
      from::PendingFillCancel v{};
      v.ClOrderID = clid;
      v.OrderID = 26'000'001;
      v.TradeLinkID = link;
      v.FillAmt = cents(amt);
      v.FillRate = rate9(rate);
      v.AggressorFlag = '2';
      v.set_ExecBroker("NA");
      v.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(v));
    }
    void pending_trade(int32_t clid, int32_t link, double amt, double rate, const std::string &exec, double leaves)
    {
      from::PendingFillTrade t{};
      t.ClOrderID = clid;
      t.OrderID = 26'000'001;
      t.TradeLinkID = link;
      t.InstrumentIndex = 7;
      t.Side = 'B';
      t.FillAmt = cents(amt);
      t.FillRate = rate9(rate);
      t.set_ExecBroker("NA");
      t.set_ExecutionID(exec);
      t.ExecType = '1';
      t.TransactTime = 1'700'000'000'123LL;
      t.LeavesAmt = cents(leaves);
      t.set_SecurityID("91282CJY8");
      t.PriceType = '1';
      t.AggressorFlag = '1';
      t.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(t));
    }
    void spread_trade(int32_t clid, double amt)
    {
      from::SpreadTrade f{};
      f.ClOrderID = clid;
      f.FillAmt = cents(amt);
      f.set_ExecutionID("SPRD1");
      f.PriceType = '4';
      report(to_bytes(f));
    }
    void cancelled(int32_t clid, char status = 'C', int16_t type = 0)
    {
      from::OrderCancelledOrExpired r{};
      r.ClOrderID = clid;
      r.OrderID = 26'000'001;
      r.Status = status;
      r.Type = type;
      r.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(r));
    }
    void cancel_reject(int32_t new_clid, int32_t prev_clid, int16_t code)
    {
      from::OrderCancelReject o{};
      o.NewClOrderID = new_clid;
      o.PrevClOrderID = prev_clid;
      o.ErrorCode = code;
      o.RoutedUserKey = NOT_ROUTED;
      report(to_bytes(o));
    }
    void replace_ack(int32_t new_clid, int32_t prev_clid, char status)
    {
      from::OrderReplaceAck q{};
      q.NewClOrderID = new_clid;
      q.PrevClOrderID = prev_clid;
      q.Status = status;
      report(to_bytes(q));
    }
    void heartbeat()
    {
      from::Heartbeat h{};
      h.SessionID = session_id;
      std::lock_guard<std::mutex> l(mu_);
      append(to_bytes(h), true);
    }
    void gap_fill_now(uint32_t new_seq, char reason)
    {
      // A GapFill delivered at the next number without being logged.
      from::GapFill g{};
      g.NewSeqNo = int32_t(new_seq);
      g.ReasonCode = reason;
      std::lock_guard<std::mutex> l(mu_);
      auto b = to_bytes(g);
      if (live_ && !dead_)
        live_->message(uint32_t(log_.size() + 1), 0, b.data(), b.size());
    }
    void raw(std::string bytes) { report(std::move(bytes)); }

    // ---- session state -------------------------------------------------------------------
    void busy() { with_live([](SessionEvents &ev) { ev.busy(); }); }
    void available() { with_live([](SessionEvents &ev) { ev.available(); }); }
    // Currenex logs the session out (UST OUCH 5.2).
    void logout(const std::string &reason)
    {
      std::lock_guard<std::mutex> l(mu_);
      auto ev = dead_ ? nullptr : live_;
      dead_ = true;
      if (ev)
        ev->logged_out(reason, false);
    }
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
      return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<CxSession> {
        return std::make_unique<FakeSession>(*this, ev);
      };
    }

  private:
    struct Logged
    {
      std::string bytes;
      bool admin;
    };

    class FakeSession : public CxSession
    {
    public:
      FakeSession(FakeCurrenexServer &s, SessionEvents &ev) : s_(s), ev_(ev) {}
      ~FakeSession() override { s_.close(&ev_); }
      void logon(const LogonParams &p) override { s_.open(ev_, p); }
      CallStatus new_order(const to::NewOrder &m) override { return s_.on_order(&ev_, m); }
      CallStatus cancel(const to::OrderCancelRequest &m) override { return s_.on_cancel(&ev_, m); }
      CallStatus instrument_info_request() override { return s_.on_instruments(&ev_); }
      CallStatus routing_user_info_request() override { return s_.on_routing(&ev_); }
      CallStatus resend(uint32_t begin) override { return s_.on_resend(&ev_, begin); }
      void logout() override { s_.close(&ev_); }

    private:
      FakeCurrenexServer &s_;
      SessionEvents &ev_;
    };

    static int64_t cents(double d) { return int64_t(std::llround(d * AMOUNT_SCALE)); }
    static int64_t rate9(double d) { return int64_t(std::llround(d * RATE_SCALE)); }

    void open(SessionEvents &ev, const LogonParams &p)
    {
      std::lock_guard<std::mutex> l(mu_);
      sessions_.push_back({p, std::this_thread::get_id()});
      if (!auto_login)
      {
        pending_ = &ev; // connected, never answered
        return;
      }
      if (!logon_logout.empty())
      {
        ev.logged_out(logon_logout, true);
        return;
      }
      live_ = &ev;
      dead_ = false;
      if (!persistence)
        log_.clear();
      from::Logon a{};
      a.set_UserID(p.user);
      a.SessionID = session_id;
      a.Version = VERSION;
      log_.push_back({to_bytes(a), true});
      logon_seq_ = uint32_t(log_.size());
      ev.logged_in(session_id, logon_seq_);
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

    CallStatus on_order(SessionEvents *ev, const to::NewOrder &m)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      auto s = order_status.load();
      if (s == CallStatus::Success)
        orders_.push_back(m);
      return s;
    }

    CallStatus on_cancel(SessionEvents *ev, const to::OrderCancelRequest &m)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      auto s = cancel_status.load();
      if (s == CallStatus::Success)
        cancels_.push_back(m);
      return s;
    }

    CallStatus on_instruments(SessionEvents *ev)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      ++instrument_requests_;
      if (answer_instruments)
        for (auto &[cusip, idx] : instruments)
        {
          from::OutrightInstrumentInfo d{};
          d.SessionID = session_id;
          d.InstrumentIndex = idx;
          d.InstrumentType = '3';
          d.set_InstrumentID("OTR-" + cusip.substr(7));
          d.set_SecurityID(cusip);
          d.SecurityType = '2';
          d.set_InstrumentState("OTR");
          d.set_InstrumentTenor("10Y");
          append(to_bytes(d));
        }
      return CallStatus::Success;
    }

    CallStatus on_routing(SessionEvents *ev)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      ++routing_requests_;
      for (auto &[id, key] : routed_users)
      {
        from::RoutingUserInfo g{};
        g.SessionID = session_id;
        g.set_RoutedUserId(id);
        g.RoutedUserKey = key;
        append(to_bytes(g));
      }
      return CallStatus::Success;
    }

    CallStatus on_resend(SessionEvents *ev, uint32_t begin)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!usable(ev))
        return CallStatus::Disconnected;
      resends_.push_back(begin);
      uint32_t s = std::max<uint32_t>(resend_from_override.value_or(begin), 1);
      while (s < logon_seq_)
      {
        if (log_[s - 1].admin)
        {
          uint32_t t = s;
          while (t < logon_seq_ && log_[t - 1].admin)
            ++t;
          from::GapFill g{};
          g.NewSeqNo = int32_t(t);
          g.ReasonCode = '1';
          auto b = to_bytes(g);
          live_->message(s, 0, b.data(), b.size());
          s = t;
          continue;
        }
        live_->message(s, 0, log_[s - 1].bytes.data(), log_[s - 1].bytes.size());
        ++s;
      }
      return CallStatus::Success;
    }

    // Logged always; delivered only to a live session (otherwise it waits in
    // the log for a resend). Under mu_.
    void append(std::string bytes, bool admin = false)
    {
      log_.push_back({std::move(bytes), admin});
      if (live_ && !dead_)
        live_->message(uint32_t(log_.size()), 0, log_.back().bytes.data(), log_.back().bytes.size());
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
    std::vector<Logged> log_; // sequence number = index + 1
    uint32_t logon_seq_ = 0;
    int instrument_requests_ = 0;
    int routing_requests_ = 0;
    std::vector<to::NewOrder> orders_;
    std::vector<to::OrderCancelRequest> cancels_;
    std::vector<Session> sessions_;
    std::vector<uint32_t> resends_;
  };
}
