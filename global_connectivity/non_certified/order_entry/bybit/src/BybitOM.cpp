/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_oe/act/BybitOM.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iostream>

#include <nlohmann/json.hpp>

#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_oe/if/BybitOM.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"

namespace bybit::oe
{
namespace som = frame::som::msg;
using frame::som::ClientId;
using frame::som::Outcome;
using frame::som::to_string;
using msg::BybitReport;

// ---- session thread -> mailbox ----------------------------------------------------------------

class BybitOM::Events : public SessionEvents
{
public:
  Events(BybitOM *om, uint64_t s) : om_(om), s_(s) {}
  void logged_in() override { post(BybitReport::LOGGED_IN); }
  void login_rejected(const std::string &why) override { post(BybitReport::LOGIN_REJECTED, why); }
  void disconnected(const std::string &why) override { post(BybitReport::DISCONNECTED, why); }
  void busy() override { post(BybitReport::BUSY); }
  void available() override { post(BybitReport::AVAILABLE); }
  void order(const std::string &j) override
  {
    auto r = new BybitReport(BybitReport::ORDER, s_);
    r->json = j;
    om_->send(r, nullptr);
  }
  void execution(const std::string &j) override
  {
    auto r = new BybitReport(BybitReport::EXECUTION, s_);
    r->json = j;
    om_->send(r, nullptr);
  }
  void response(RequestKind k, const std::string &link, int code, const std::string &m) override
  {
    auto r = new BybitReport(BybitReport::RESPONSE, s_);
    r->request = k;
    r->link = link;
    r->ret_code = code;
    r->text = m;
    om_->send(r, nullptr);
  }

private:
  void post(BybitReport::Kind k, std::string text = {})
  {
    auto r = new BybitReport(k, s_);
    r->text = std::move(text);
    om_->send(r, nullptr);
  }
  BybitOM *om_;
  uint64_t s_;
};

// ---- identifiers and units ----------------------------------------------------------------------

namespace
{
  uint32_t utc_today()
  {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    return uint32_t((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
  }

  double num(const nlohmann::json &j, const char *k)
  {
    auto it = j.find(k);
    if (it == j.end() || it->is_null())
      return 0;
    if (it->is_string())
    {
      const auto s = it->get<std::string>();
      return s.empty() ? 0 : std::stod(s);
    }
    return it->get<double>();
  }

  std::string str(const nlohmann::json &j, const char *k)
  {
    auto it = j.find(k);
    if (it == j.end() || it->is_null())
      return {};
    return it->is_string() ? it->get<std::string>() : it->dump();
  }

  // Whole SOM units in a venue quantity; -1 if it is not a whole number of them.
  int units(double qty, double unit)
  {
    const double u = qty / unit;
    const double r = std::round(u);
    return std::fabs(u - r) > 1e-6 ? -1 : int(r);
  }

  int reject_reason_for(int code)
  {
    return code == 10006 ? som::Reject::THROTTLE : som::Reject::EXCHLIMIT;
  }
}

std::string BybitOM::link_id(uint32_t d, ClientId cid)
{
  return "k" + std::to_string(d) + "-" + std::to_string(cid);
}

bool BybitOM::parse_link(const std::string &link, ClientId &cid)
{
  if (link.size() < 11 || link[0] != 'k' || link[9] != '-')
    return false;
  try
  {
    size_t n = 0;
    cid = std::stoull(link.substr(10), &n);
    return n == link.size() - 10;
  }
  catch (...)
  {
    return false;
  }
}

uint64_t BybitOM::xordid(const std::string &id)
{
  if (!id.empty() && id.size() <= 19 && std::all_of(id.begin(), id.end(), ::isdigit))
    return std::stoull(id);
  uint64_t h = 1469598103934665603ull; // FNV-1a 64
  for (unsigned char c : id)
  {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

std::string BybitOM::decimal(double v, double step)
{
  int d = 0;
  for (double s = step; d < 12 && std::fabs(s - std::round(s)) > 1e-9; s *= 10)
    ++d;
  char b[64];
  std::snprintf(b, sizeof(b), "%.*f", d, v);
  return b;
}

// ---- lifecycle ------------------------------------------------------------------------------

BybitOM::BybitOM(BybitOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
                 frame::som::ClientIdGen ids, uint32_t yyyymmdd)
    : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue),
      date_(yyyymmdd ? yyyymmdd : utc_today()), book_(ids)
{
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(som::Order, order_handler);
  MESSAGE_HANDLER(som::Cancel, cancel_handler);
  MESSAGE_HANDLER(BybitReport, report_handler);
}

BybitOM::~BybitOM()
{
  stopping_ = true;
  reconnect_.cancel();
  drop_session();
}

std::string BybitOM::link_of(int oid) const
{
  auto e = book_.find_oid(oid);
  if (!e)
    return {};
  auto it = link_of_cid_.find(e->cid);
  return it == link_of_cid_.end() ? std::string() : it->second;
}

void BybitOM::connect() noexcept
{
  ++session_no_;
  events_ = std::make_unique<Events>(this, session_no_);
  try
  {
    session_ = factory_(cfg_.session, *events_);
    ++sessions_created_;
    log_omg("Bybit session %lu: logging in to %s", session_no_, cfg_.session.ws_host);
    session_->logon();
  }
  catch (const std::exception &e)
  {
    session_.reset();
    log_err("cannot create Bybit session: %s", e.what());
    schedule_reconnect();
  }
}

void BybitOM::drop_session() noexcept
{
  ready_ = false;
  busy_ = false;
  session_.reset(); // joins the session's threads: no events after this
  events_.reset();
  // Requests in flight on the dropped connection get no answer: a cancel is
  // refused now, a new order's outcome comes from the query after the next
  // login (on_logged_in).
  std::vector<ClientId> cids;
  for (const auto &[cid, link] : link_of_cid_)
    cids.push_back(cid);
  auto unconfirmed = oe_common::abandon_in_flight(book_, cids, this);
  if (!unconfirmed.empty())
    log_wrn("%zu new orders in flight at Bybit: outcome known after the next login", unconfirmed.size());
}

void BybitOM::schedule_reconnect() noexcept
{
  if (stopping_)
    return;
  reconnect_.arm(cfg_.reconnect_ms, [this, expected = session_no_] {
    send(new BybitReport(BybitReport::RECONNECT, expected), nullptr);
  });
}

void BybitOM::start_handler(const actors::msg::Start *) noexcept
{
  if (!factory_)
  {
    log_err("no Bybit session factory: Bybit is unrouted");
    return;
  }
  send(new BybitReport(BybitReport::RECONNECT, session_no_), nullptr);
  if (som_)
    som_->send(new som::SetExchManager(venue_, this), this);
}

void BybitOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  stopping_ = true;
  reconnect_.cancel();
  if (session_)
    session_->logout();
  drop_session();
  if (book_.live())
    log_wrn("shutting down with %zu orders live at Bybit", book_.live());
}

// ---- requests from the SOM ------------------------------------------------------------------

void BybitOM::order_handler(const som::Order *m) noexcept
{
  using OT = som::Order::OrdType;
  using TIF = som::Order::TimeInForce;
  if (m->oid_to_cancel >= 0)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
  if (!session_)
    return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Bybit connection");
  if (!ready_)
    return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Bybit session not logged in");
  if (busy_)
    return reject(m->sender, m->oid, som::Reject::THROTTLE, "Bybit rate limit hit; waiting");
  if (m->ord_type == OT::STOP || m->ord_type == OT::STOP_LIMIT)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "stop orders not supported");
  if (m->short_sale != som::Order::ShortSale::NONE)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "short-sale flags do not apply to Bybit");
  if (!m->strategy.empty() && m->strategy != "POSTONLY")
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no broker algorithms on Bybit");
  if (!m->route.empty() && m->route != "BYBIT")
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "route must be empty or BYBIT");
  auto ins_it = cfg_.instruments.find(m->sym);
  if (ins_it == cfg_.instruments.end())
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument not configured");
  const Instrument &ins = ins_it->second;

  std::string tif;
  switch (m->tif)
  {
  case TIF::GTC: tif = "GTC"; break;
  case TIF::DAY:
    if (!cfg_.day_as_gtc)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Bybit has no DAY orders (day_as_gtc off)");
    tif = "GTC";
    break;
  case TIF::IOC: tif = "IOC"; break;
  case TIF::FOK: tif = "FOK"; break;
  default: return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on Bybit");
  }
  if (m->strategy == "POSTONLY")
  {
    if (m->ord_type == OT::MARKET || tif != "GTC")
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "PostOnly needs a GTC limit order");
    tif = "PostOnly";
  }
  if (!(m->sz > 0) || std::floor(m->sz) != m->sz)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a whole number of units");
  if (cfg_.max_order_qty > 0 && m->sz > cfg_.max_order_qty)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order quantity limit");
  const double qty = m->sz * ins.qty_unit;
  const double px = double(m->px) * ins.tick;
  if (m->ord_type == OT::LIMIT && !(m->px > 0))
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "limit price must be positive");
  if (m->ord_type == OT::MARKET && cfg_.max_order_notional > 0)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "market order with a notional limit set");
  if (cfg_.max_order_notional > 0 && qty * px > cfg_.max_order_notional)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
  if (ins.category == "spot" && m->ord_type == OT::MARKET && m->side == en::bs::BUY)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                  "spot market buys are sized in quote currency by default; not supported");
  if (!tokens_.take(cfg_.max_orders_per_sec))
    return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

  auto r = book_.add(*m, m->sender);
  if (r.outcome != Outcome::Ok)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                  r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
  auto &e = *r.entry;

  PlaceRequest p;
  p.category = ins.category;
  p.symbol = ins.symbol;
  p.side = m->side == en::bs::BUY ? "Buy" : "Sell";
  p.order_type = m->ord_type == OT::MARKET ? "Market" : "Limit";
  p.qty = decimal(qty, ins.qty_unit);
  if (m->ord_type != OT::MARKET)
    p.price = decimal(px, ins.tick);
  p.time_in_force = m->ord_type == OT::MARKET ? "IOC" : tif;
  p.order_link_id = link_id(date_, e.cid);

  switch (session_->place(p))
  {
  case CallStatus::Success:
  {
    auto &v = by_link_[p.order_link_id];
    v.link = p.order_link_id;
    v.cid = e.cid;
    v.category = ins.category;
    v.ins = &ins;
    link_of_cid_[e.cid] = p.order_link_id;
    log_omg("new order oid=%d %s %s %s %s @ %s %s", m->oid, p.order_link_id, p.side, p.qty, p.symbol,
            p.price.empty() ? "MKT" : p.price, p.time_in_force);
    return;
  }
  case CallStatus::Busy:
    book_.send_failed(e.cid);
    busy_ = true;
    return reject(m->sender, m->oid, som::Reject::THROTTLE, "request queue full");
  case CallStatus::Disconnected:
    book_.send_failed(e.cid);
    return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Bybit connection lost");
  }
}

void BybitOM::cancel_handler(const som::Cancel *m) noexcept
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
    return cancel_reject(m->sender, oid, oe_common::late_cancel_reason(r.entry->state), "order already done");
  default:
    return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "no such order");
  }
  auto &e = *r.entry;
  auto l = link_of_cid_.find(e.cid);
  auto v = l == link_of_cid_.end() ? nullptr : venue_of(l->second);
  if (!v)
  {
    book_.send_failed(e.pending_cid);
    return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "order has no orderLinkId");
  }
  CancelRequest c{v->category, v->ins->symbol, v->link};
  auto st = session_ && ready_ ? session_->cancel(c) : CallStatus::Disconnected;
  if (st != CallStatus::Success)
  {
    book_.send_failed(e.pending_cid);
    if (st == CallStatus::Busy)
      busy_ = true;
    return cancel_reject(m->sender, oid,
                         st == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                         "Bybit refused the cancel request");
  }
  log_omg("cancel oid=%d %s", oid, v->link);
}

// ---- reports ------------------------------------------------------------------------------------

BybitOM::Venue *BybitOM::venue_of(const std::string &link) noexcept
{
  auto it = by_link_.find(link);
  return it == by_link_.end() ? nullptr : &it->second;
}

void BybitOM::report_handler(const BybitReport *m) noexcept
{
  switch (m->kind)
  {
  case BybitReport::RECONNECT:
    if (m->session != session_no_ || stopping_)
      return;
    drop_session();
    connect();
    return;
  case BybitReport::LOGGED_IN:
    if (m->session == session_no_)
      on_logged_in();
    return;
  case BybitReport::LOGIN_REJECTED:
  case BybitReport::DISCONNECTED:
    if (m->session != session_no_)
      return;
    log_err("Bybit session %lu %s: %s", m->session,
            m->kind == BybitReport::LOGIN_REJECTED ? "login refused" : "lost", m->text);
    drop_session();
    schedule_reconnect();
    return;
  case BybitReport::BUSY:
    if (m->session == session_no_)
      busy_ = true;
    return;
  case BybitReport::AVAILABLE:
    if (m->session == session_no_)
      busy_ = false;
    return;
  // Order reports from an older session are real: apply them (idempotently).
  case BybitReport::ORDER: return on_order(m->json);
  case BybitReport::EXECUTION: return on_execution(m->json);
  case BybitReport::RESPONSE: return on_response(*m);
  }
}

void BybitOM::on_logged_in() noexcept
{
  ready_ = true;
  busy_ = false;
  log_omg("Bybit session %lu logged in", session_no_);
  // No replay on the private stream: reconcile every order still live.
  for (auto &[link, v] : by_link_)
  {
    auto e = book_.find_cid(v.cid);
    if (!e || frame::som::is_done(e->state))
      continue;
    query(v);
  }
}

// The executions come last: their answer decides a PendingNew order's fate
// (on_response), after any order or execution the queries returned.
void BybitOM::query(Venue &v) noexcept
{
  if (!session_)
    return;
  session_->query_order(v.category, v.link);
  session_->query_executions(v.category, v.link);
}

void BybitOM::ensure_ack(Venue &v) noexcept
{
  if (v.acked)
    return;
  auto res = book_.on_accepted(v.cid, xordid(v.order_id));
  if (res.outcome != Outcome::Ok)
  {
    log_wrn("accept of %s: %s", v.link, to_string(res.outcome));
    return;
  }
  v.acked = true;
  if (res.entry->sender)
    res.entry->sender->send(new som::Ack(uint(res.entry->oid), res.entry->exch_oid), this);
  log_omg("ack oid=%d %s orderId %s", res.entry->oid, v.link, v.order_id);
}

void BybitOM::finish_cancel(Venue &v, const char *why) noexcept
{
  v.cancel_waiting = false;
  auto e = book_.find_cid(v.cid);
  if (!e || frame::som::is_done(e->state))
    return;
  auto res = book_.on_canceled(v.cid);
  if (res.outcome == Outcome::Ok && res.entry->sender)
    res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
  log_omg("order oid=%d %s canceled (%s), filled %d", res.entry->oid, v.link, why, res.entry->cum);
}

void BybitOM::on_order(const std::string &json) noexcept
{
  nlohmann::json j;
  try
  {
    j = nlohmann::json::parse(json);
  }
  catch (const std::exception &)
  {
    log_err("unparsable Bybit order update: %s", json);
    return;
  }
  const auto link = str(j, "orderLinkId");
  auto v = venue_of(link);
  if (!v)
  {
    log_wrn("Bybit order update for %s (orderId %s), not placed by this OM: ignored", link,
            str(j, "orderId"));
    return;
  }
  if (v->order_id.empty())
    v->order_id = str(j, "orderId");
  const auto status = str(j, "orderStatus");
  const int cum = units(num(j, "cumExecQty"), v->ins->qty_unit);
  if (cum > v->venue_cum)
    v->venue_cum = cum;

  if (status == "New" || status == "PartiallyFilled" || status == "Untriggered")
    return ensure_ack(*v);
  if (status == "Filled")
  {
    ensure_ack(*v);
    if (v->filled < v->venue_cum)
      session_ ? (void)session_->query_executions(v->category, link) : (void)0;
    return;
  }
  if (status == "Rejected" ||
      ((status == "Cancelled" || status == "Deactivated") && !v->acked && v->venue_cum == 0 &&
       str(j, "rejectReason") != "EC_NoError" && str(j, "rejectReason") != "EC_PerCancelRequest" &&
       !str(j, "rejectReason").empty()))
  {
    auto res = book_.on_rejected(v->cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), som::Reject::EXCHLIMIT), this);
    log_rej("Bybit rejected %s: %s (%s)", link, str(j, "rejectReason"), to_string(res.outcome));
    return;
  }
  if (status == "Cancelled" || status == "PartiallyFilledCanceled" || status == "Deactivated")
  {
    ensure_ack(*v);
    if (v->filled < v->venue_cum)
    {
      // Fills the update counts have not arrived yet: hold the CancAck.
      v->cancel_waiting = true;
      log_inf("%s canceled at Bybit with %d filled, %d applied: fetching executions", link, v->venue_cum,
              v->filled);
      if (session_)
        session_->query_executions(v->category, link);
      return;
    }
    return finish_cancel(*v, str(j, "cancelType").c_str());
  }
  log_wrn("Bybit order %s status %s: no action", link, status);
}

void BybitOM::on_execution(const std::string &json) noexcept
{
  nlohmann::json j;
  try
  {
    j = nlohmann::json::parse(json);
  }
  catch (const std::exception &)
  {
    log_err("unparsable Bybit execution: %s", json);
    return;
  }
  const auto link = str(j, "orderLinkId");
  const auto exec_id = str(j, "execId");
  const auto type = str(j, "execType");
  auto v = venue_of(link);
  if (!v)
  {
    if (type == "Trade")
      log_err("FILL ON AN ORDER NOT PLACED BY THIS OM: %s execId %s %s %s @ %s: CHECK POSITIONS", link,
              exec_id, str(j, "side"), str(j, "execQty"), str(j, "execPrice"));
    else
      log_inf("Bybit %s execution %s ignored", type, exec_id);
    return;
  }
  if (type != "Trade")
  {
    // Bybit's other execution types are not executions of this order:
    // BustTrade is a takeover liquidation, AdlTrade auto-deleveraging,
    // Delivery a delisting close (all change the position, outside the
    // SOM's fills); Funding and Settle are cash. None is a trade bust:
    // Bybit V5 documents no bust or correction of an execution.
    const bool moves_position = type != "Funding" && type != "Settle";
    if (moves_position)
      log_err("Bybit %s execution %s on %s: %s %s @ %s changed the position outside the OM: CHECK POSITIONS",
              type, exec_id, link, str(j, "side"), str(j, "execQty"), str(j, "execPrice"));
    else
      log_wrn("Bybit %s execution %s on %s: not a fill", type, exec_id, link);
    return;
  }
  if (!v->exec_ids.insert(exec_id).second)
    return; // already applied (stream and REST both deliver it)
  if (v->order_id.empty())
    v->order_id = str(j, "orderId");
  const int qty = units(num(j, "execQty"), v->ins->qty_unit);
  if (qty <= 0)
  {
    log_err("execution %s on %s: %s is not a whole number of units of %g: CHECK POSITIONS", exec_id, link,
            str(j, "execQty"), v->ins->qty_unit);
    return;
  }
  ensure_ack(*v);
  v->filled += qty;
  auto res = book_.on_fill(v->cid, qty);
  if (res.outcome == Outcome::Ok)
  {
    const auto &e = *res.entry;
    const auto &o = e.order;
    const int pxi = int(std::llround(num(j, "execPrice") / v->ins->tick));
    const uint64_t tim = uint64_t(num(j, "execTime")) * 1000000ull;
    if (e.sender)
    {
      auto f = new som::Fill(uint(e.oid), venue_, uint(o.sym), pxi, o.side, qty, e.leaves, o.owner, tim);
      f->xordid = e.exch_oid;
      e.sender->send(f, this);
    }
    log_fil("fill oid=%d %s %d @ %s leaves %d execId %s", e.oid, link, qty, str(j, "execPrice"), e.leaves,
            exec_id);
  }
  else
    log_err("fill on %s qty=%d execId %s: %s: CHECK POSITIONS", link, qty, exec_id, to_string(res.outcome));
  if (v->cancel_waiting && v->filled >= v->venue_cum)
    finish_cancel(*v, "canceled, fills complete");
}

void BybitOM::on_response(const BybitReport &r) noexcept
{
  auto v = venue_of(r.link);
  if (!v)
  {
    if (r.ret_code != 0)
      log_err("Bybit answer %d (%s) for unknown %s", r.ret_code, r.text, r.link);
    return;
  }
  if (r.ret_code == 0)
  {
    // The order and its executions were fetched (after a login, or after a
    // create without an answer). Neither came for an order still PendingNew:
    // Bybit never had the create.
    if (r.request == RequestKind::QueryExecutions && oe_common::reject_unknown(book_, v->cid, this))
      log_rej("Bybit does not know %s: the create never arrived", r.link);
    return;
  }
  if (r.request == RequestKind::Place)
  {
    if (r.ret_code == -1)
    {
      // No answer: the order may or may not exist. Ask, never resend.
      log_err("no answer to the create of %s (%s): querying", r.link, r.text);
      query(*v);
      return;
    }
    if (r.ret_code == 10006)
      busy_ = true;
    auto res = book_.on_rejected(v->cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), reject_reason_for(r.ret_code)),
                              this);
    log_rej("Bybit refused the create of %s: %d %s (%s)", r.link, r.ret_code, r.text, to_string(res.outcome));
    return;
  }
  if (r.request == RequestKind::Cancel)
  {
    if (r.ret_code == -1)
    {
      log_err("no answer to the cancel of %s (%s): querying", r.link, r.text);
      if (session_)
        session_->query_order(v->category, r.link);
      return;
    }
    auto res = book_.on_cancel_rejected(v->cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::FROMEXCHANGE), this);
    log_rej("Bybit refused the cancel of %s: %d %s (%s)", r.link, r.ret_code, r.text, to_string(res.outcome));
    return;
  }
  log_err("Bybit query for %s failed: %d %s", r.link, r.ret_code, r.text);
}

} // namespace bybit::oe

// ---- factories ----------------------------------------------------------------------------------

actors::Actor *create_BybitOM(const bybit::oe::BybitOMConfig &cfg, actors::Actor *som, en::x venue,
                              bybit::oe::SessionFactory factory)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "BybitOM: not created, bad config: " << err << ". Bybit is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? bybit::oe::SimBybit::make_factory(cfg.sim_config) : bybit::oe::rest_ws_session_factory();
  return new bybit::oe::BybitOM(cfg, som, std::move(factory), venue);
}

actors::Actor *create_BybitOM(const std::string &path, actors::Actor *som, en::x venue)
{
  bybit::oe::BybitOMConfig cfg;
  std::string err;
  if (!bybit::oe::BybitOMConfig::from_file(path, cfg, err))
  {
    std::cerr << "BybitOM: not created, cannot read " << path << ": " << err << ". Bybit is unrouted."
              << std::endl;
    return nullptr;
  }
  return create_BybitOM(cfg, som, venue);
}
