/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "clearstreet_om/SimClearStreet.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace clearstreet::om
{
  using namespace std::chrono;

  SimClearStreet::SimClearStreet(SimConfig cfg, SessionEvents &ev) : cfg_(std::move(cfg)), ev_(ev) {}
  SimClearStreet::~SimClearStreet() { stop(); }

  void SimClearStreet::start()
  {
    run_ = std::make_unique<frame::som::TimedRunner>();
    run_->after(milliseconds(cfg_.ack_ms), [this] { restore(); });
  }

  void SimClearStreet::stop()
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_)
        return;
      stopped_ = true;
      up_ = false;
    }
    run_.reset(); // joins the worker: no events after this
  }

  void SimClearStreet::restore()
  {
    std::string text;
    {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_ || up_)
        return;
      up_ = true;
      for (auto &a : cfg_.accounts)
        text += (text.empty() ? "" : ",") + a;
    }
    ev_.logged_on(text);
  }

  void SimClearStreet::drop()
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_ || !up_)
        return;
      up_ = false;
    }
    ev_.logged_out("connection lost (simulated)");
  }

  std::string SimClearStreet::check(const NewOrderReq &r)
  {
    if (r.account.empty() || r.clordid.empty() || r.symbol.empty())
      return "required tag missing";
    if (r.clordid.size() > 64)
      return "ClOrdID longer than 64 characters";
    if (r.side != '1' && r.side != '2')
      return "Side must be 1 or 2";
    if (r.open_close && r.open_close != 'O' && r.open_close != 'C')
      return "OpenClose must be O or C";
    if (!(std::strtod(r.qty.c_str(), nullptr) > 0))
      return "OrderQty must be greater than zero";
    const bool limit = r.ord_type == '2' || r.ord_type == '4';
    const bool stop = r.ord_type == '3' || r.ord_type == '4';
    if (r.ord_type < '1' || r.ord_type > '4')
      return "OrdType not supported";
    if (limit != !r.px.empty())
      return limit ? "Price required" : "Price forbidden";
    if (stop != !r.stop_px.empty())
      return stop ? "StopPx required" : "StopPx forbidden";
    switch (r.tif)
    {
    case '0': break;
    case '1':
      if (r.ord_type == '1')
        return "GTC not allowed on market orders";
      break;
    case '2':
    case '3':
    case '4':
    case '7':
      if (stop)
        return "time in force not allowed on stop orders";
      break;
    default: return "TimeInForce not supported";
    }
    if (r.target_strategy)
    {
      if (r.target_strategy < 1001 || r.target_strategy > 1003)
        return "TargetStrategy not supported";
      if (r.ord_type != '1' && r.ord_type != '2')
        return "strategies only on market and limit orders";
      if (r.tif != '0' || r.extended_hours)
        return "strategies only with Day and without extended hours";
      if ((r.target_strategy == 1003) != !r.ex_destination.empty())
        return r.target_strategy == 1003 ? "ExDestination required with DMA" : "ExDestination only with DMA";
    }
    else if (!r.ex_destination.empty())
      return "ExDestination only with DMA";
    if (r.extended_hours && (!limit || r.tif != '0'))
      return "extended hours only for Day limit and stop-limit orders";
    return {};
  }

  double SimClearStreet::px_of(const Ord &o) const
  {
    if (!o.req.px.empty())
      return std::strtod(o.req.px.c_str(), nullptr);
    if (!o.req.stop_px.empty())
      return std::strtod(o.req.stop_px.c_str(), nullptr);
    return cfg_.market_px;
  }

  ExecReport SimClearStreet::base(const Ord &o, char exec_type) const
  {
    ExecReport r;
    r.order_id = o.order_id;
    r.exec_type = exec_type;
    r.ord_status = o.status;
    r.clordid = o.current;
    r.account = o.req.account;
    r.symbol = o.req.symbol;
    r.side = o.req.side;
    r.qty = o.qty;
    r.px = o.req.px.empty() ? 0 : std::strtod(o.req.px.c_str(), nullptr);
    r.stop_px = o.req.stop_px.empty() ? 0 : std::strtod(o.req.stop_px.c_str(), nullptr);
    r.cum = o.cum;
    r.leaves = terminal(o) ? 0 : o.qty - o.cum;
    r.avg_px = o.cum > 0 ? o.notional / o.cum : 0;
    r.transact_time = utc_now();
    return r;
  }

  // Called with mu_ held.
  void SimClearStreet::emit_exec(Ord &o, char exec_type, const std::string &orig, double last_qty, double last_px,
                                 const std::string &text)
  {
    ExecReport r = base(o, exec_type);
    char b[64];
    std::snprintf(b, sizeof b, "SIMX-%012llu", (unsigned long long)++ids_);
    r.exec_id = b;
    r.orig_clordid = orig;
    r.last_qty = last_qty;
    r.last_px = last_px;
    r.text = text;
    r.seq = ++seq_;
    ev_.exec(r);
  }

  void SimClearStreet::schedule_fill(const std::string &order_id, int n)
  {
    if (cfg_.fill_ms < 0)
      return;
    run_->after(milliseconds(cfg_.fill_ms), [this, order_id, n] {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_)
        return;
      auto it = orders_.find(order_id);
      if (it == orders_.end() || terminal(it->second))
        return;
      Ord &o = it->second;
      const double open = o.qty - o.cum;
      if (open <= 0)
        return;
      double q = n >= cfg_.fills ? open : std::floor(o.qty / std::max(1, cfg_.fills));
      if (q <= 0 || q > open)
        q = open;
      const double px = px_of(o);
      o.cum += q;
      o.notional += q * px;
      o.status = o.cum >= o.qty ? '2' : '1';
      emit_exec(o, o.status, "", q, px);
      if (o.status == '1')
        schedule_fill(order_id, n + 1);
    });
  }

  CallStatus SimClearStreet::send_new(const NewOrderReq &r)
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!up_ || stopped_)
      return CallStatus::Disconnected;
    const uint64_t seq = ++seq_;
    run_->after(milliseconds(cfg_.ack_ms), [this, r, seq] {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_)
        return;
      if (!cfg_.accounts.count(r.account))
      {
        // guide: an unknown or unauthorized account is rejected with Reject (35=3)
        SessionReject j;
        j.ref_seq = seq;
        j.ref_msg_type = "D";
        j.ref_tag = 1;
        j.reason = 5; // value is incorrect for this tag
        j.text = "Account not authorized for this session";
        j.clordid = r.clordid;
        ev_.session_reject(j);
        return;
      }
      char b[64];
      std::snprintf(b, sizeof b, "01a0sim0-0000-7000-8000-%012llu", (unsigned long long)++ids_);
      Ord o;
      o.req = r;
      o.order_id = b;
      o.current = r.clordid;
      o.qty = std::strtod(r.qty.c_str(), nullptr);
      std::string why = check(r);
      int rej = -1;
      if (why.empty() && !clordids_.insert(r.clordid).second)
      {
        why = "duplicate ClOrdID";
        rej = 6;
      }
      if (!why.empty())
      {
        o.order_id = "NONE";
        o.status = '8';
        ExecReport x = base(o, '8');
        std::snprintf(b, sizeof b, "SIMX-%012llu", (unsigned long long)++ids_);
        x.exec_id = b;
        x.rej_reason = rej;
        x.text = why;
        x.seq = ++seq_;
        ev_.exec(x);
        return;
      }
      o.status = cfg_.queue_orders ? 'A' : '0';
      auto &ref = orders_[o.order_id] = o;
      by_clordid_[o.current] = o.order_id;
      emit_exec(ref, ref.status, "", 0, 0);
      if (cfg_.queue_orders)
      {
        ref.status = '0';
        emit_exec(ref, '0', "", 0, 0);
      }
      schedule_fill(o.order_id, 1);
    });
    return CallStatus::Success;
  }

  CallStatus SimClearStreet::send_cancel(const CancelReq &r)
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!up_ || stopped_)
      return CallStatus::Disconnected;
    ++seq_;
    run_->after(milliseconds(cfg_.ack_ms), [this, r] {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_)
        return;
      clordids_.insert(r.clordid);
      auto it = by_clordid_.find(r.orig_clordid);
      Ord *o = it == by_clordid_.end() ? nullptr : &orders_[it->second];
      if (!o || terminal(*o))
      {
        CancelReject c;
        c.order_id = o ? o->order_id : "NONE";
        c.clordid = r.clordid;
        c.orig_clordid = r.orig_clordid;
        c.account = r.account;
        c.ord_status = o ? o->status : '8';
        c.response_to = '1';
        c.reason = o ? 0 : 1;
        c.text = o ? "order is terminal" : "unknown order";
        c.seq = ++seq_;
        ev_.cancel_reject(c);
        return;
      }
      by_clordid_.erase(o->current);
      o->current = r.clordid;
      by_clordid_[o->current] = o->order_id;
      o->status = '4';
      emit_exec(*o, '4', r.orig_clordid, 0, 0);
    });
    return CallStatus::Success;
  }

  CallStatus SimClearStreet::send_replace(const ReplaceReq &r)
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!up_ || stopped_)
      return CallStatus::Disconnected;
    ++seq_;
    run_->after(milliseconds(cfg_.ack_ms), [this, r] {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_)
        return;
      clordids_.insert(r.clordid);
      auto it = by_clordid_.find(r.orig_clordid);
      Ord *o = it == by_clordid_.end() ? nullptr : &orders_[it->second];
      std::string why;
      int reason = -1;
      if (!o)
        why = "unknown order", reason = 1;
      else if (terminal(*o))
        why = "order is terminal", reason = 0;
      else if (r.side != o->req.side || r.symbol != o->req.symbol || r.ord_type != o->req.ord_type ||
               r.tif != o->req.tif || r.extended_hours != o->req.extended_hours)
        why = "non-replaceable field changed";
      else if (std::strtod(r.qty.c_str(), nullptr) < o->cum)
        why = "OrderQty below the filled quantity";
      if (!why.empty())
      {
        CancelReject c;
        c.order_id = o ? o->order_id : "NONE";
        c.clordid = r.clordid;
        c.orig_clordid = r.orig_clordid;
        c.account = r.account;
        c.ord_status = o ? o->status : '8';
        c.response_to = '2';
        c.reason = reason;
        c.text = why;
        c.seq = ++seq_;
        ev_.cancel_reject(c);
        return;
      }
      by_clordid_.erase(o->current);
      o->current = r.clordid;
      by_clordid_[o->current] = o->order_id;
      o->req.px = r.px;
      o->req.stop_px = r.stop_px;
      o->req.qty = r.qty;
      o->qty = std::strtod(r.qty.c_str(), nullptr);
      o->status = o->cum >= o->qty ? '2' : o->cum > 0 ? '1' : '0';
      emit_exec(*o, '5', r.orig_clordid, 0, 0);
    });
    return CallStatus::Success;
  }

  void SimClearStreet::cancel_all(const std::string &text)
  {
    if (!run_)
      return;
    run_->now([this, text] {
      std::lock_guard<std::mutex> l(mu_);
      if (stopped_)
        return;
      for (auto &[id, o] : orders_)
        if (!terminal(o))
        {
          o.status = '4';
          emit_exec(o, '4', "", 0, 0, text);
        }
    });
  }

  SessionFactory sim_session_factory(SimConfig cfg)
  {
    return [cfg](SessionEvents &ev) -> std::unique_ptr<CsSession> { return std::make_unique<SimClearStreet>(cfg, ev); };
  }
}
