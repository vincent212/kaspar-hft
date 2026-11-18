/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_oe/act/DeribitOM.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"

#include "deribit_oe/SimDeribit.hpp"

namespace deribit::oe
{
  namespace som = frame::som::msg;
  using frame::som::ClientId;
  using frame::som::ClientIdGen;
  using frame::som::Outcome;
  using frame::som::State;
  using frame::som::to_string;
  using msg::DeribitReport;
  using json = nlohmann::json;

  bool order_method(const std::string &m)
  {
    return m == "private/buy" || m == "private/sell" || m == "private/cancel" || m == "private/cancel_by_label";
  }

  // ---- session thread -> mailbox --------------------------------------------------------------

  class DeribitOM::Events : public SessionEvents
  {
  public:
    Events(DeribitOM *om, uint64_t session) : om_(om), session_(session) {}
    void logged_in() override { post(DeribitReport::LOGGED_IN); }
    void login_rejected(const std::string &code, const std::string &m) override
    {
      post(DeribitReport::LOGIN_REJECTED, code + " " + m);
    }
    void disconnected(const std::string &why) override { post(DeribitReport::DISCONNECTED, why); }
    void busy() override { post(DeribitReport::BUSY); }
    void available() override { post(DeribitReport::AVAILABLE); }
    void rpc_result(const json &r) override { post(DeribitReport::RPC_RESULT, {}, r); }
    void order_update(const json &o) override { post(DeribitReport::ORDER_UPDATE, {}, o); }
    void trade(const json &t) override { post(DeribitReport::TRADE, {}, t); }

  private:
    void post(DeribitReport::Kind k, std::string text = {}, json body = {})
    {
      auto r = new DeribitReport(k, session_);
      r->text = std::move(text);
      r->body = std::move(body);
      om_->send(r, nullptr);
    }
    DeribitOM *om_;
    uint64_t session_;
  };

  namespace
  {
    std::string str(const json &j, const char *k)
    {
      auto it = j.find(k);
      if (it == j.end() || it->is_null())
        return {};
      return it->is_string() ? it->get<std::string>() : it->dump();
    }

    double num(const json &j, const char *k)
    {
      auto it = j.find(k);
      if (it == j.end() || !it->is_number())
        return NAN;
      return it->get<double>();
    }

    bool all_digits(const std::string &s)
    {
      return !s.empty() && s.size() <= 19 &&
             std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    // JSON-RPC error code -> SOM reject reason (feed.md 4, "Errors"; codes
    // from docs.deribit.com/articles/errors).
    int reject_reason(int64_t code)
    {
      switch (code)
      {
      case 10028:   // too_many_requests
      case 10040:   // retry: "can't be processed right now and should be retried"
      case 10041:   // settlement_in_progress
        return som::Reject::THROTTLE;
      case 10009:   // not_enough_funds (the same page's summary table also says invalid_argument)
        return som::Reject::EXCHLIMIT;
      default: return som::Reject::UNKNOWN;
      }
    }

    int cancel_reject_reason(State s)
    {
      switch (s)
      {
      case State::Filled: return som::CancReject::FILLEDALREADY;
      case State::Canceled: return som::CancReject::CANCELLEDALREADY;
      case State::Rejected: return som::CancReject::REJECTEDPREVIOUSLY;
      default: return som::CancReject::NOTFOUND;
      }
    }

    uint64_t ms_to_ns(const json &j, const char *k)
    {
      const double ms = num(j, k);
      return std::isnan(ms) || ms < 0 ? 0 : uint64_t(ms) * 1000000ull;
    }
  }

  uint64_t DeribitOM::xordid(const std::string &order_id)
  {
    const auto p = order_id.rfind('-');
    const std::string d = p == std::string::npos ? order_id : order_id.substr(p + 1);
    return all_digits(d) ? std::stoull(d) : 0;
  }

  // ---- lifecycle --------------------------------------------------------------------------------

  DeribitOM::DeribitOM(DeribitOMConfig cfg, actors::Actor *som, SessionFactory factory, ClientIdGen ids)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), book_(ids)
  {
    strncpy(name, "DeribitOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(DeribitReport, report_handler);
  }

  DeribitOM::~DeribitOM()
  {
    stopping_ = true;
    reconnect_timer_ = {};
    drop_session();
  }

  std::string DeribitOM::order_id(int oid) const
  {
    auto e = book_.find_oid(oid);
    if (!e)
      return {};
    auto it = by_cid_.find(e->cid);
    return it == by_cid_.end() ? std::string() : it->second.order_id;
  }

  void DeribitOM::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(cfg_, *events_);
      ++sessions_created_;
      log_omg("Deribit session %lu: logging in (%s, host %s)", session_no_, cfg_.ep.test ? "test" : "production",
              cfg_.ep.host);
      session_->logon();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create Deribit session: %s", e.what());
      schedule_reconnect();
    }
  }

  void DeribitOM::drop_session() noexcept
  {
    ready_ = false;
    busy_ = false;
    session_.reset();   // joins the session's thread: no events after this
    events_.reset();
    // Requests in flight on the dropped connection get no answer: a new
    // order's outcome comes from recover(), a cancel is refused now.
    for (auto it = pending_.begin(); it != pending_.end();)
    {
      if (it->second.kind == Pending::CANCEL)
      {
        auto res = book_.on_cancel_rejected(it->second.cid);
        if (res.outcome == Outcome::Ok && res.entry->sender)
          res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::DISCONNECTED), this);
      }
      it = pending_.erase(it);
    }
  }

  void DeribitOM::schedule_reconnect() noexcept
  {
    if (stopping_ || login_refused_)
      return;
    auto expected = session_no_;
    auto ms = cfg_.reconnect_ms;
    reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> l(m);
      if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
        send(new DeribitReport(DeribitReport::RECONNECT, expected), nullptr);
    });
  }

  void DeribitOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no Deribit session factory: DERIBIT is unrouted");
      return;
    }
    send(new DeribitReport(DeribitReport::RECONNECT, session_no_), nullptr);
    if (som_)
    {
      std::unordered_set<int> venues;
      for (const auto &i : cfg_.instruments)
        if (venues.insert(int(i.venue)).second)
          som_->send(new som::SetExchManager(i.venue, this), this);
    }
  }

  void DeribitOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    if (session_)
      session_->logout();
    drop_session();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at Deribit", book_.live());
  }

  // ---- requests from the SOM ---------------------------------------------------------------------

  void DeribitOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void DeribitOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool DeribitOM::take_token() noexcept
  {
    if (cfg_.max_orders_per_sec <= 0)
      return true;
    auto now = std::chrono::steady_clock::now();
    const double rate = cfg_.max_orders_per_sec;
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

  void DeribitOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    using TIF = som::Order::TimeInForce;
    if (m->oid_to_cancel >= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Deribit connection");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Deribit session not logged in");
    if (busy_)
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Deribit connection busy");
    const Instrument *ins = cfg_.by_sym(m->sym);
    if (!ins)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument not configured for Deribit");
    if (ins->venue != m->venue)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "venue does not match the instrument");
    if (m->ord_type != OT::LIMIT)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "only limit orders are supported");
    const char *tif = nullptr;
    bool post_only = false;
    switch (m->tif)
    {
    case TIF::DAY: tif = "good_til_day"; post_only = cfg_.post_only; break;
    case TIF::GTC: tif = "good_til_cancelled"; post_only = cfg_.post_only; break;
    case TIF::IOC: tif = "immediate_or_cancel"; break;
    case TIF::FOK: tif = "fill_or_kill"; break;
    default: break;
    }
    if (!tif)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on Deribit");
    if (m->short_sale != som::Order::ShortSale::NONE)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Deribit has no short-sale marking");
    if (!m->route.empty() && m->route != "DERIBIT")
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "DeribitOM routes to DERIBIT only");
    if (!m->strategy.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Deribit has no broker algorithms");
    if (!(m->sz > 0) || std::floor(m->sz) != m->sz || m->sz > 2e9)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a positive whole number of units");
    if (cfg_.max_order_units > 0 && m->sz > cfg_.max_order_units)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order size limit");
    if (m->px <= 0)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "price must be positive");
    const double amount = m->sz * ins->unit_d();
    const double notional = ins->inverse ? amount : amount * m->px * ins->tick_d();
    if (cfg_.max_order_notional > 0 && notional > cfg_.max_order_notional)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
    if (!take_token())
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    auto &e = *r.entry;
    const std::string label = std::to_string(e.cid);
    // Numbers, not strings: Deribit's examples send amount and price as JSON
    // numbers. Both are exact decimals of the configured units.
    json params = {{"instrument_name", ins->name},
                   {"amount", json::parse(scaled(int64_t(e.qty), ins->qty_unit))},
                   {"type", "limit"},
                   {"price", json::parse(scaled(int64_t(m->px), ins->tick))},
                   {"label", label},
                   {"time_in_force", tif}};
    if (post_only)
    {
      // reject_post_only: a post-only order that would cross is rejected,
      // not repriced (feed.md 4).
      params["post_only"] = true;
      params["reject_post_only"] = true;
    }
    const std::string method = m->side == en::bs::BUY ? "private/buy" : "private/sell";
    const int64_t id = next_rpc_id_++;
    auto &v = by_cid_[e.cid];
    v.cid = e.cid;
    v.ins = ins;
    switch (session_->call(id, method, params))
    {
    case CallStatus::Success:
      pending_[id] = {Pending::NEW, e.cid, e.cid};
      log_omg("new order oid=%d label=%s %s %s amount %s @ %s %s", m->oid, label, method, ins->name,
              params["amount"].dump(), params["price"].dump(), tif);
      return;
    case CallStatus::Busy:
      by_cid_.erase(e.cid);
      book_.send_failed(e.cid);
      busy_ = true;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Deribit connection busy");
    case CallStatus::Disconnected:
      by_cid_.erase(e.cid);
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Deribit connection lost");
    }
  }

  void DeribitOM::cancel_handler(const som::Cancel *m) noexcept
  {
    const int oid = int(m->id);
    auto r = book_.request_cancel(oid);
    switch (r.outcome)
    {
    case Outcome::Ok: break;
    case Outcome::Busy:
      log_inf("cancel of oid=%d ignored: a cancel is already pending", oid);
      return;
    case Outcome::Late:
      return cancel_reject(m->sender, oid, cancel_reject_reason(r.entry->state), "order already done");
    default:
      return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "no such order");
    }
    auto &e = *r.entry;
    auto vit = by_cid_.find(e.cid);
    if (vit == by_cid_.end())
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "order was never sent");
    }
    auto &v = vit->second;
    // private/cancel needs the order_id; before Deribit has answered, the
    // order is cancelled by its label, which also removes it from the
    // connection's queue if it has not reached the book (feed.md 4).
    std::string method = "private/cancel";
    json params = {{"order_id", v.order_id}};
    if (v.order_id.empty())
    {
      method = "private/cancel_by_label";
      params = {{"label", std::to_string(e.cid)}, {"currency", v.ins->currency}};
    }
    const int64_t id = next_rpc_id_++;
    auto st = session_ && ready_ ? session_->call(id, method, params) : CallStatus::Disconnected;
    if (st != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      if (st == CallStatus::Busy)
        busy_ = true;
      return cancel_reject(m->sender, oid,
                           st == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                           "Deribit cancel not sent");
    }
    pending_[id] = {Pending::CANCEL, e.pending_cid, e.cid};
    log_omg("cancel oid=%d label=%lu via %s", oid, e.cid, method);
  }

  // ---- reports -----------------------------------------------------------------------------------

  void DeribitOM::report_handler(const DeribitReport *m) noexcept
  {
    if (m->kind == DeribitReport::RECONNECT)
    {
      if (!stopping_ && !login_refused_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->kind == DeribitReport::DROP_CONNECTION)
    {
      if (session_)
      {
        log_wrn("dropping the Deribit connection on request (%zu orders live)", book_.live());
        drop_session();
        schedule_reconnect();
      }
      return;
    }
    if (m->session != session_no_ || !session_)
      return;   // from a session already dropped

    switch (m->kind)
    {
    case DeribitReport::LOGGED_IN:
      ready_ = true;
      log_omg("Deribit logged in, user.orders and user.trades subscribed (%zu orders live)", book_.live());
      recover_live();
      return;
    case DeribitReport::LOGIN_REJECTED:
      log_err("Deribit login refused: %s. DERIBIT is unrouted until restart.", m->text);
      login_refused_ = true;
      drop_session();
      return;
    case DeribitReport::DISCONNECTED:
      log_err("Deribit connection lost: %s; reconnecting in %d ms (%zu orders live)", m->text, cfg_.reconnect_ms,
              book_.live());
      drop_session();
      schedule_reconnect();
      return;
    case DeribitReport::BUSY:
      busy_ = true;
      log_wrn("Deribit connection busy: refusing orders");
      return;
    case DeribitReport::AVAILABLE:
      busy_ = false;
      log_omg("Deribit connection available");
      return;
    case DeribitReport::RPC_RESULT: on_rpc_result(m->body); return;
    case DeribitReport::ORDER_UPDATE: on_order(m->body); return;
    case DeribitReport::TRADE: on_trade(m->body); return;
    default: return;
    }
  }

  void DeribitOM::recover_live() noexcept
  {
    for (auto &[cid, v] : by_cid_)
    {
      auto e = book_.find_cid(cid);
      if (!e || frame::som::is_done(e->state))
        continue;
      if (session_->recover(v.order_id, std::to_string(cid), v.ins->currency) != CallStatus::Success)
        log_err("Deribit recovery of label=%lu not sent", cid);
    }
  }

  void DeribitOM::ack(Venue &v) noexcept
  {
    if (v.acked)
      return;
    auto res = book_.on_accepted(v.cid, xordid(v.order_id));
    if (res.outcome != Outcome::Ok)
    {
      log_wrn("accept of label=%lu: %s", v.cid, to_string(res.outcome));
      return;
    }
    v.acked = true;
    if (res.entry->sender)
      res.entry->sender->send(new som::Ack(uint(res.entry->oid), res.entry->exch_oid), this);
  }

  void DeribitOM::on_rpc_result(const json &r) noexcept
  {
    if (!r.contains("id") || !r["id"].is_number_integer())
    {
      log_wrn("Deribit response without an integer id ignored: %s", r.dump());
      return;
    }
    const int64_t id = r["id"].get<int64_t>();
    auto it = pending_.find(id);
    if (it == pending_.end())
    {
      log_err("Deribit response to request %ld, not one of ours: %s", id, r.dump());
      return;
    }
    const Pending p = it->second;
    pending_.erase(it);
    const bool ok = r.contains("result") && !r.contains("error");
    const json err = r.value("error", json::object());
    const int64_t code = err.value("code", int64_t(0));
    const std::string why = err.value("message", std::string()) + " " + err.value("data", json()).dump();

    if (p.kind == Pending::NEW)
    {
      auto vit = by_cid_.find(p.cid);
      if (vit == by_cid_.end())
        return;
      if (!ok)
      {
        auto res = book_.on_rejected(p.cid);
        if (res.outcome == Outcome::Ok && res.entry->sender)
          res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), reject_reason(code)), this);
        log_rej("Deribit rejected label=%lu: code %ld %s (%s)", p.cid, code, why, to_string(res.outcome));
        return;
      }
      const json &res = r["result"];
      // Accepted (unless rejected), then the trades the order made on
      // arrival, then its state: Ack, Fill..., and CancAck for an IOC
      // remainder.
      if (res.contains("order") && res["order"].is_object())
      {
        learn_order_id(vit->second, res["order"]);
        if (str(res["order"], "order_state") != "rejected")
          ack(vit->second);
      }
      if (res.contains("trades") && res["trades"].is_array())
        for (const auto &t : res["trades"])
          on_trade(t);
      if (res.contains("order") && res["order"].is_object())
        on_order(res["order"]);
      return;
    }

    // A cancel.
    if (ok)
    {
      const json &res = r["result"];
      if (res.is_object())
        on_order(res);   // private/cancel answers with the order, order_state cancelled
      else if (res.is_number() && res.get<double>() == 0)
      {
        // cancel_by_label found nothing open under the label: the order has
        // ended (its report is on the way) or never reached the book.
        auto rr = book_.on_cancel_rejected(p.cid);
        if (rr.outcome == Outcome::Ok && rr.entry->sender)
          rr.entry->sender->send(new som::CancReject(uint(rr.entry->oid), som::CancReject::FROMEXCHANGE), this);
        log_rej("Deribit cancel_by_label of label=%lu cancelled nothing (%s)", p.order_cid, to_string(rr.outcome));
      }
      return;
    }
    auto rr = book_.on_cancel_rejected(p.cid);
    if (rr.outcome == Outcome::Ok && rr.entry->sender)
      rr.entry->sender->send(new som::CancReject(uint(rr.entry->oid), som::CancReject::FROMEXCHANGE), this);
    log_rej("Deribit refused cancel of label=%lu: code %ld %s (%s)", p.order_cid, code, why, to_string(rr.outcome));
  }

  DeribitOM::Venue *DeribitOM::find(const json &j) noexcept
  {
    const std::string label = str(j, "label");
    if (all_digits(label))
    {
      auto it = by_cid_.find(std::stoull(label));
      if (it != by_cid_.end())
        return &it->second;
    }
    const std::string oid = str(j, "order_id");
    if (!oid.empty())
    {
      auto it = cid_of_order_.find(oid);
      if (it != cid_of_order_.end())
        return &by_cid_[it->second];
    }
    return nullptr;
  }

  void DeribitOM::learn_order_id(Venue &v, const json &j) noexcept
  {
    if (!v.order_id.empty())
      return;
    v.order_id = str(j, "order_id");
    if (!v.order_id.empty())
      cid_of_order_[v.order_id] = v.cid;
  }

  void DeribitOM::on_order(const json &o) noexcept
  {
    Venue *v = find(o);
    const std::string state = str(o, "order_state");
    if (!v)
    {
      log_inf("Deribit order %s (label '%s') state %s: not this OM's, ignored", str(o, "order_id"), str(o, "label"),
              state);
      return;
    }
    learn_order_id(*v, o);
    if (state == "open")
      ack(*v);
    else if (state == "filled")
    {
      ack(*v);
      // The trades come on user.trades (or in the response); the state may
      // arrive first.
      auto e = book_.find_cid(v->cid);
      if (e && e->state != State::Filled)
        log_inf("Deribit says label=%lu is filled (filled_amount %s); %d of %d units reported so far", v->cid,
                str(o, "filled_amount"), e->cum, e->qty);
    }
    else if (state == "cancelled")
    {
      ack(*v);
      auto res = book_.on_canceled(v->cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
      log_omg("Deribit cancelled label=%lu (cancel_reason '%s') filled %d: %s", v->cid, str(o, "cancel_reason"),
              res.entry ? res.entry->cum : 0, to_string(res.outcome));
    }
    else if (state == "rejected")
    {
      auto res = book_.on_rejected(v->cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), som::Reject::UNKNOWN), this);
      log_rej("Deribit order label=%lu rejected (reject_post_only %s): %s", v->cid, str(o, "reject_post_only"),
              to_string(res.outcome));
    }
    else
      log_inf("Deribit order label=%lu state '%s' ignored", v->cid, state);
  }

  void DeribitOM::on_trade(const json &t) noexcept
  {
    const std::string trade_id = str(t, "trade_id");
    Venue *v = find(t);
    if (!v)
    {
      log_err("FILL ON AN ORDER OUTSIDE THIS OM: %s order_id=%s label=%s trade_id=%s %s amount %s @ %s: CHECK POSITIONS",
              str(t, "instrument_name"), str(t, "order_id"), str(t, "label"), trade_id, str(t, "direction"),
              str(t, "amount"), str(t, "price"));
      return;
    }
    learn_order_id(*v, t);
    if (trade_id.empty())
    {
      log_err("Deribit trade without trade_id on label=%lu: %s", v->cid, t.dump());
      return;
    }
    const double amount = num(t, "amount");
    if (std::isnan(amount) || amount <= 0)
    {
      log_err("Deribit trade %s on label=%lu with amount '%s' not understood: CHECK POSITIONS", trade_id, v->cid,
              str(t, "amount"));
      return;
    }
    if (!v->trades.insert(trade_id).second)
    {
      log_inf("Deribit trade %s on label=%lu already applied, skipped", trade_id, v->cid);
      return;
    }
    bool exact = true;
    const int64_t units = units_of(amount, v->ins->qty_unit, exact);
    if (!exact || units <= 0)
      log_err("Deribit trade %s amount %s on label=%lu is not a whole number of units of %s: applied as %ld; CHECK "
              "POSITIONS",
              trade_id, str(t, "amount"), v->cid, v->ins->qty_unit, units);
    if (units <= 0)
      return;
    auto res = book_.on_fill(v->cid, int(units));
    if (res.outcome != Outcome::Ok)
    {
      log_err("fill trade_id=%s on label=%lu qty=%ld: %s: CHECK POSITIONS", trade_id, v->cid, units,
              to_string(res.outcome));
      return;
    }
    const auto &e = *res.entry;
    bool px_exact = true;
    const int64_t pxi = units_of(num(t, "price"), v->ins->tick, px_exact);
    if (!px_exact)
      log_wrn("Deribit fill price %s on label=%lu is not on the tick %s", str(t, "price"), v->cid, v->ins->tick);
    if (e.sender)
      e.sender->send(new som::Fill(uint(e.oid), v->ins->venue, uint(e.order.sym), int(pxi), e.order.side,
                                   double(units), double(e.leaves), e.order.owner, ms_to_ns(t, "timestamp")),
                     this);
    log_fil("fill oid=%d label=%lu trade_id=%s amount %s @ %s leaves %d (liquidity %s)", e.oid, v->cid, trade_id,
            str(t, "amount"), str(t, "price"), e.leaves, str(t, "liquidity"));
  }
}

// ---- factories ------------------------------------------------------------------------------------

actors::Actor *create_DeribitOM(deribit::oe::DeribitOMConfig cfg, actors::Actor *som,
                                deribit::oe::SessionFactory factory)
{
  std::string err;
  for (auto &i : cfg.instruments)
  {
    auto a = frame::ref::RefData::get_asset(i.asset);
    if (!a)
    {
      std::cerr << "DeribitOM: not created, asset " << i.asset << " is not in RefData. DERIBIT is unrouted."
                << std::endl;
      return nullptr;
    }
    i.sym = int(a->id);
  }
  if (!cfg.valid(err))
  {
    std::cerr << "DeribitOM: not created, bad config: " << err << ". DERIBIT is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? deribit::oe::SimDeribit::make_factory({}) : deribit::oe::ws_session_factory();
  return new deribit::oe::DeribitOM(std::move(cfg), som, std::move(factory));
}

actors::Actor *create_DeribitOM(const std::string &config_path, actors::Actor *som)
{
  deribit::oe::DeribitOMConfig cfg;
  std::string err;
  if (!deribit::oe::DeribitOMConfig::from_file(config_path, cfg, err))
  {
    std::cerr << "DeribitOM: not created, cannot read " << config_path << ": " << err << ". DERIBIT is unrouted."
              << std::endl;
    return nullptr;
  }
  return create_DeribitOM(std::move(cfg), som);
}
