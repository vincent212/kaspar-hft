/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeClearStreet: the test double for CsSession. It records every request
// and lets the test thread play Clear Street: each call below delivers one
// event to the session's SessionEvents, as QuickFIX's thread would. Reports
// carry the fields the FIX API guide lists for ExecutionReport (35=8).

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "clearstreet_om/CsSession.hpp"

namespace clearstreet::om::test
{
  class FakeClearStreet
  {
  public:
    bool auto_logon = true;
    std::string logon_text = "SIM1";
    bool connected = true; // false: requests return Disconnected

    SessionFactory factory()
    {
      return [this](SessionEvents &ev) -> std::unique_ptr<CsSession> { return std::make_unique<Session>(*this, ev); };
    }

    // ---- what the OM sent ----
    std::vector<NewOrderReq> news() { std::lock_guard<std::mutex> l(mu_); return news_; }
    std::vector<CancelReq> cancels() { std::lock_guard<std::mutex> l(mu_); return cancels_; }
    std::vector<ReplaceReq> replaces() { std::lock_guard<std::mutex> l(mu_); return replaces_; }
    int sessions() { std::lock_guard<std::mutex> l(mu_); return sessions_; }

    // ---- Clear Street's side ----
    void logon(const std::string &text = "SIM1") { ev()->logged_on(text); }
    void logout(const std::string &why = "") { ev()->logged_out(why); }

    ExecReport base(const std::string &clordid, char exec_type, char status, double qty = 100)
    {
      ExecReport r;
      r.order_id = "01a01234-0000-7000-8000-" + clordid;
      r.exec_id = next_exec();
      r.exec_type = exec_type;
      r.ord_status = status;
      r.clordid = clordid;
      r.account = "SIM1";
      r.symbol = "AAPL";
      r.side = '1';
      r.qty = qty;
      r.leaves = qty;
      r.transact_time = "20261118-15:00:00.000";
      return r;
    }
    void exec(const ExecReport &r) { ev()->exec(r); }
    ExecReport accepted(const std::string &clordid, double qty = 100, char type = '0')
    {
      auto r = base(clordid, type, type, qty);
      exec(r);
      return r;
    }
    ExecReport fill(const std::string &clordid, double last_qty, double last_px, double cum, double qty = 100)
    {
      auto r = base(clordid, cum >= qty ? '2' : '1', cum >= qty ? '2' : '1', qty);
      r.last_qty = last_qty;
      r.last_px = last_px;
      r.cum = cum;
      r.leaves = qty - cum;
      r.avg_px = last_px;
      exec(r);
      return r;
    }
    void canceled(const std::string &clordid, const std::string &orig = {}, double cum = 0, double avg = 0)
    {
      auto r = base(clordid, '4', '4');
      r.orig_clordid = orig;
      r.cum = cum;
      r.avg_px = avg;
      r.leaves = 0;
      exec(r);
    }
    void replaced(const std::string &clordid, const std::string &orig, double qty, double cum = 0)
    {
      auto r = base(clordid, '5', cum > 0 ? '1' : '0', qty);
      r.orig_clordid = orig;
      r.cum = cum;
      r.leaves = qty - cum;
      exec(r);
    }
    void rejected(const std::string &clordid, int reason = -1, const std::string &text = "")
    {
      auto r = base(clordid, '8', '8');
      r.order_id = "NONE";
      r.rej_reason = reason;
      r.text = text;
      r.leaves = 0;
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
      c.order_id = "NONE";
      c.text = "refused";
      ev()->cancel_reject(c);
    }
    void session_reject(const std::string &clordid, const std::string &type, int tag = 1)
    {
      SessionReject j;
      j.ref_seq = 7;
      j.ref_msg_type = type;
      j.ref_tag = tag;
      j.reason = 5;
      j.text = "Account not authorized";
      j.clordid = clordid;
      ev()->session_reject(j);
    }
    void unparsed(const std::string &t) { ev()->unparsed(t); }

    SessionEvents *ev()
    {
      std::lock_guard<std::mutex> l(mu_);
      return ev_;
    }

  private:
    class Session : public CsSession
    {
    public:
      Session(FakeClearStreet &f, SessionEvents &ev) : f_(f)
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
          f_.ev()->logged_on(f_.logon_text);
      }
      void stop() override {}
      CallStatus send_new(const NewOrderReq &r) override { return rec(f_.news_, r); }
      CallStatus send_cancel(const CancelReq &r) override { return rec(f_.cancels_, r); }
      CallStatus send_replace(const ReplaceReq &r) override { return rec(f_.replaces_, r); }

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
      FakeClearStreet &f_;
    };

    std::string next_exec()
    {
      char b[64];
      std::snprintf(b, sizeof b, "01a01234-0000-7000-9000-%012d", ++exec_n_);
      return b;
    }

    std::mutex mu_;
    SessionEvents *ev_ = nullptr;
    int sessions_ = 0;
    int exec_n_ = 0;
    std::vector<NewOrderReq> news_;
    std::vector<CancelReq> cancels_;
    std::vector<ReplaceReq> replaces_;
  };
}
