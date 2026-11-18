/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lime_om/act/LimeOM.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include "frame/ref/Price.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"

namespace lime
{
  namespace som = frame::som::msg;
  using frame::som::Outcome;
  using frame::som::Result;
  using frame::som::State;
  using frame::som::to_string;
  using msg::LimeEvent;

  const char *to_string(Side s)
  {
    switch (s)
    {
    case Side::Buy: return "BUY";
    case Side::Sell: return "SELL";
    case Side::SellShort: return "SELL_SHORT";
    case Side::SellShortExempt: return "SELL_SHORT_EXEMPT";
    case Side::BuyToCover: return "BUY_TO_COVER";
    }
    return "?";
  }

  const char *to_string(Tif t)
  {
    switch (t)
    {
    case Tif::Day: return "DAY";
    case Tif::Opg: return "OPG";
    case Tif::Ioc: return "IOC";
    case Tif::ExtendedDay: return "EXTENDED_DAY";
    case Tif::AtTheClose: return "AT_THE_CLOSE";
    }
    return "?";
  }

  const char *to_string(ExecType t)
  {
    switch (t)
    {
    case ExecType::Fill: return "FILL";
    case ExecType::Bust: return "BUST";
    case ExecType::Correction: return "CORRECTION";
    }
    return "?";
  }

  const char *msg::to_string(LimeEvent::Kind k)
  {
    switch (k)
    {
    case LimeEvent::LOGIN_ACCEPTED: return "LOGIN_ACCEPTED";
    case LimeEvent::LOGIN_FAILED: return "LOGIN_FAILED";
    case LimeEvent::CONNECTION_FAILED: return "CONNECTION_FAILED";
    case LimeEvent::BUSY: return "BUSY";
    case LimeEvent::AVAILABLE: return "AVAILABLE";
    case LimeEvent::ACCEPTED: return "ACCEPTED";
    case LimeEvent::FILLED: return "FILLED";
    case LimeEvent::CANCELED: return "CANCELED";
    case LimeEvent::PARTIAL_CANCELED: return "PARTIAL_CANCELED";
    case LimeEvent::REPLACED: return "REPLACED";
    case LimeEvent::REJECTED: return "REJECTED";
    case LimeEvent::CANCEL_REJECTED: return "CANCEL_REJECTED";
    case LimeEvent::REPLACE_REJECTED: return "REPLACE_REJECTED";
    case LimeEvent::MANUAL_ORDER: return "MANUAL_ORDER";
    case LimeEvent::MANUAL_REPLACE: return "MANUAL_REPLACE";
    case LimeEvent::RECONNECT: return "RECONNECT";
    }
    return "?";
  }

  // ---- callback thread -> mailbox -----------------------------------------------------------

  class LimeOM::Events : public SessionEvents
  {
  public:
    Events(LimeOM *om, uint64_t session) : om_(om), session_(session) {}

    void login_accepted(uint64_t ev) override { post(make(LimeEvent::LOGIN_ACCEPTED, ev)); }
    void login_failed(const std::string &why) override { post(make(LimeEvent::LOGIN_FAILED, 0, why)); }
    void connection_failed(const std::string &why) override { post(make(LimeEvent::CONNECTION_FAILED, 0, why)); }
    void busy() override { post(make(LimeEvent::BUSY, 0)); }
    void available() override { post(make(LimeEvent::AVAILABLE, 0)); }

    void accepted(OrderId id, OrderId lime_id, uint64_t ev) override
    {
      auto m = make(LimeEvent::ACCEPTED, ev);
      m->id = id;
      m->lime_id = lime_id;
      post(m);
    }
    void filled(OrderId id, const FillInfo &f, uint64_t ev) override
    {
      auto m = make(LimeEvent::FILLED, ev);
      m->id = id;
      m->fill = f;
      post(m);
    }
    void canceled(OrderId id, const std::string &why, uint64_t ev) override
    {
      auto m = make(LimeEvent::CANCELED, ev, why);
      m->id = id;
      post(m);
    }
    void partial_canceled(OrderId id, int left, uint64_t ev) override
    {
      auto m = make(LimeEvent::PARTIAL_CANCELED, ev);
      m->id = id;
      m->quantity = left;
      post(m);
    }
    void replaced(OrderId id, OrderId rid, OrderId lime_id, uint64_t ev) override
    {
      auto m = make(LimeEvent::REPLACED, ev);
      m->id = id;
      m->replace_id = rid;
      m->lime_id = lime_id;
      post(m);
    }
    void rejected(OrderId id, const std::string &why, uint64_t ev) override
    {
      auto m = make(LimeEvent::REJECTED, ev, why);
      m->id = id;
      post(m);
    }
    void cancel_rejected(OrderId id, const std::string &why, uint64_t ev) override
    {
      auto m = make(LimeEvent::CANCEL_REJECTED, ev, why);
      m->id = id;
      post(m);
    }
    void replace_rejected(OrderId id, OrderId rid, const std::string &why, uint64_t ev) override
    {
      auto m = make(LimeEvent::REPLACE_REJECTED, ev, why);
      m->id = id;
      m->replace_id = rid;
      post(m);
    }
    void manual_order(OrderId id, const std::string &sym, const std::string &route, int qty, ScaledPrice px,
                      Side side, uint64_t ev) override
    {
      auto m = make(LimeEvent::MANUAL_ORDER, ev);
      m->id = id;
      m->symbol = sym;
      m->route = route;
      m->quantity = qty;
      m->price = px;
      m->side = side;
      post(m);
    }
    void manual_replace(OrderId id, OrderId rid, int qty, ScaledPrice px, uint64_t ev) override
    {
      auto m = make(LimeEvent::MANUAL_REPLACE, ev);
      m->id = id;
      m->replace_id = rid;
      m->quantity = qty;
      m->price = px;
      post(m);
    }

  private:
    LimeEvent *make(LimeEvent::Kind k, uint64_t ev, std::string text = {})
    {
      auto m = new LimeEvent(k, session_);
      m->event_id = ev;
      m->text = std::move(text);
      return m;
    }
    void post(LimeEvent *m) { om_->send(m, nullptr); }
    LimeOM *om_;
    uint64_t session_;
  };

  // ---- units -----------------------------------------------------------------------------------

  ScaledPrice LimeOM::to_scaled(int ticks, double tick_size)
  {
    long double d = (long double)ticks * tick_size * PRICE_SCALE;
    return d <= 0 ? 0 : ScaledPrice(std::llround(d));
  }

  namespace
  {
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
  }

  // ---- lifecycle -------------------------------------------------------------------------------

  LimeOM::LimeOM(LimeOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
                 frame::som::ClientIdGen ids)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), book_(ids)
  {
    strncpy(name, "LimeOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(LimeEvent, event_handler);
  }

  LimeOM::~LimeOM()
  {
    stopping_ = true;
    reconnect_timer_ = {};
    drop_session();
  }

  void LimeOM::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    LoginParams p;
    p.account = cfg_.account;
    p.user = cfg_.user;
    p.password = cfg_.password;
    p.host = cfg_.host;
    p.infiniband = cfg_.infiniband;
    p.cancel_all_on_disconnect = cfg_.cancel_all_on_disconnect;
    // Resume after the last event applied (TO 3). With none applied yet but
    // orders sent, replay the day: their events may have been missed.
    if (last_event_)
      p.event_id = last_event_ + 1;
    else if (book_.size())
      p.event_id = 1;
    else
      p.event_id = cfg_.first_event_id;
    login_event_id_ = p.event_id;
    try
    {
      log_omg("Lime session %lu: logging in to account '%s' as '%s', eventId %lu", session_no_, cfg_.account,
              cfg_.user, p.event_id);
      ++sessions_created_;
      session_ = factory_(p, *events_);
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create Lime session: %s", e.what());
      schedule_reconnect();
    }
  }

  void LimeOM::drop_session() noexcept
  {
    logged_in_ = false;
    busy_ = false;
    session_.reset(); // joins the callback thread: no events after this
    events_.reset();
  }

  void LimeOM::schedule_reconnect() noexcept
  {
    if (stopping_ || down_)
      return;
    auto expected = session_no_;
    auto ms = cfg_.reconnect_ms;
    reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> l(m);
      if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
        send(new LimeEvent(LimeEvent::RECONNECT, expected), nullptr);
    });
  }

  void LimeOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no Lime session factory: the venue is unrouted");
      return;
    }
    send(new LimeEvent(LimeEvent::RECONNECT, session_no_), nullptr);
    if (som_)
      som_->send(new som::SetExchManager(venue_, this), this);
  }

  void LimeOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    drop_session();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at Lime%s", book_.live(),
              cfg_.cancel_all_on_disconnect ? " (cancel on disconnect is on)" : "");
  }

  // ---- requests from the SOM -------------------------------------------------------------------

  void LimeOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void LimeOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool LimeOM::take_token() noexcept
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

  void LimeOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    using TIF = som::Order::TimeInForce;
    using SS = som::Order::ShortSale;
    if (m->oid_to_cancel >= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Lime connection");
    if (!logged_in_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Lime session not logged in");
    if (busy_)
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Lime connection busy");

    NewOrder o;
    // Order type: the API knows limit (price > 0) and market (price 0) only (TO 4.1).
    bool market = false;
    switch (m->ord_type)
    {
    case OT::LIMIT: break;
    case OT::MARKET:
      if (!cfg_.allow_market)
        return reject(m->sender, m->oid, som::Reject::UNKNOWN, "market orders are disabled");
      market = true;
      break;
    default: return reject(m->sender, m->oid, som::Reject::UNKNOWN, "the Lime C++ API has no stop orders");
    }
    switch (m->tif)
    {
    case TIF::DAY: o.tif = Tif::Day; break;
    case TIF::IOC: o.tif = Tif::Ioc; break;
    case TIF::AT_OPEN: o.tif = Tif::Opg; break;
    case TIF::AT_CLOSE: o.tif = Tif::AtTheClose; break;
    case TIF::EXTENDED_DAY: o.tif = Tif::ExtendedDay; break;
    default: return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available for Lime equities");
    }
    if (m->side == en::bs::BUY)
      o.side = Side::Buy;
    else
    {
      switch (m->short_sale)
      {
      case SS::NONE: o.side = Side::Sell; break;
      case SS::SHORT: o.side = Side::SellShort; break;
      case SS::SHORT_EXEMPT: o.side = Side::SellShortExempt; break;
      }
      if (o.side != Side::Sell && !cfg_.allow_short)
        return reject(m->sender, m->oid, som::Reject::UNKNOWN, "short sales are disabled");
      o.sale_affirm = cfg_.sale_affirm;
    }
    if (!m->locate_id.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "the Lime C++ API has no locate field");
    if (!m->strategy.empty())
    {
      if (!cfg_.algo_strategies.count(m->strategy))
        return reject(m->sender, m->oid, som::Reject::UNKNOWN, "algo strategy not allowed");
      o.strategy = m->strategy;
      o.route = m->route.empty() ? cfg_.algo_route : m->route;
      if (o.route.empty())
        return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no algo route configured");
    }
    else
      o.route = m->route.empty() ? cfg_.default_route : m->route;
    if (o.route.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no route");
    o.regular_session_only = cfg_.regular_session_only && o.tif == Tif::Day;

    auto a = frame::ref::RefData::get_asset(std::size_t(m->sym));
    if (!a)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "unknown instrument");
    o.symbol = a->name;
    if (o.symbol.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument has no symbol");
    if (!(m->sz > 0) || m->sz > INT_MAX || std::floor(m->sz) != m->sz)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a positive whole number of shares");
    o.quantity = int(m->sz);
    if (cfg_.max_order_qty > 0 && o.quantity > cfg_.max_order_qty)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order quantity limit");
    if (!market)
    {
      o.price = to_scaled(m->px, a->get_units());
      if (o.price <= 0)
        return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "limit price must be positive");
      double notional = double(o.price) / PRICE_SCALE * o.quantity;
      if (cfg_.max_order_notional > 0 && notional > cfg_.max_order_notional)
        return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
    }
    if (!take_token())
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    auto &e = *r.entry;
    o.id = e.cid;

    CallStatus st;
    try
    {
      st = session_->place(o);
    }
    catch (const std::exception &x)
    {
      // TO 6: delete the instance after an exception.
      book_.send_failed(e.cid);
      reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Lime API threw");
      log_err("Lime API exception on placeOrder: %s; reconnecting", x.what());
      drop_session();
      return schedule_reconnect();
    }
    switch (st)
    {
    case CallStatus::Success:
      log_omg("new order oid=%d id=%lu %s %d %s @ %ld route %s%s%s tif %s", m->oid, o.id, to_string(o.side),
              o.quantity, o.symbol, long(o.price), o.route, o.strategy.empty() ? "" : " algo ", o.strategy,
              to_string(o.tif));
      return;
    case CallStatus::ConnectionBusy:
      book_.send_failed(e.cid);
      busy_ = true;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Lime connection busy");
    case CallStatus::ConnectionError:
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Lime connection error");
    }
  }

  void LimeOM::cancel_handler(const som::Cancel *m) noexcept
  {
    int oid = int(m->id);
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
    if (!session_ || !logged_in_)
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, session_ ? som::CancReject::NOTLOGGEDIN : som::CancReject::DISCONNECTED,
                           "no Lime connection");
    }
    CallStatus st;
    try
    {
      st = session_->cancel(e.cid); // by the order's own id (TO 4.5)
    }
    catch (const std::exception &x)
    {
      book_.send_failed(e.pending_cid);
      cancel_reject(m->sender, oid, som::CancReject::DISCONNECTED, "Lime API threw");
      log_err("Lime API exception on cancelOrder: %s; reconnecting", x.what());
      drop_session();
      return schedule_reconnect();
    }
    if (st != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      if (st == CallStatus::ConnectionBusy)
        busy_ = true;
      return cancel_reject(m->sender, oid,
                           st == CallStatus::ConnectionBusy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                           "Lime refused the cancel call");
    }
    log_omg("cancel oid=%d id=%lu", oid, e.cid);
  }

  // ---- events from Lime --------------------------------------------------------------------------

  bool LimeOM::fresh(const LimeEvent &m) noexcept
  {
    if (m.event_id == 0)
      return true; // generated in the API itself; never replayed
    if (m.event_id <= last_event_)
    {
      log_inf("Lime %s eventId %lu already applied (last %lu): skipped", msg::to_string(m.kind), m.event_id,
              last_event_);
      return false;
    }
    last_event_ = m.event_id;
    return true;
  }

  void LimeOM::event_handler(const LimeEvent *m) noexcept
  {
    if (m->kind == LimeEvent::RECONNECT)
    {
      if (!stopping_ && !down_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->session != session_no_ || !session_)
      return; // from a session already dropped

    switch (m->kind)
    {
    case LimeEvent::LOGIN_ACCEPTED:
      logged_in_ = true;
      busy_ = false;
      log_omg("Lime login accepted (eventId %lu), %zu orders live", m->event_id, book_.live());
      return;
    case LimeEvent::LOGIN_FAILED:
      log_err("Lime login failed: %s; the venue is unrouted until restart", m->text);
      drop_session();
      down_ = true;
      return;
    case LimeEvent::CONNECTION_FAILED:
      log_err("Lime connection failed: %s; reconnecting in %d ms (%zu orders live)", m->text, cfg_.reconnect_ms,
              book_.live());
      drop_session();
      schedule_reconnect();
      return;
    case LimeEvent::BUSY:
      busy_ = true;
      log_wrn("Lime connection busy: refusing orders");
      return;
    case LimeEvent::AVAILABLE:
      busy_ = false;
      log_omg("Lime connection available");
      return;
    default:
      break;
    }

    if (!fresh(*m))
      return;
    switch (m->kind)
    {
    case LimeEvent::ACCEPTED: return on_accepted(*m);
    case LimeEvent::FILLED: return on_filled(*m);
    case LimeEvent::CANCELED: return on_canceled(*m);
    case LimeEvent::REJECTED: return on_rejected(*m);
    case LimeEvent::CANCEL_REJECTED: return on_cancel_rejected(*m);
    case LimeEvent::PARTIAL_CANCELED:
      log_err("Lime partial cancel of id=%lu (left %d), which this OM never requests: ignored", m->id, m->quantity);
      return;
    case LimeEvent::REPLACED:
      log_err("Lime replace of id=%lu by id=%lu, which this OM never requests: ignored. CHECK ORDERS", m->id,
              m->replace_id);
      return;
    case LimeEvent::REPLACE_REJECTED:
      log_err("Lime replace reject id=%lu -> %lu (%s), which this OM never requests: ignored", m->id,
              m->replace_id, m->text);
      return;
    case LimeEvent::MANUAL_ORDER:
      log_wrn("Lime order id=%lu entered outside this OM: %s %d %s @ %ld route %s (not tracked)", m->id,
              to_string(m->side), m->quantity, m->symbol, long(m->price), m->route);
      return;
    case LimeEvent::MANUAL_REPLACE:
      log_wrn("Lime replace id=%lu -> %lu entered outside this OM: %d @ %ld (not tracked)", m->id, m->replace_id,
              m->quantity, long(m->price));
      return;
    default:
      log_err("Lime event %s not handled", msg::to_string(m->kind));
    }
  }

  void LimeOM::on_accepted(const LimeEvent &m) noexcept
  {
    auto e0 = book_.find_cid(m.id);
    bool acked_before = e0 && e0->exch_oid != 0;
    auto res = book_.on_accepted(m.id, m.lime_id);
    if (res.outcome == Outcome::Unknown)
    {
      log_err("Lime accept of id=%lu (lime id %lu), unknown to this OM", m.id, m.lime_id);
      return;
    }
    if (res.outcome != Outcome::Ok)
    {
      log_wrn("Lime accept of id=%lu: %s (order %s)", m.id, to_string(res.outcome), to_string(res.entry->state));
      return;
    }
    if (acked_before)
      return;
    auto &e = *res.entry;
    if (e.sender)
      e.sender->send(new som::Ack(uint(e.oid), e.exch_oid), this);
  }

  void LimeOM::on_filled(const LimeEvent &m) noexcept
  {
    const FillInfo &f = m.fill;
    if (f.exec_type != ExecType::Fill)
    {
      // A bust reverses an earlier fill; a correction is a bust's
      // replacement or an administrative fill (Java Callback.onCorrect).
      // Neither is passed to the SOM as a fill.
      log_err("Lime %s on id=%lu exec %s: %d %s @ %ld. NOT APPLIED: CHECK POSITIONS", to_string(f.exec_type), m.id,
              f.exec_id, f.last_shares, f.symbol, long(f.last_price));
      return;
    }
    auto e0 = book_.find_cid(m.id);
    if (!e0)
    {
      log_err("FILL ON UNKNOWN ORDER id=%lu exec %s: %s %d %s @ %ld. CHECK POSITIONS", m.id, f.exec_id,
              to_string(f.side), f.last_shares, f.symbol, long(f.last_price));
      return;
    }
    if (!f.exec_id.empty() && !exec_ids_[m.id].insert(f.exec_id).second)
    {
      log_wrn("Lime exec %s on id=%lu already applied: skipped", f.exec_id, m.id);
      return;
    }
    auto res = book_.on_fill(m.id, f.last_shares);
    if (res.outcome != Outcome::Ok)
    {
      log_err("Lime fill on id=%lu exec %s qty %d: %s (order %s). CHECK POSITIONS", m.id, f.exec_id, f.last_shares,
              to_string(res.outcome), res.entry ? to_string(res.entry->state) : "-");
      return;
    }
    auto &e = *res.entry;
    if (f.left_qty != e.leaves && !frame::som::is_done(e.state))
      log_wrn("Lime fill on id=%lu says %d left, the book has %d", m.id, f.left_qty, e.leaves);
    const auto &o = e.order;
    uint64_t ns = f.transact_time_ns ? f.transact_time_ns : f.transact_time_ms * 1'000'000ULL;
    if (e.sender)
    {
      auto fill = new som::Fill(uint(e.oid), venue_, uint(o.sym),
                                frame::ref::Price((long double)f.last_price / PRICE_SCALE, uint(o.sym)), o.side,
                                double(f.last_shares), double(e.leaves), o.owner, ns);
      fill->xordid = e.exch_oid;
      e.sender->send(fill, this);
    }
    log_fil("fill oid=%d id=%lu %d @ %ld leaves %d exec %s mkt %s", e.oid, m.id, f.last_shares, long(f.last_price),
            e.leaves, f.exec_id, f.last_market);
  }

  void LimeOM::on_canceled(const LimeEvent &m) noexcept
  {
    auto res = book_.on_canceled(m.id);
    if (res.outcome == Outcome::Unknown)
    {
      log_err("Lime cancel of id=%lu (%s), unknown to this OM", m.id, m.text);
      return;
    }
    if (res.outcome != Outcome::Ok)
    {
      log_inf("Lime cancel of id=%lu: %s (order %s)", m.id, to_string(res.outcome), to_string(res.entry->state));
      return;
    }
    auto &e = *res.entry;
    if (e.sender)
      e.sender->send(new som::CancAck(uint(e.oid), e.cum, 0), this);
    log_omg("order oid=%d id=%lu cancelled%s%s, filled %d", e.oid, m.id, m.text.empty() ? "" : ": ", m.text, e.cum);
  }

  void LimeOM::on_rejected(const LimeEvent &m) noexcept
  {
    auto res = book_.on_rejected(m.id);
    if (res.outcome == Outcome::Unknown)
    {
      log_err("Lime reject of id=%lu (%s), unknown to this OM", m.id, m.text);
      return;
    }
    if (res.outcome != Outcome::Ok)
    {
      log_err("Lime reject of id=%lu (%s): %s (order %s)", m.id, m.text, to_string(res.outcome),
              to_string(res.entry->state));
      return;
    }
    auto &e = *res.entry;
    // Reason strings are for people, never for code (Java Callback.onOrderReject).
    if (e.sender)
      e.sender->send(new som::Reject(uint(e.oid), uint64_t(0), som::Reject::UNKNOWN), this);
    log_rej("Lime rejected oid=%d id=%lu%s: %s", e.oid, m.id, m.event_id ? "" : " (in the API)", m.text);
  }

  void LimeOM::on_cancel_rejected(const LimeEvent &m) noexcept
  {
    auto res = book_.on_cancel_rejected(m.id);
    if (res.outcome == Outcome::Unknown)
    {
      log_err("Lime cancel reject of id=%lu (%s), unknown to this OM", m.id, m.text);
      return;
    }
    if (res.outcome != Outcome::Ok)
    {
      log_inf("Lime cancel reject of id=%lu (%s): %s (order %s)", m.id, m.text, to_string(res.outcome),
              to_string(res.entry->state));
      return;
    }
    auto &e = *res.entry;
    if (e.sender)
      e.sender->send(new som::CancReject(uint(e.oid), e.exch_oid, som::CancReject::FROMEXCHANGE), this);
    log_rej("Lime refused cancel of oid=%d id=%lu: %s", e.oid, m.id, m.text);
  }
}

// ---- factories -------------------------------------------------------------------------------------

actors::Actor *create_LimeOM(const lime::LimeOMConfig &cfg, actors::Actor *som, en::x venue,
                             lime::SessionFactory factory)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "LimeOM: not created, bad config: " << err << ". The venue is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
  {
    if (cfg.sim)
      factory = lime::SimLime::make_factory(cfg.sim_config);
    else if (lime::sdk_available())
      factory = lime::sdk_session_factory();
    else
    {
      std::cerr << "LimeOM: not created: this build has no Lime SDK (build with LIME_SDK_HEADER) and sim is off. "
                   "The venue is unrouted."
                << std::endl;
      return nullptr;
    }
  }
  return new lime::LimeOM(cfg, som, std::move(factory), venue);
}

actors::Actor *create_LimeOM(const std::string &config_path, actors::Actor *som)
{
  lime::LimeOMConfig cfg;
  en::x venue = en::x::LIME;
  std::string err;
  if (!lime::LimeOMConfig::from_file(config_path, cfg, venue, err))
  {
    std::cerr << "LimeOM: not created, cannot read " << config_path << ": " << err << ". The venue is unrouted."
              << std::endl;
    return nullptr;
  }
  return create_LimeOM(cfg, som, venue);
}
