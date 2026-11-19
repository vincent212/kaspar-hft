/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_oe/act/OkxOM.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"

#include "okx_oe/SimOkx.hpp"
#include "okx_oe/if/OkxOM.hpp"

namespace okx::oe
{
  namespace som = frame::som::msg;
  using frame::som::ClientId;
  using frame::som::ClientIdGen;
  using frame::som::Outcome;
  using frame::som::State;
  using frame::som::to_string;
  using msg::OkxReport;
  using json = nlohmann::json;

  // ---- session thread -> mailbox ----------------------------------------------------------

  class OkxOM::Events : public SessionEvents
  {
  public:
    Events(OkxOM *om, uint64_t session) : om_(om), session_(session) {}
    void logged_in() override { post(OkxReport::LOGGED_IN); }
    void login_rejected(const std::string &code, const std::string &m) override
    {
      post(OkxReport::LOGIN_REJECTED, code + " " + m);
    }
    void disconnected(const std::string &why) override { post(OkxReport::DISCONNECTED, why); }
    void busy() override { post(OkxReport::BUSY); }
    void available() override { post(OkxReport::AVAILABLE); }
    void op_result(const json &r) override { post(OkxReport::OP_RESULT, {}, r); }
    void order_update(const json &d) override { post(OkxReport::ORDER_UPDATE, {}, d); }
    void order_unknown(const std::string &cl) override { post(OkxReport::ORDER_UNKNOWN, cl); }

  private:
    void post(OkxReport::Kind k, std::string text = {}, json body = {})
    {
      auto r = new OkxReport(k, session_);
      r->text = std::move(text);
      r->body = std::move(body);
      om_->send(r, nullptr);
    }
    OkxOM *om_;
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

    bool all_digits(const std::string &s)
    {
      return !s.empty() && s.size() <= 19 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    // sCode -> SOM reject reason (docs-v5, "Error Code").
    int reject_reason(const std::string &code)
    {
      if (code == "50011" || code == "50061" || code == "51034")
        return som::Reject::THROTTLE;
      if (code == "51008" || code == "51006" || code == "51004" || code == "51020" || code == "51121" ||
          code == "51201" || code == "51202")
        return som::Reject::EXCHLIMIT;
      return som::Reject::UNKNOWN;
    }

    uint64_t ms_to_ns(const std::string &ms)
    {
      try
      {
        return uint64_t(std::stoull(ms)) * 1000000ull;
      }
      catch (...)
      {
        return 0;
      }
    }
  }

  // ---- lifecycle -----------------------------------------------------------------------------

  OkxOM::OkxOM(OkxOMConfig cfg, actors::Actor *som, SessionFactory factory, ClientIdGen ids)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), book_(ids)
  {
    strncpy(name, "OkxOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(OkxReport, report_handler);
  }

  OkxOM::~OkxOM()
  {
    stopping_ = true;
    reconnect_.cancel();
    drop_session();
  }

  std::string OkxOM::ord_id(int oid) const
  {
    auto e = book_.find_oid(oid);
    if (!e)
      return {};
    auto it = by_cid_.find(e->cid);
    return it == by_cid_.end() ? std::string() : it->second.ord_id;
  }

  void OkxOM::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(cfg_, *events_);
      ++sessions_created_;
      log_omg("OKX session %lu: logging in (%s, ws %s)", session_no_, cfg_.ep.demo ? "demo" : "production",
              cfg_.ep.ws_host);
      session_->logon();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create OKX session: %s", e.what());
      schedule_reconnect();
    }
  }

  void OkxOM::drop_session() noexcept
  {
    ready_ = false;
    busy_ = false;
    session_.reset(); // joins the session's thread: no events after this
    events_.reset();
    // Requests in flight on the dropped connection get no answer: a cancel is
    // refused now, a new order's outcome comes from recover_live().
    std::vector<ClientId> cids;
    for (const auto &[cid, v] : by_cid_)
      cids.push_back(cid);
    auto unconfirmed = oe_common::abandon_in_flight(book_, cids, this);
    if (!unconfirmed.empty())
      log_wrn("%zu new orders in flight at OKX: outcome known after recovery", unconfirmed.size());
    cancel_req_.clear();
  }

  void OkxOM::schedule_reconnect() noexcept
  {
    if (stopping_ || login_refused_)
      return;
    reconnect_.arm(cfg_.reconnect_ms, [this, expected = session_no_] {
      send(new OkxReport(OkxReport::RECONNECT, expected), nullptr);
    });
  }

  void OkxOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no OKX session factory: OKX is unrouted");
      return;
    }
    send(new OkxReport(OkxReport::RECONNECT, session_no_), nullptr);
    if (som_)
    {
      std::unordered_set<int> venues;
      for (const auto &i : cfg_.instruments)
        if (venues.insert(int(i.venue)).second)
          som_->send(new som::SetExchManager(i.venue, this), this);
    }
  }

  void OkxOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_.cancel();
    if (session_)
      session_->logout();
    drop_session();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at OKX", book_.live());
  }

  // ---- requests from the SOM ------------------------------------------------------------------

  void OkxOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    using TIF = som::Order::TimeInForce;
    if (m->oid_to_cancel >= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no OKX connection");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "OKX session not logged in");
    if (busy_)
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "OKX connection busy");
    const Instrument *ins = cfg_.by_sym(m->sym);
    if (!ins)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument not configured for OKX");
    if (ins->venue != m->venue)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "venue does not match the instrument");
    if (m->ord_type != OT::LIMIT)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "only limit orders are supported");
    const char *ord_type = nullptr;
    switch (m->tif)
    {
    case TIF::DAY:
    case TIF::GTC: ord_type = cfg_.post_only ? "post_only" : "limit"; break;
    case TIF::IOC: ord_type = "ioc"; break;
    case TIF::FOK: ord_type = "fok"; break;
    default: break;
    }
    if (!ord_type)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on OKX");
    if (m->short_sale != som::Order::ShortSale::NONE)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "OKX has no short-sale marking");
    if (!m->route.empty() && m->route != "OKX")
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "OkxOM routes to OKX only");
    if (!m->strategy.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "OKX has no broker algorithms");
    if (!(m->sz > 0) || std::floor(m->sz) != m->sz || m->sz > 2e9)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a positive whole number of units");
    if (cfg_.max_order_units > 0 && m->sz > cfg_.max_order_units)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order size limit");
    if (m->px <= 0)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "price must be positive");
    const double notional = m->sz * ins->unit_d() * m->px * ins->tick_d();
    if (cfg_.max_order_notional > 0 && notional > cfg_.max_order_notional)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
    if (!tokens_.take(cfg_.max_orders_per_sec))
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    auto &e = *r.entry;
    const std::string cl = std::to_string(e.cid);
    json arg = {{"instIdCode", ins->inst_id_code},
                {"tdMode", ins->td_mode},
                {"clOrdId", cl},
                {"side", m->side == en::bs::BUY ? "buy" : "sell"},
                {"ordType", ord_type},
                {"sz", scaled(int64_t(e.qty), ins->qty_unit)},
                {"px", scaled(int64_t(m->px), ins->tick)}};
    switch (session_->place("n" + cl, arg))
    {
    case CallStatus::Success:
    {
      auto &v = by_cid_[e.cid];
      v.cid = e.cid;
      v.ins = ins;
      log_omg("new order oid=%d clOrdId=%s %s %s %s @ %s %s", m->oid, cl, ins->inst_id, arg["side"].get<std::string>(),
              arg["sz"].get<std::string>(), arg["px"].get<std::string>(), ord_type);
      return;
    }
    case CallStatus::Busy:
      book_.send_failed(e.cid);
      busy_ = true;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "OKX connection busy");
    case CallStatus::Disconnected:
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "OKX connection lost");
    }
  }

  void OkxOM::cancel_handler(const som::Cancel *m) noexcept
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
      return cancel_reject(m->sender, oid, oe_common::late_cancel_reason(r.entry->state), "order already done");
    default:
      return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "no such order");
    }
    auto &e = *r.entry;
    auto vit = by_cid_.find(e.cid);
    if (vit == by_cid_.end()) // cannot happen: every order in the book was sent
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "order was never sent");
    }
    auto &v = vit->second;
    json arg = {{"instIdCode", v.ins->inst_id_code}, {"clOrdId", std::to_string(e.cid)}};
    if (!v.ord_id.empty())
      arg["ordId"] = v.ord_id;
    const std::string req = "c" + std::to_string(e.cid) + "r" + std::to_string(++v.cancels);
    auto st = session_ && ready_ ? session_->cancel(req, arg) : CallStatus::Disconnected;
    if (st != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      if (st == CallStatus::Busy)
        busy_ = true;
      return cancel_reject(m->sender, oid,
                           st == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                           "OKX refused the cancel");
    }
    cancel_req_[req] = e.pending_cid;
    log_omg("cancel oid=%d clOrdId=%lu", oid, e.cid);
  }

  // ---- reports ---------------------------------------------------------------------------------

  void OkxOM::report_handler(const OkxReport *m) noexcept
  {
    if (m->kind == OkxReport::RECONNECT)
    {
      if (!stopping_ && !login_refused_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->session != session_no_ || !session_)
      return; // from a session already dropped

    switch (m->kind)
    {
    case OkxReport::LOGGED_IN:
      ready_ = true;
      log_omg("OKX logged in, orders channel subscribed (%zu orders live)", book_.live());
      recover_live();
      return;
    case OkxReport::LOGIN_REJECTED:
      log_err("OKX login rejected: %s. OKX is unrouted until restart.", m->text);
      login_refused_ = true;
      drop_session();
      return;
    case OkxReport::DISCONNECTED:
      log_err("OKX connection lost: %s; reconnecting in %d ms (%zu orders live)", m->text, cfg_.reconnect_ms,
              book_.live());
      drop_session();
      schedule_reconnect();
      return;
    case OkxReport::BUSY:
      busy_ = true;
      log_wrn("OKX connection busy: refusing orders");
      return;
    case OkxReport::AVAILABLE:
      busy_ = false;
      log_omg("OKX connection available");
      return;
    case OkxReport::OP_RESULT: on_op_result(m->body); return;
    case OkxReport::ORDER_UPDATE: on_order_update(m->body); return;
    case OkxReport::ORDER_UNKNOWN:
      if (all_digits(m->text) && oe_common::reject_unknown(book_, std::stoull(m->text), this))
        log_rej("OKX does not know clOrdId=%s: the order never reached it", m->text);
      else
        log_inf("OKX does not know clOrdId=%s: nothing to do", m->text);
      return;
    default: return;
    }
  }

  // Orders the book still has live may have filled or ended while the
  // connection was down; the orders channel does not resend.
  void OkxOM::recover_live() noexcept
  {
    for (auto &[cid, v] : by_cid_)
    {
      auto e = book_.find_cid(cid);
      if (!e || frame::som::is_done(e->state))
        continue;
      if (session_->recover(v.ins->inst_id, v.ord_id, std::to_string(cid)) != CallStatus::Success)
        log_err("OKX recovery of clOrdId=%lu not sent", cid);
    }
  }

  void OkxOM::ack(Venue &v) noexcept
  {
    if (v.acked)
      return;
    uint64_t xid = 0;
    try
    {
      xid = v.ord_id.empty() ? 0 : std::stoull(v.ord_id);
    }
    catch (...)
    {
    }
    auto res = book_.on_accepted(v.cid, xid);
    if (res.outcome != Outcome::Ok)
    {
      log_wrn("accept of clOrdId=%lu: %s", v.cid, to_string(res.outcome));
      return;
    }
    v.acked = true;
    if (res.entry->sender)
      res.entry->sender->send(new som::Ack(uint(res.entry->oid), res.entry->exch_oid), this);
  }

  void OkxOM::on_op_result(const json &r) noexcept
  {
    const std::string id = str(r, "id"), op = str(r, "op"), code = str(r, "code");
    const json d = r.contains("data") && r["data"].is_array() && !r["data"].empty() ? r["data"][0] : json::object();
    const std::string scode = d.empty() ? code : str(d, "sCode");
    const bool ok = code == "0" && scode == "0";
    const std::string why = d.empty() ? str(r, "msg") : str(d, "sMsg");

    if (op == "order")
    {
      if (id.size() < 2 || id[0] != 'n' || !all_digits(id.substr(1)))
      {
        log_err("OKX order result for request '%s', not one of ours", id);
        return;
      }
      const ClientId cid = std::stoull(id.substr(1));
      auto vit = by_cid_.find(cid);
      if (vit == by_cid_.end())
      {
        log_err("OKX order result for clOrdId=%lu, unknown to this OM", cid);
        return;
      }
      auto &v = vit->second;
      if (ok)
      {
        v.ord_id = str(d, "ordId");
        if (!v.ord_id.empty())
          cid_of_ord_[v.ord_id] = cid;
        ack(v);
        return;
      }
      auto res = book_.on_rejected(cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), reject_reason(scode)), this);
      log_rej("OKX rejected clOrdId=%lu: code %s sCode %s %s (%s)", cid, code, scode, why, to_string(res.outcome));
      return;
    }
    if (op == "cancel-order")
    {
      auto it = cancel_req_.find(id);
      if (it == cancel_req_.end())
      {
        log_err("OKX cancel result for request '%s', not one of ours", id);
        return;
      }
      const ClientId pending = it->second;
      cancel_req_.erase(it);
      if (ok)
      {
        log_omg("OKX accepted the cancel request %s; waiting for the orders channel", id);
        return;
      }
      auto res = book_.on_cancel_rejected(pending);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::FROMEXCHANGE), this);
      log_rej("OKX refused cancel %s: sCode %s %s (%s)", id, scode, why, to_string(res.outcome));
      return;
    }
    log_wrn("OKX result for op '%s' id '%s' ignored", op, id);
  }

  OkxOM::Venue *OkxOM::find(const json &d) noexcept
  {
    const std::string cl = str(d, "clOrdId");
    if (all_digits(cl))
    {
      auto it = by_cid_.find(std::stoull(cl));
      if (it != by_cid_.end())
        return &it->second;
    }
    const std::string oid = str(d, "ordId");
    if (!oid.empty())
    {
      auto it = cid_of_ord_.find(oid);
      if (it != cid_of_ord_.end())
        return &by_cid_[it->second];
    }
    return nullptr;
  }

  void OkxOM::on_order_update(const json &d) noexcept
  {
    Venue *v = find(d);
    const std::string state = str(d, "state");
    if (!v)
    {
      const std::string fs = str(d, "fillSz");
      if (!fs.empty() && fs != "0" && !str(d, "tradeId").empty())
        log_err("FILL ON AN ORDER OUTSIDE THIS OM: %s ordId=%s clOrdId=%s tradeId=%s %s @ %s: CHECK POSITIONS",
                str(d, "instId"), str(d, "ordId"), str(d, "clOrdId"), str(d, "tradeId"), fs, str(d, "fillPx"));
      else
        log_inf("OKX order %s (clOrdId '%s') state %s: not this OM's, ignored", str(d, "ordId"), str(d, "clOrdId"),
                state);
      return;
    }
    if (v->ord_id.empty())
    {
      v->ord_id = str(d, "ordId");
      if (!v->ord_id.empty())
        cid_of_ord_[v->ord_id] = v->cid;
    }
    // A fill first: one update can carry the last fill and state "filled".
    apply_fill(*v, d);

    if (state == "live" || state == "partially_filled")
      ack(*v);
    else if (state == "canceled" || state == "mmp_canceled")
    {
      ack(*v); // canceled before the op result arrived: Ack, then CancAck
      auto res = book_.on_canceled(v->cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
      log_omg("OKX canceled clOrdId=%lu (cancelSource '%s') filled %d: %s", v->cid, str(d, "cancelSource"),
              res.entry ? res.entry->cum : 0, to_string(res.outcome));
    }
    else if (state == "filled")
    {
      auto e = book_.find_cid(v->cid);
      if (e && e->state != State::Filled)
        log_err("OKX says clOrdId=%lu is filled (accFillSz %s) but %d of %d units were reported: CHECK POSITIONS",
                v->cid, str(d, "accFillSz"), e->cum, e->qty);
    }
  }

  void OkxOM::apply_fill(Venue &v, const json &d) noexcept
  {
    const std::string fs = str(d, "fillSz"), trade = str(d, "tradeId");
    if (fs.empty() || trade.empty())
      return;
    bool exact = true;
    int64_t units = 0;
    try
    {
      if (std::stod(fs) <= 0)
        return;
      units = units_of(fs, v.ins->qty_unit, exact);
    }
    catch (...)
    {
      log_err("OKX fill with fillSz '%s' on clOrdId=%lu not understood: CHECK POSITIONS", fs, v.cid);
      return;
    }
    if (!v.trades.insert(trade).second)
    {
      log_inf("OKX fill tradeId=%s on clOrdId=%lu already applied, skipped", trade, v.cid);
      return;
    }
    if (!exact || units <= 0)
      log_err("OKX fill of %s on clOrdId=%lu is not a whole number of units of %s: applied as %ld; CHECK POSITIONS",
              fs, v.cid, v.ins->qty_unit, units);
    if (units <= 0)
      return;
    auto res = book_.on_fill(v.cid, int(units));
    if (res.outcome != Outcome::Ok)
    {
      log_err("fill tradeId=%s on clOrdId=%lu qty=%ld: %s: CHECK POSITIONS", trade, v.cid, units,
              to_string(res.outcome));
      return;
    }
    const auto &e = *res.entry;
    bool px_exact = true;
    const int64_t pxi = units_of(str(d, "fillPx"), v.ins->tick, px_exact);
    if (!px_exact)
      log_wrn("OKX fill price %s on clOrdId=%lu is not on the tick %s", str(d, "fillPx"), v.cid, v.ins->tick);
    if (e.sender)
      e.sender->send(new som::Fill(uint(e.oid), v.ins->venue, uint(e.order.sym), int(pxi), e.order.side, double(units),
                                   double(e.leaves), e.order.owner, ms_to_ns(str(d, "fillTime"))),
                     this);
    log_fil("fill oid=%d clOrdId=%lu tradeId=%s %s @ %s leaves %d (%s)", e.oid, v.cid, trade, fs, str(d, "fillPx"),
            e.leaves, str(d, "execType"));
  }
}

// ---- factories -------------------------------------------------------------------------------

actors::Actor *create_OkxOM(okx::oe::OkxOMConfig cfg, actors::Actor *som, okx::oe::SessionFactory factory)
{
  std::string err;
  for (auto &i : cfg.instruments)
  {
    auto a = frame::ref::RefData::get_asset(i.asset);
    if (!a)
    {
      std::cerr << "OkxOM: not created, asset " << i.asset << " is not in RefData. OKX is unrouted." << std::endl;
      return nullptr;
    }
    i.sym = int(a->id);
  }
  if (!cfg.valid(err))
  {
    std::cerr << "OkxOM: not created, bad config: " << err << ". OKX is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? okx::oe::SimOkx::make_factory({}) : okx::oe::ws_session_factory();
  return new okx::oe::OkxOM(std::move(cfg), som, std::move(factory));
}

actors::Actor *create_OkxOM(const std::string &config_path, actors::Actor *som)
{
  okx::oe::OkxOMConfig cfg;
  std::string err;
  if (!okx::oe::OkxOMConfig::from_file(config_path, cfg, err))
  {
    std::cerr << "OkxOM: not created, cannot read " << config_path << ": " << err << ". OKX is unrouted."
              << std::endl;
    return nullptr;
  }
  return create_OkxOM(std::move(cfg), som);
}
