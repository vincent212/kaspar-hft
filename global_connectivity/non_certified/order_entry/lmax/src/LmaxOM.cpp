/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_om/act/LmaxOM.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"

namespace lmax::om
{
  namespace som = frame::som::msg;
  using frame::som::ClientId;
  using frame::som::Entry;
  using frame::som::Outcome;
  using frame::som::Result;
  using frame::som::State;
  using msg::LmaxReport;

  // ---- session thread -> mailbox --------------------------------------------------------

  class LmaxOM::Events : public SessionEvents
  {
  public:
    Events(LmaxOM *om, uint64_t s) : om_(om), s_(s) {}
    void logged_on() override { post(new LmaxReport(LmaxReport::LOGGED_ON, s_)); }
    void logged_out(const std::string &why) override
    {
      auto r = new LmaxReport(LmaxReport::LOGGED_OUT, s_);
      r->text = why;
      post(r);
    }
    void exec(const ExecReport &e) override
    {
      auto r = new LmaxReport(LmaxReport::EXEC, s_);
      r->exec = e;
      post(r);
    }
    void cancel_reject(const CancelReject &c) override
    {
      auto r = new LmaxReport(LmaxReport::CANCEL_REJECT, s_);
      r->cxl = c;
      post(r);
    }
    void session_reject(const std::string &t) override
    {
      auto r = new LmaxReport(LmaxReport::SESSION_REJECT, s_);
      r->text = t;
      post(r);
    }
    void unparsed(const std::string &t) override
    {
      auto r = new LmaxReport(LmaxReport::UNPARSED, s_);
      r->text = t;
      post(r);
    }

  private:
    void post(LmaxReport *r) { om_->send(r, nullptr); }
    LmaxOM *om_;
    uint64_t s_;
  };

  // ---- helpers -------------------------------------------------------------------------------

  uint64_t LmaxOM::xordid(const std::string &id)
  {
    if (!id.empty() && id.size() <= 19 && std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit((unsigned char)c); }))
      return std::strtoull(id.c_str(), nullptr, 10);
    uint64_t h = 1469598103934665603ULL; // FNV-1a of a non-numeric OrderID
    for (unsigned char c : id)
      h = (h ^ c) * 1099511628211ULL;
    return h;
  }

  bool LmaxOM::parse_cid(const std::string &s, ClientId &cid)
  {
    if (s.empty() || s.size() > 19 || !std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c); }))
      return false;
    cid = std::strtoull(s.c_str(), nullptr, 10);
    return true;
  }

  const OmInstrument *LmaxOM::instrument(int sym) const
  {
    auto it = cfg_.instruments.find(sym);
    return it == cfg_.instruments.end() ? nullptr : &it->second;
  }

  bool LmaxOM::contracts(int sym, double units, std::string &out) const
  {
    auto in = instrument(sym);
    if (!in)
      return false;
    out = decimal(units * in->unit, in->unit);
    return true;
  }

  int LmaxOM::som_id(int book_oid) const
  {
    auto it = som_id_.find(book_oid);
    return it == som_id_.end() ? book_oid : it->second;
  }

  std::string LmaxOM::clordid(int som_oid) const
  {
    auto it = book_oid_.find(som_oid);
    auto e = book_.find_oid(it == book_oid_.end() ? som_oid : it->second);
    return e ? std::to_string(e->cid) : std::string();
  }

  void LmaxOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void LmaxOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool LmaxOM::take_token() noexcept
  {
    if (cfg_.max_orders_per_sec <= 0)
      return true;
    auto now = std::chrono::steady_clock::now();
    double rate = cfg_.max_orders_per_sec;
    if (refilled_ == std::chrono::steady_clock::time_point{})
      tokens_ = rate;
    else
      tokens_ = std::min(rate, tokens_ + rate * std::chrono::duration<double>(now - refilled_).count());
    refilled_ = now;
    if (tokens_ < 1)
      return false;
    tokens_ -= 1;
    return true;
  }

  namespace
  {
    bool map_tif(som::Order::TimeInForce t, char &tif)
    {
      switch (t)
      {
      case som::Order::TimeInForce::DAY: tif = '0'; return true;
      case som::Order::TimeInForce::GTC: tif = '1'; return true;
      case som::Order::TimeInForce::IOC: tif = '3'; return true;
      case som::Order::TimeInForce::FOK: tif = '4'; return true;
      default: return false;
      }
    }

    char map_ord_type(som::Order::OrdType t)
    {
      switch (t)
      {
      case som::Order::OrdType::MARKET: return '1';
      case som::Order::OrdType::LIMIT: return '2';
      case som::Order::OrdType::STOP: return '3';
      case som::Order::OrdType::STOP_LIMIT: return '4';
      }
      return '2';
    }

    int reject_reason(int ord_rej_reason)
    {
      switch (ord_rej_reason)
      {
      case 3: return som::Reject::EXCHLIMIT; // exceeds limit
      default: return som::Reject::UNKNOWN;
      }
    }

    int cancel_reject_reason(char ord_status, int reason)
    {
      if (ord_status == '2')
        return som::CancReject::FILLEDALREADY;
      if (ord_status == '4' || ord_status == 'C')
        return som::CancReject::CANCELLEDALREADY;
      if (ord_status == '8')
        return som::CancReject::REJECTEDPREVIOUSLY;
      if (reason == 1)
        return som::CancReject::NOTFOUND;
      return som::CancReject::FROMEXCHANGE;
    }

    uint64_t transact_ns(const ExecReport &r)
    {
      uint64_t t = fix::utc_timestamp_ns(r.transact_time);
      return t ? t : fix::now_ns();
    }
  }

  // ---- lifecycle -----------------------------------------------------------------------------

  LmaxOM::LmaxOM(LmaxOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
                 frame::som::ClientIdGen ids, actors::Actor *trade_sink)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), sink_(trade_sink),
        book_(ids)
  {
    strncpy(name, "LmaxOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(LmaxReport, report_handler);
  }

  LmaxOM::~LmaxOM()
  {
    stopping_ = true;
    session_.reset();
    events_.reset();
  }

  void LmaxOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no LMAX session factory: LMAX is unrouted");
      return;
    }
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(*events_);
      session_->start();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("LMAX session not started: %s", e.what());
      std::cerr << "LmaxOM: session not started: " << e.what() << std::endl;
      return;
    }
    if (som_)
      som_->send(new som::SetExchManager(venue_, this), this);
  }

  void LmaxOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    ready_ = false;
    if (session_)
      session_->stop(); // joins QuickFIX's thread: no events after this
    session_.reset();
    events_.reset();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at LMAX", book_.live());
  }

  // ---- requests from the SOM -----------------------------------------------------------------

  void LmaxOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no LMAX session");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "LMAX session not logged on");
    if (!m->strategy.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "LMAX has no broker algorithms");
    if (!m->route.empty() && m->route != "LMAX")
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "route other than LMAX");
    char tif;
    if (!map_tif(m->tif, tif))
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available at LMAX");
    auto in = instrument(m->sym);
    if (!in)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument not configured for LMAX");
    if (!(m->sz > 0) || std::floor(m->sz) != m->sz)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a whole number of units");
    if (cfg_.max_order_qty > 0 && m->sz > cfg_.max_order_qty)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order quantity limit");
    const bool priced = m->ord_type == OT::LIMIT || m->ord_type == OT::STOP_LIMIT;
    if (priced && m->px <= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "limit price must be positive");
    if ((m->ord_type == OT::STOP || m->ord_type == OT::STOP_LIMIT) && m->stop_px <= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "stop price must be positive");
    if (!take_token())
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");
    if (m->oid_to_cancel >= 0)
      return replace(m);

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    Entry &e = *r.entry;
    som_id_[e.oid] = e.oid;
    book_oid_[e.oid] = e.oid;
    venue_state_[e.oid] = {};

    NewOrderReq q;
    q.clordid = std::to_string(e.cid);
    q.security_id = in->security_id;
    q.account = cfg_.account;
    q.side = m->side == en::bs::BUY ? '1' : '2';
    q.qty = decimal(e.qty * in->unit, in->unit);
    q.ord_type = map_ord_type(m->ord_type);
    if (priced)
      q.px = decimal(m->px * in->tick, in->tick);
    if (m->ord_type == OT::STOP || m->ord_type == OT::STOP_LIMIT)
      q.stop_px = decimal(m->stop_px * in->tick, in->tick);
    q.tif = tif;
    q.reinstate = cfg_.reinstate_on_disconnect;
    if (session_->send_new(q) != CallStatus::Success)
    {
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "LMAX session not logged on");
    }
    ++c_.sent_new;
    log_omg("new order oid=%d clordid=%s %s %c %s @ %s tif %c", m->oid, q.clordid.c_str(), in->symbol.c_str(),
            q.side, q.qty.c_str(), q.px.c_str(), tif);
  }

  void LmaxOM::replace(const som::Order *m) noexcept
  {
    auto bo = book_oid_.find(m->oid_to_cancel);
    const int book_oid = bo == book_oid_.end() ? m->oid_to_cancel : bo->second;
    const Entry *old = book_.find_oid(book_oid);
    if (!old)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "replace of an unknown order");
    if (old->order.sym != m->sym || old->order.side != m->side || old->order.ord_type != m->ord_type)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "replace may change only size and price");
    if (m->ord_type != som::Order::OrdType::LIMIT)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "replace of a non-limit order");
    auto in = instrument(m->sym);
    char tif;
    map_tif(old->order.tif, tif);
    auto r = book_.request_replace(book_oid, int(m->sz), m->px);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Busy   ? "a cancel or replace is already in flight"
                    : r.outcome == Outcome::Late ? "the order is done"
                                                 : "replace size not above the filled quantity");
    Entry &e = *r.entry;
    ReplaceReq q;
    q.clordid = std::to_string(e.pending_cid);
    q.orig_clordid = std::to_string(e.cid);
    q.security_id = in->security_id;
    q.account = cfg_.account;
    q.side = m->side == en::bs::BUY ? '1' : '2';
    q.qty = decimal(e.pending_qty * in->unit, in->unit);
    q.ord_type = '2';
    q.px = decimal(m->px * in->tick, in->tick);
    q.tif = tif;
    q.reinstate = cfg_.reinstate_on_disconnect;
    if (session_->send_replace(q) != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "LMAX session not logged on");
    }
    ++c_.sent_replace;
    replace_[book_oid] = *m;
    book_oid_[m->oid] = book_oid;
    log_omg("replace oid=%d -> oid=%d clordid=%s orig=%s %s @ %s", som_id(book_oid), m->oid, q.clordid.c_str(),
            q.orig_clordid.c_str(), q.qty.c_str(), q.px.c_str());
  }

  void LmaxOM::cancel_handler(const som::Cancel *m) noexcept
  {
    const int oid = int(m->id);
    auto bo = book_oid_.find(oid);
    const int book_oid = bo == book_oid_.end() ? oid : bo->second;
    if (!session_ || !ready_)
      return cancel_reject(m->sender, oid, som::CancReject::NOTLOGGEDIN, "LMAX session not logged on");
    auto r = book_.request_cancel(book_oid);
    switch (r.outcome)
    {
    case Outcome::Ok: break;
    case Outcome::Unknown: return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "unknown order");
    case Outcome::Late:
      return cancel_reject(m->sender, oid,
                           r.entry->state == State::Filled     ? som::CancReject::FILLEDALREADY
                           : r.entry->state == State::Canceled ? som::CancReject::CANCELLEDALREADY
                                                               : som::CancReject::REJECTEDPREVIOUSLY,
                           "order is done");
    default: return cancel_reject(m->sender, oid, som::CancReject::UNKNOWN, "a cancel or replace is in flight");
    }
    Entry &e = *r.entry;
    auto in = instrument(e.order.sym);
    CancelReq q;
    q.clordid = std::to_string(e.pending_cid);
    q.orig_clordid = std::to_string(e.cid);
    q.security_id = in ? in->security_id : std::string();
    q.account = cfg_.account;
    q.side = e.order.side == en::bs::BUY ? '1' : '2';
    if (in)
      q.qty = decimal(e.qty * in->unit, in->unit);
    if (session_->send_cancel(q) != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::DISCONNECTED, "LMAX session not logged on");
    }
    ++c_.sent_cancel;
    log_omg("cancel oid=%d clordid=%s orig=%s", oid, q.clordid.c_str(), q.orig_clordid.c_str());
  }

  // ---- reports from LMAX ---------------------------------------------------------------------

  void LmaxOM::request_status() noexcept
  {
    if (!cfg_.status_on_logon || !session_)
      return;
    for (auto &[book_oid, sid] : som_id_)
    {
      const Entry *e = book_.find_oid(book_oid);
      if (!e || frame::som::is_done(e->state))
        continue;
      auto in = instrument(e->order.sym);
      if (!in)
        continue;
      StatusReq q;
      q.clordid = std::to_string(e->cid);
      q.security_id = in->security_id;
      q.account = cfg_.account;
      q.side = e->order.side == en::bs::BUY ? '1' : '2';
      q.req_id = "S" + std::to_string(next_status_++);
      if (session_->send_status(q) == CallStatus::Success)
        ++c_.sent_status;
    }
  }

  void LmaxOM::report_handler(const LmaxReport *r) noexcept
  {
    if (r->session != session_no_ || stopping_)
      return;
    switch (r->kind)
    {
    case LmaxReport::LOGGED_ON:
      ++c_.logons;
      ready_ = true;
      log_omg("LMAX trading session logged on");
      request_status();
      break;
    case LmaxReport::LOGGED_OUT:
      ++c_.logouts;
      ready_ = false;
      log_wrn("LMAX trading session logged out%s%s; %zu orders live", r->text.empty() ? "" : ": ",
              r->text.c_str(), book_.live());
      break;
    case LmaxReport::EXEC: on_exec(r->exec); break;
    case LmaxReport::CANCEL_REJECT: on_cancel_reject(r->cxl); break;
    case LmaxReport::SESSION_REJECT:
      ++c_.session_rejects;
      log_err("session Reject from LMAX: %s", r->text.c_str());
      break;
    case LmaxReport::UNPARSED:
      ++c_.unparsed;
      log_err("LMAX report not parsed: %s", r->text.c_str());
      break;
    default: break;
    }
  }

  void LmaxOM::ack(Entry &e) noexcept
  {
    if (e.sender)
      e.sender->send(new som::Ack(uint(som_id(e.oid)), e.exch_oid), this);
  }

  // Applies the report's CumQty: a fill of the increase over what is known.
  void LmaxOM::apply_cum(Entry &e, const OmInstrument &in, const ExecReport &r, bool from_status) noexcept
  {
    auto &v = venue_state_[e.oid];
    const double add_contracts = r.cum - v.cum_contracts;
    if (add_contracts <= in.unit * 1e-6)
    {
      if (r.exec_type == 'F')
      {
        ++c_.covered_fills;
        log_omg("trade %s on oid=%d already counted (CumQty %g)", r.exec_id.c_str(), som_id(e.oid), r.cum);
      }
      return;
    }
    int64_t units;
    if (!fix::to_steps(add_contracts, in.unit, units))
    {
      ++c_.invalid;
      log_err("fill of %g contracts on oid=%d is not a whole number of units of %g", add_contracts,
              som_id(e.oid), in.unit);
      return;
    }
    // Price: LastPx on a trade report; on a status answer, what the average
    // price implies for the part not yet seen.
    double px = r.last_px;
    if (from_status || !r.has_last || std::fabs(r.last_qty - add_contracts) > in.unit * 1e-6)
      px = r.avg_px > 0 ? (r.avg_px * r.cum - v.notional) / add_contracts : r.last_px;
    if (!e.accepted)
    {
      if (book_.on_accepted(e.cid, xordid(r.order_id)).outcome == Outcome::Ok)
        ack(e);
    }
    auto res = book_.on_fill(e.cid, int(units));
    if (res.outcome != Outcome::Ok)
    {
      ++(res.outcome == Outcome::Late ? c_.late : c_.invalid);
      log_err("fill of %lld units on oid=%d: %s (state %s)", (long long)units, som_id(e.oid),
              frame::som::to_string(res.outcome), frame::som::to_string(e.state));
      return;
    }
    v.cum_contracts = r.cum;
    v.notional += px * add_contracts;
    if (from_status)
      ++c_.status_fills;
    const int pxi = int(std::llround(px / in.tick));
    if (e.sender)
      e.sender->send(new som::Fill(uint(som_id(e.oid)), venue_, uint(e.order.sym), pxi, e.order.side,
                                   double(units), double(e.leaves), e.order.owner, transact_ns(r)),
                     this);
    log_omg("fill oid=%d %lld units @ %.8g (CumQty %g) leaves %d%s", som_id(e.oid), (long long)units, px, r.cum,
            e.leaves, from_status ? " [from status]" : "");
  }

  void LmaxOM::on_exec(const ExecReport &r) noexcept
  {
    ++c_.execs;
    if (!exec_ids_.insert(r.exec_id).second && r.exec_type != 'I')
    {
      ++c_.duplicates;
      log_omg("duplicate ExecID %s ignored", r.exec_id.c_str());
      return;
    }
    if (sink_ && (r.exec_type == 'F' || r.exec_type == 'G' || r.exec_type == 'H'))
      sink_->send(new msg::ExecCopy(r), this);
    if (r.exec_type == 'G' || r.exec_type == 'H')
    {
      ++c_.busts;
      log_err("LMAX %s for ExecID %s, order %s: not a fill; check the position", r.exec_type == 'G' ? "TradeCorrect" : "TradeCancel",
              r.exec_id.c_str(), r.order_id.c_str());
      return;
    }

    ClientId cid = 0;
    const Entry *ce = nullptr;
    if (parse_cid(r.clordid, cid))
      ce = book_.find_cid(cid);
    if (!ce)
    {
      ++c_.unknown;
      log_err("report for an order this OM did not place: ClOrdID %s OrderID %s ExecType %c OrdStatus %c%s",
              r.clordid.c_str(), r.order_id.c_str(), r.exec_type, r.ord_status,
              r.exec_type == 'F' ? " -- a FILL the SOM does not know about" : "");
      return;
    }
    Entry &e = const_cast<Entry &>(*ce);
    auto in = instrument(e.order.sym);
    auto &v = venue_state_[e.oid];
    if (!r.order_id.empty() && r.order_id != "0")
      v.order_id = r.order_id;

    if (!in)
    {
      ++c_.invalid;
      log_err("report for oid=%d of an instrument no longer configured", som_id(e.oid));
      return;
    }
    auto finish_replace = [&](bool replaced) {
      auto it = replace_.find(e.oid);
      if (it == replace_.end())
        return;
      const som::Order &nw = it->second;
      if (replaced)
      {
        if (nw.sender)
        {
          nw.sender->send(new som::CancAck(uint(som_id(e.oid))), this);
          nw.sender->send(new som::Ack(uint(nw.oid), e.exch_oid), this);
        }
        som_id_[e.oid] = nw.oid;
      }
      else if (nw.sender)
        nw.sender->send(new som::Reject(uint(nw.oid), som::Reject::UNKNOWN), this);
      replace_.erase(it);
    };

    switch (r.exec_type)
    {
    case '0': // New
    {
      const bool was = e.accepted;
      auto res = book_.on_accepted(cid, xordid(r.order_id));
      if (res.outcome == Outcome::Ok && !was)
        ack(e);
      else if (res.outcome != Outcome::Ok)
        ++c_.late;
      break;
    }
    case 'F': // Trade
      apply_cum(e, *in, r, false);
      break;
    case '4': // Canceled
    case 'C': // Expired
    {
      apply_cum(e, *in, r, false); // a fill the cancel report shows but no trade report did
      const State before = e.state;
      auto res = book_.on_canceled(cid);
      if (res.outcome == Outcome::Ok)
      {
        if (e.sender)
          e.sender->send(new som::CancAck(uint(som_id(e.oid))), this);
        finish_replace(false); // a replace in flight cannot complete now
        log_omg("cancelled oid=%d (%s%s)%s%s", som_id(e.oid), before == State::PendingCancel ? "requested" : "unsolicited",
                r.exec_type == 'C' ? ", expired" : "", r.text.empty() ? "" : ": ", r.text.c_str());
      }
      else
      {
        ++c_.late;
        log_omg("cancel report for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
      }
      break;
    }
    case '5': // Replaced
    {
      auto res = book_.on_replaced(cid);
      if (res.outcome == Outcome::Ok || res.outcome == Outcome::Invalid)
      {
        finish_replace(true);
        if (res.outcome == Outcome::Invalid)
          log_err("oid=%d filled beyond its new size while the replace was in flight", som_id(e.oid));
        log_omg("replaced: now oid=%d clordid=%s qty %d px %d", som_id(e.oid), r.clordid.c_str(), e.qty, e.px);
      }
      else
      {
        ++c_.late;
        log_err("Replaced for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
      }
      apply_cum(e, *in, r, false);
      break;
    }
    case '8': // Rejected
    {
      if (e.state == State::PendingReplace && cid == e.pending_cid)
      {
        auto res = book_.on_replace_rejected(cid);
        if (res.outcome == Outcome::Ok)
        {
          if (e.sender)
            e.sender->send(new som::CancReject(uint(som_id(e.oid)), som::CancReject::REJECTONMOD), this);
          finish_replace(false);
        }
        break;
      }
      auto res = book_.on_rejected(cid);
      if (res.outcome == Outcome::Ok)
      {
        if (e.sender)
          e.sender->send(new som::Reject(uint(som_id(e.oid)), reject_reason(r.rej_reason)), this);
        log_omg("rejected oid=%d reason %d %s", som_id(e.oid), r.rej_reason, r.text.c_str());
      }
      else
      {
        ++(res.outcome == Outcome::Invalid ? c_.invalid : c_.late);
        log_err("Rejected for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
      }
      break;
    }
    case 'I': // OrderStatus, answering our OrderStatusRequest
    {
      switch (r.ord_status)
      {
      case '0':
      case '1':
      case '2':
      {
        const bool was = e.accepted;
        if (!was && book_.on_accepted(cid, xordid(r.order_id)).outcome == Outcome::Ok)
          ack(e);
        apply_cum(e, *in, r, true);
        break;
      }
      case '4':
      case 'C':
        apply_cum(e, *in, r, true);
        if (book_.on_canceled(cid).outcome == Outcome::Ok)
        {
          if (e.sender)
            e.sender->send(new som::CancAck(uint(som_id(e.oid))), this);
          finish_replace(false);
          log_omg("oid=%d found cancelled by OrderStatus", som_id(e.oid));
        }
        break;
      case '8':
        if (!e.accepted && book_.on_rejected(cid).outcome == Outcome::Ok)
        {
          if (e.sender)
            e.sender->send(new som::Reject(uint(som_id(e.oid)), som::Reject::UNKNOWN), this);
          log_omg("oid=%d found rejected by OrderStatus: %s", som_id(e.oid), r.text.c_str());
        }
        else
          log_err("OrderStatus says rejected for accepted oid=%d: %s", som_id(e.oid), r.text.c_str());
        break;
      default:
        log_omg("OrderStatus %c for oid=%d", r.ord_status, som_id(e.oid));
      }
      break;
    }
    default: // PendingNew, PendingCancel, PendingReplace, Restated, Done, ...
      log_omg("ExecType %c OrdStatus %c for oid=%d", r.exec_type, r.ord_status, som_id(e.oid));
    }
  }

  void LmaxOM::on_cancel_reject(const CancelReject &r) noexcept
  {
    ClientId cid = 0;
    const Entry *ce = parse_cid(r.clordid, cid) ? book_.find_cid(cid) : nullptr;
    if (!ce)
    {
      ++c_.unknown;
      log_err("OrderCancelReject for an unknown request: ClOrdID %s OrigClOrdID %s", r.clordid.c_str(),
              r.orig_clordid.c_str());
      return;
    }
    Entry &e = const_cast<Entry &>(*ce);
    const int reason = cancel_reject_reason(r.ord_status, r.reason);
    if (r.response_to == '2')
    {
      auto res = book_.on_replace_rejected(cid);
      if (res.outcome != Outcome::Ok)
      {
        ++c_.late;
        log_omg("replace reject for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
        return;
      }
      if (e.sender)
        e.sender->send(new som::CancReject(uint(som_id(e.oid)), reason), this);
      auto it = replace_.find(e.oid);
      if (it != replace_.end())
      {
        if (it->second.sender)
          it->second.sender->send(new som::Reject(uint(it->second.oid), som::Reject::UNKNOWN), this);
        replace_.erase(it);
      }
      log_omg("replace of oid=%d rejected: reason %d %s", som_id(e.oid), r.reason, r.text.c_str());
      return;
    }
    auto res = book_.on_cancel_rejected(cid);
    if (res.outcome != Outcome::Ok)
    {
      ++c_.late;
      log_omg("cancel reject for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
      return;
    }
    if (e.sender)
      e.sender->send(new som::CancReject(uint(som_id(e.oid)), reason), this);
    log_omg("cancel of oid=%d rejected: reason %d %s", som_id(e.oid), r.reason, r.text.c_str());
  }
}
