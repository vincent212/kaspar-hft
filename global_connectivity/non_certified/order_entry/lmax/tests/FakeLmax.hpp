/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeLmax: the test double for LmaxSession. It records every request and
// lets the test thread play LMAX: each call below delivers one event to the
// session's SessionEvents, as QuickFIX's thread would. Reports are built as
// LMAX's Broker Trading spec lists their fields.

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lmax_om/LmaxSession.hpp"

namespace lmax::om::test
{
  class FakeLmax
  {
  public:
    bool auto_logon = true;
    bool connected = true; // false: requests return Disconnected

    SessionFactory factory()
    {
      return [this](SessionEvents &ev) -> std::unique_ptr<LmaxSession> { return std::make_unique<Session>(*this, ev); };
    }

    // ---- what the OM sent ----
    std::vector<NewOrderReq> news() { std::lock_guard<std::mutex> l(mu_); return news_; }
    std::vector<CancelReq> cancels() { std::lock_guard<std::mutex> l(mu_); return cancels_; }
    std::vector<ReplaceReq> replaces() { std::lock_guard<std::mutex> l(mu_); return replaces_; }
    std::vector<StatusReq> statuses() { std::lock_guard<std::mutex> l(mu_); return statuses_; }
    std::vector<TcrReq> tcr_requests() { std::lock_guard<std::mutex> l(mu_); return tcrs_; }
    int sessions() { std::lock_guard<std::mutex> l(mu_); return sessions_; }

    // ---- LMAX's side ----
    void logon() { ev()->logged_on(); }
    void logout(const std::string &why = "") { ev()->logged_out(why); }

    ExecReport base(const std::string &clordid, char exec_type, char status)
    {
      ExecReport r;
      r.clordid = clordid;
      r.order_id = "OID" + clordid;
      r.exec_id = next_exec();
      r.secondary_exec_id = "0";
      r.exec_type = exec_type;
      r.ord_status = status;
      r.side = '1';
      r.security_id = "4001";
      r.transact_time = "20261118-10:00:00.000";
      return r;
    }
    void exec(const ExecReport &r) { ev()->exec(r); }
    void accepted(const std::string &clordid, double qty = 1.0)
    {
      auto r = base(clordid, '0', '0');
      r.order_id = "1" + clordid.substr(clordid.size() > 6 ? clordid.size() - 6 : 0);
      r.qty = qty;
      r.leaves = qty;
      exec(r);
    }
    ExecReport trade(const std::string &clordid, double last_qty, double last_px, double cum, double leaves)
    {
      auto r = base(clordid, 'F', leaves > 0 ? '1' : '2');
      r.has_last = true;
      r.last_qty = last_qty;
      r.last_px = last_px;
      r.cum = cum;
      r.leaves = leaves;
      r.avg_px = last_px;
      r.secondary_exec_id = "M" + r.exec_id;
      exec(r);
      return r;
    }
    void canceled(const std::string &clordid, const std::string &orig = {}, double cum = 0, char type = '4')
    {
      auto r = base(clordid, type, type == 'C' ? 'C' : '4');
      r.orig_clordid = orig;
      r.cum = cum;
      exec(r);
    }
    void replaced(const std::string &clordid, const std::string &orig, double qty, double cum = 0)
    {
      auto r = base(clordid, '5', cum > 0 ? '1' : '0');
      r.orig_clordid = orig;
      r.qty = qty;
      r.cum = cum;
      r.leaves = qty - cum;
      exec(r);
    }
    void rejected(const std::string &clordid, int reason = 99, const std::string &text = "")
    {
      auto r = base(clordid, '8', '8');
      r.rej_reason = reason;
      r.text = text;
      exec(r);
    }
    void status(const std::string &clordid, char ord_status, double cum, double avg_px)
    {
      auto r = base(clordid, 'I', ord_status);
      r.exec_id = "0"; // what an OrderStatus carries is not known (feed.md section 10)
      r.cum = cum;
      r.avg_px = avg_px;
      exec(r);
    }
    void cancel_reject(const std::string &clordid, const std::string &orig, char response_to, char status,
                       int reason)
    {
      CancelReject c;
      c.clordid = clordid;
      c.orig_clordid = orig;
      c.response_to = response_to;
      c.ord_status = status;
      c.reason = reason;
      c.order_id = "OID";
      ev()->cancel_reject(c);
    }
    void tcr(const TradeCaptureReport &r) { ev()->trade_capture(r); }
    void tcr_ack(const TcrAck &a) { ev()->tcr_ack(a); }
    void unparsed(const std::string &t) { ev()->unparsed(t); }

    SessionEvents *ev()
    {
      std::lock_guard<std::mutex> l(mu_);
      return ev_;
    }

  private:
    class Session : public LmaxSession
    {
    public:
      Session(FakeLmax &f, SessionEvents &ev) : f_(f)
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        f_.ev_ = &ev;
        ++f_.sessions_;
      }
      ~Session() override
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        f_.ev_ = nullptr;
      }
      void start() override
      {
        if (f_.auto_logon)
          f_.ev()->logged_on();
      }
      void stop() override {}
      CallStatus send_new(const NewOrderReq &r) override { return rec(f_.news_, r); }
      CallStatus send_cancel(const CancelReq &r) override { return rec(f_.cancels_, r); }
      CallStatus send_replace(const ReplaceReq &r) override { return rec(f_.replaces_, r); }
      CallStatus send_status(const StatusReq &r) override { return rec(f_.statuses_, r); }
      CallStatus send_tcr_request(const TcrReq &r) override { return rec(f_.tcrs_, r); }

    private:
      template <class V, class R>
      CallStatus rec(V &v, const R &r)
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        if (!f_.connected)
          return CallStatus::Disconnected;
        v.push_back(r);
        return CallStatus::Success;
      }
      FakeLmax &f_;
    };

    std::string next_exec()
    {
      char b[32];
      std::snprintf(b, sizeof b, "X%015d", ++exec_n_);
      return b;
    }

    std::mutex mu_;
    SessionEvents *ev_ = nullptr;
    int sessions_ = 0;
    int exec_n_ = 0;
    std::vector<NewOrderReq> news_;
    std::vector<CancelReq> cancels_;
    std::vector<ReplaceReq> replaces_;
    std::vector<StatusReq> statuses_;
    std::vector<TcrReq> tcrs_;
  };
}
