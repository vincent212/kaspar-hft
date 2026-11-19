/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Assert.hpp"
#include "coinbase_exchange_oe/act/CbxOM.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
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

namespace coinbase_exchange::oe
{
namespace som = frame::som::msg;
using frame::som::ClientId;
using frame::som::ClientIdGen;
using frame::som::Outcome;
using frame::som::State;
using frame::som::to_string;
using msg::CbxReport;
using namespace fix;

// ---- products ------------------------------------------------------------------------------

bool OEProduct::make(const std::string &product, const std::string &quote_increment, const std::string &base_increment,
                     const std::string &qty_unit, OEProduct &out, std::string &err)
{
  out.product = product;
  out.price_decimals = decimals_of(quote_increment);
  out.size_decimals = decimals_of(base_increment);
  if (out.price_decimals < 0 || out.size_decimals < 0)
  {
    err = product + ": bad quote_increment or base_increment";
    return false;
  }
  auto inc = scaled(quote_increment, out.price_decimals);
  auto base = scaled(base_increment, out.size_decimals);
  auto unit = scaled(qty_unit.empty() ? base_increment : qty_unit, out.size_decimals);
  if (!inc || *inc <= 0 || !base || *base <= 0 || !unit || *unit <= 0 || *unit % *base != 0)
  {
    err = product + ": qty_unit must be a positive multiple of base_increment";
    return false;
  }
  out.price_increment = *inc;
  out.qty_unit = *unit;
  return true;
}

std::optional<int64_t> OEProduct::ticks(const std::string &px) const
{
  auto v = scaled(px, price_decimals);
  if (!v || *v % price_increment)
    return std::nullopt;
  return *v / price_increment;
}

std::optional<int64_t> OEProduct::units(const std::string &qty) const
{
  auto v = scaled(qty, size_decimals);
  if (!v || *v % qty_unit)
    return std::nullopt;
  return *v / qty_unit;
}

bool CbxOMConfig::valid(std::string &err) const
{
  if (products.empty())
  {
    err = "no products";
    return false;
  }
  if (day_tif != '1' && day_tif != '6' && day_tif != 'R')
  {
    err = "day_tif must be gtc, gtd or reject";
    return false;
  }
  if (std::string("DONB").find(self_trade) == std::string::npos)
  {
    err = "self_trade must be D, O, N or B";
    return false;
  }
  if (session.cancel_on_disconnect != 'Y' && session.cancel_on_disconnect != 'S' && session.cancel_on_disconnect != 0)
  {
    err = "cancel_on_disconnect must be Y, S or none";
    return false;
  }
  if (session.heartbeat_s <= 0 || session.heartbeat_s > 30)
  {
    err = "heartbeat_s must be 1 to 30 (Coinbase: HeartBtInt <= 30)";
    return false;
  }
  return true;
}

// ---- session thread -> mailbox ---------------------------------------------------------------

class CbxOM::Events : public SessionEvents
{
public:
  Events(CbxOM *om, uint64_t session) : om_(om), session_(session) {}
  void logged_on() override { post(CbxReport::LOGGED_ON); }
  void logon_rejected(const std::string &why) override { post(CbxReport::LOGON_REJECTED, why); }
  void disconnected(const std::string &why) override { post(CbxReport::DISCONNECTED, why); }
  void busy() override { post(CbxReport::BUSY); }
  void available() override { post(CbxReport::AVAILABLE); }
  void message(const FixMsg &m) override
  {
    auto r = new CbxReport(CbxReport::MESSAGE, session_);
    r->fix = m;
    om_->send(r, nullptr);
  }

private:
  void post(CbxReport::Kind k, std::string text = {})
  {
    auto r = new CbxReport(k, session_);
    r->text = std::move(text);
    om_->send(r, nullptr);
  }
  CbxOM *om_;
  uint64_t session_;
};

// ---- lifecycle ---------------------------------------------------------------------------------

namespace
{
  uint64_t epoch_ns_now()
  {
    using namespace std::chrono;
    return uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
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

  // Next 00:00 UTC, in ns since the epoch.
  uint64_t next_utc_midnight_ns()
  {
    uint64_t day = 86400ull * 1000000000ull;
    return (epoch_ns_now() / day + 1) * day;
  }

  // Next 00:00 UTC, as a FIX UTCTimestamp: the expiry of a DAY order sent as GTD.
  std::string next_utc_midnight()
  {
    return fix_time_ms(next_utc_midnight_ns());
  }
}

CbxOM::CbxOM(CbxOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue, ClientIdGen ids,
             uint64_t uuid_seed)
    : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), book_(ids),
      rng_(uuid_seed ? uuid_seed : std::random_device{}() ^ (uint64_t(std::random_device{}()) << 32) ^ epoch_ns_now())
{
  strncpy(name, "CbxOM", sizeof(name) - 1);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(som::Order, order_handler);
  MESSAGE_HANDLER(som::Cancel, cancel_handler);
  MESSAGE_HANDLER(CbxReport, report_handler);
}

CbxOM::~CbxOM()
{
  stopping_ = true;
  reconnect_timer_ = {};
  logon_timer_ = {};
  purge_timer_ = {};
  drop_session();
}

std::string CbxOM::clordid(int oid) const
{
  auto e = book_.find_oid(oid);
  if (!e)
    return {};
  auto it = uuid_of_cid_.find(e->cid);
  return it == uuid_of_cid_.end() ? std::string() : it->second;
}

ClientId CbxOM::cid_of(const std::string &uuid) const
{
  auto it = cid_of_uuid_.find(uuid);
  return it == cid_of_uuid_.end() ? 0 : it->second;
}

std::string CbxOM::new_uuid(ClientId cid)
{
  std::string u;
  do
    u = to_string(make_uuid_v4(rng_(), rng_()));
  while (cid_of_uuid_.count(u));
  uuid_of_cid_[cid] = u;
  cid_of_uuid_[u] = cid;
  return u;
}

CbxOM::VenueOrder *CbxOM::venue_of(ClientId cid)
{
  auto e = book_.find_cid(cid);
  if (!e)
    return nullptr;
  auto it = vorders_.find(e->oid);
  return it == vorders_.end() ? nullptr : &it->second;
}

int64_t CbxOM::applied_base(const VenueOrder &v, const frame::som::Entry &e) const
{
  return int64_t(e.cum) * v.prod->qty_unit + v.remainder;
}

int64_t CbxOM::take_units(VenueOrder &v, int64_t base) const
{
  const int64_t total = v.remainder + base;
  v.remainder = total % v.prod->qty_unit;
  return total / v.prod->qty_unit;
}

void CbxOM::connect() noexcept
{
  ++session_no_;
  events_ = std::make_unique<Events>(this, session_no_);
  try
  {
    session_ = factory_(cfg_.session, *events_);
    ++sessions_created_;
    log_omg("Coinbase Exchange FIX session %lu: logging on", session_no_);
    session_->logon();
    arm(logon_timer_, cfg_.logon_timeout_ms, CbxReport::LOGON_TIMEOUT);
  }
  catch (const std::exception &e)
  {
    session_.reset();
    log_err("cannot create the Coinbase Exchange session: %s", e.what());
    schedule_reconnect();
  }
}

void CbxOM::drop_session() noexcept
{
  ready_ = false;
  busy_ = false;
  session_.reset();
  events_.reset();
}

void CbxOM::arm(std::jthread &t, int ms, CbxReport::Kind k) noexcept
{
  auto expected = session_no_;
  t = std::jthread([this, expected, ms, k](std::stop_token st) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> l(m);
    if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
      send(new CbxReport(k, expected), nullptr);
  });
}

void CbxOM::schedule_reconnect() noexcept
{
  if (stopping_ || rejected_)
    return;
  arm(reconnect_timer_, cfg_.reconnect_ms, CbxReport::RECONNECT);
}

// Finished orders are forgotten at 00:00 UTC (and at each logon).
void CbxOM::schedule_purge() noexcept
{
  if (stopping_)
    return;
  uint64_t now = epoch_ns_now(), at = next_utc_midnight_ns();
  arm(purge_timer_, int(std::min<uint64_t>((at > now ? at - now : 0) / 1000000ull, 86400000ull)), CbxReport::PURGE);
}

void CbxOM::start_handler(const actors::msg::Start *) noexcept
{
  if (!factory_)
  {
    log_err("no Coinbase Exchange session factory: the venue is unrouted");
    return;
  }
  send(new CbxReport(CbxReport::RECONNECT, session_no_), nullptr);
  schedule_purge();
  if (som_)
    som_->send(new som::SetExchManager(venue_, this), this);
}

void CbxOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  stopping_ = true;
  reconnect_timer_ = {};
  logon_timer_ = {};
  purge_timer_ = {};
  if (session_ && ready_)
    session_->logout();
  drop_session();
  if (book_.live())
    log_wrn("shutting down with %zu orders live at Coinbase Exchange", book_.live());
}

// ---- requests from the SOM ---------------------------------------------------------------------

void CbxOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
{
  log_err("rejecting order oid=%d: %s", oid, why);
  if (to)
    to->send(new som::Reject(uint(oid), reason), this);
}

void CbxOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
{
  log_err("rejecting cancel of oid=%d: %s", oid, why);
  if (to)
    to->send(new som::CancReject(uint(oid), reason), this);
}

bool CbxOM::take_token() noexcept
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

void CbxOM::order_handler(const som::Order *m) noexcept
{
  using OT = som::Order::OrdType;
  using TIF = som::Order::TimeInForce;
  if (m->oid_to_cancel >= 0)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
  if (!session_)
    return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Coinbase Exchange connection");
  if (!ready_)
    return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Coinbase Exchange session not logged on");
  if (busy_)
    return reject(m->sender, m->oid, som::Reject::THROTTLE, "Coinbase Exchange connection busy");
  if (m->ord_type == OT::STOP)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Coinbase Exchange FIX has stop-limit orders only");
  if (!m->strategy.empty())
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no broker algorithms on Coinbase Exchange");
  if (!m->route.empty())
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no routes on Coinbase Exchange");
  if (m->side != en::bs::BUY && m->short_sale != som::Order::ShortSale::NONE)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no short-sale marks on a spot exchange");
  char tif = 0;
  switch (m->tif)
  {
  case TIF::GTC: tif = '1'; break;
  case TIF::IOC: tif = '3'; break;
  case TIF::FOK: tif = '4'; break;
  case TIF::DAY: tif = cfg_.day_tif; break;
  default: break;
  }
  if (tif == 0 || tif == 'R')
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on Coinbase Exchange");
  if (m->ord_type == OT::MARKET && tif != '3' && tif != '4')
    tif = '3'; // a market order fills at once or not at all; Coinbase sends it IOC
  auto a = frame::ref::RefData::get_asset(std::size_t(m->sym));
  if (!a)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "unknown instrument");
  auto pit = cfg_.products.find(a->name);
  if (pit == cfg_.products.end())
    return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument not in the products table");
  const OEProduct &p = pit->second;
  if (!(m->sz > 0) || std::floor(m->sz) != m->sz || m->sz > 2147483647.0)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "quantity must be a positive whole number of units");
  if (cfg_.max_order_qty > 0 && m->sz > cfg_.max_order_qty)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order quantity limit");
  if (m->ord_type != OT::MARKET && m->px <= 0)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "price must be positive");
  if (m->ord_type == OT::STOP_LIMIT && m->stop_px <= 0)
    return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "stop price must be positive");
  if (cfg_.max_order_notional > 0)
  {
    if (m->ord_type == OT::MARKET)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "market order with a notional limit set");
    long double notional = (long double)m->sz * p.qty_unit * std::pow(10.0L, -p.size_decimals) *
                           (long double)m->px * p.price_increment * std::pow(10.0L, -p.price_decimals);
    if (notional > cfg_.max_order_notional)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
  }
  if (!take_token())
    return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

  auto r = book_.add(*m, m->sender);
  if (r.outcome != Outcome::Ok)
    return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                  r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
  auto &e = *r.entry;
  const std::string clid = new_uuid(e.cid);

  FixMsg o("D");
  o.set(ClOrdID, clid);
  o.set(Symbol, p.product);
  o.set(Side, m->side == en::bs::BUY ? '1' : '2');
  o.set(OrderQty, p.qty(e.qty));
  switch (m->ord_type)
  {
  case OT::MARKET: o.set(OrdType, '1'); break;
  case OT::STOP_LIMIT:
    o.set(OrdType, '4');
    o.set(Price, p.price(m->px));
    o.set(StopPx, p.price(m->stop_px));
    break;
  default:
    o.set(OrdType, '2');
    o.set(Price, p.price(m->px));
    break;
  }
  o.set(TimeInForce, tif);
  if (tif == '6')
    o.set(ExpireTime, next_utc_midnight());
  if (cfg_.post_only && m->ord_type == OT::LIMIT)
    o.set(ExecInst, 'A');
  o.set(SelfTradeType, cfg_.self_trade);

  switch (session_->send(o))
  {
  case CallStatus::Success:
  {
    auto &v = vorders_[e.oid];
    v.symbol = p.product;
    v.prod = &p;
    log_omg("new order oid=%d %s", m->oid, o.str());
    return;
  }
  case CallStatus::Busy:
    book_.send_failed(e.cid);
    busy_ = true;
    return reject(m->sender, m->oid, som::Reject::THROTTLE, "Coinbase Exchange connection busy");
  case CallStatus::Disconnected:
    book_.send_failed(e.cid);
    return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Coinbase Exchange connection lost");
  }
}

void CbxOM::cancel_handler(const som::Cancel *m) noexcept
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
  auto vit = vorders_.find(oid);
  auto orig = uuid_of_cid_.find(e.cid);
  if (vit == vorders_.end() || orig == uuid_of_cid_.end())
  {
    book_.send_failed(e.pending_cid);
    return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "order was never sent");
  }
  FixMsg c("F");
  c.set(ClOrdID, new_uuid(e.pending_cid));
  if (!vit->second.order_id.empty())
    c.set(OrderID, vit->second.order_id);
  c.set(OrigClOrdID, orig->second);
  c.set(Symbol, vit->second.symbol);
  auto status = session_ && ready_ ? session_->send(c) : CallStatus::Disconnected;
  if (status != CallStatus::Success)
  {
    book_.send_failed(e.pending_cid);
    if (status == CallStatus::Busy)
      busy_ = true;
    return cancel_reject(m->sender, oid,
                         status == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                         "Coinbase Exchange refused the cancel");
  }
  log_omg("cancel oid=%d %s", oid, c.str());
}

// ---- reports -------------------------------------------------------------------------------------

void CbxOM::report_handler(const CbxReport *m) noexcept
{
  if (m->kind == CbxReport::RECONNECT)
  {
    if (!stopping_ && !rejected_ && m->session == session_no_ && !session_)
      connect();
    return;
  }
  if (m->kind == CbxReport::PURGE)
  {
    purge_done();
    schedule_purge();
    return;
  }
  if (m->session != session_no_ || !session_)
    return; // from a session already dropped

  switch (m->kind)
  {
  case CbxReport::LOGGED_ON:
    ready_ = true;
    logon_timer_.request_stop();
    log_omg("Coinbase Exchange logged on (%zu orders live)", book_.live());
    purge_done();
    request_status();
    return;
  case CbxReport::LOGON_TIMEOUT:
    if (ready_)
      return;
    // No Logon answer: the connection never came up (TCP or TLS), or the
    // venue is not answering. QuickFIX reports neither.
    log_err("Coinbase Exchange: not logged on %d ms after connecting; reconnecting in %d ms", cfg_.logon_timeout_ms,
            cfg_.reconnect_ms);
    drop_session();
    schedule_reconnect();
    return;
  case CbxReport::LOGON_REJECTED:
    // bad key, signature or passphrase: retrying will not help
    log_err("Coinbase Exchange logon rejected (%s): the venue is unrouted until restart", m->text);
    rejected_ = true;
    drop_session();
    return;
  case CbxReport::DISCONNECTED:
    log_err("Coinbase Exchange connection lost: %s; reconnecting in %d ms (%zu orders live)", m->text,
            cfg_.reconnect_ms, book_.live());
    drop_session();
    schedule_reconnect();
    return;
  case CbxReport::BUSY:
    busy_ = true;
    return;
  case CbxReport::AVAILABLE:
    busy_ = false;
    return;
  default:
    break;
  }
  const auto t = m->fix.type();
  if (t == "8")
    on_execution_report(m->fix);
  else if (t == "9")
    on_cancel_reject(m->fix);
  else if (t == "j")
    on_business_reject(m->fix);
  else if (t == "3")
    log_err("Coinbase Exchange session Reject: RefSeqNum %s RefTagID %s reason %s: %s", m->fix.get(RefSeqNum),
            m->fix.get(371), m->fix.get(373), m->fix.get(Text));
  else
    log_wrn("Coinbase Exchange message %s ignored: %s", t, m->fix.str());
}

// After a logon: ask where every live order stands; the answers (150=I)
// settle what happened while disconnected.
void CbxOM::request_status() noexcept
{
  for (const auto &[oid, v] : vorders_)
  {
    auto e = book_.find_oid(oid);
    if (!e || frame::som::is_done(e->state))
      continue;
    auto u = uuid_of_cid_.find(e->cid);
    if (u == uuid_of_cid_.end())
      continue;
    FixMsg h("H");
    h.set(ClOrdID, u->second);
    if (!v.order_id.empty())
      h.set(OrderID, v.order_id);
    h.set(Symbol, v.symbol);
    if (session_->send(h) != CallStatus::Success)
      log_err("order status request for oid=%d not sent", oid);
  }
}

// Forget finished orders: their venue state, their ids and their ExecIDs,
// and the book's entries. A report for one of them afterwards is logged as
// unknown. Called at a quiet point only: at logon, and at 00:00 UTC.
void CbxOM::purge_done() noexcept
{
  auto done = [&](const frame::som::Entry *e) { return !e || frame::som::is_done(e->state); };
  size_t orders = 0, ids = 0;
  for (auto it = cid_of_uuid_.begin(); it != cid_of_uuid_.end();)
  {
    if (!done(book_.find_cid(it->second)))
    {
      ++it;
      continue;
    }
    uuid_of_cid_.erase(it->second);
    it = cid_of_uuid_.erase(it);
    ++ids;
  }
  for (auto it = vorders_.begin(); it != vorders_.end();)
  {
    if (!done(book_.find_oid(it->first)))
    {
      ++it;
      continue;
    }
    it = vorders_.erase(it);
    ++orders;
  }
  book_.purge_done();
  ++purges_;
  if (orders || ids)
    log_inf("forgot %zu finished Coinbase Exchange orders (%zu ids); %zu orders kept", orders, ids, book_.size());
}

void CbxOM::ack_once(ClientId cid) noexcept
{
  auto e = book_.find_cid(cid);
  auto v = venue_of(cid);
  if (!e || !v || v->acked)
    return;
  v->acked = true;
  if (e->sender)
    e->sender->send(new som::Ack(uint(e->oid), e->exch_oid), this);
}

void CbxOM::canceled(ClientId cid, const char *why) noexcept
{
  auto res = book_.on_canceled(cid);
  if (res.outcome == Outcome::Ok)
  {
    if (res.entry->sender)
      res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
    log_omg("order oid=%d canceled (%s), filled %d", res.entry->oid, why, res.entry->cum);
  }
  else
    log_inf("cancel report (%s): %s", why, to_string(res.outcome));
}

void CbxOM::send_fill(ClientId cid, int qty, int64_t px_ticks, uint64_t tim, const char *why) noexcept
{
  ack_once(cid);
  auto res = book_.on_fill(cid, qty);
  if (res.outcome != Outcome::Ok)
  {
    log_err("fill of %d (%s): %s: CHECK POSITIONS", qty, why, to_string(res.outcome));
    return;
  }
  ++fills_;
  const auto &e = *res.entry;
  const auto &o = e.order;
  if (e.sender)
    e.sender->send(new som::Fill(uint(e.oid), venue_, uint(o.sym), int(px_ticks), o.side, qty, e.leaves, o.owner, tim),
                   this);
  log_fil("fill oid=%d %d @ %ld ticks leaves %d (%s)", e.oid, qty, (long)px_ticks, e.leaves, why);
}

void CbxOM::on_execution_report(const FixMsg &m) noexcept
{
  const std::string clid = m.get(ClOrdID);
  ClientId cid = cid_of(clid);
  if (!cid && m.has(OrigClOrdID))
    cid = cid_of(m.get(OrigClOrdID));
  const char et = m.get_char(ExecType);
  if (!cid)
  {
    if (et == 'F')
      log_err("FILL ON AN ORDER UNKNOWN TO THIS OM: %s: CHECK POSITIONS", m.str());
    else
      log_err("execution report for an order unknown to this OM (placed elsewhere?): %s", m.str());
    return;
  }
  auto v = venue_of(cid);
  if (v && m.has(OrderID) && v->order_id.empty())
    v->order_id = m.get(OrderID);
  uint64_t xid = 0;
  if (auto oid = parse_uuid(m.get(OrderID)); oid && !oid->nil())
    xid = order_id64(*oid);

  switch (et)
  {
  case '0': // New
  {
    auto res = book_.on_accepted(cid, xid);
    if (res.outcome == Outcome::Ok)
      ack_once(cid);
    else
      log_inf("accept of %s: %s", clid, to_string(res.outcome));
    return;
  }
  case 'F':
    if (xid)
      book_.on_accepted(cid, xid); // a fill implies the order was accepted
    return on_trade(m, cid);
  case '4': // Canceled
  case 'C': // Expired (IOC, FOK, GTD)
    return canceled(cid, et == '4' ? (m.get(Text).empty() ? "canceled" : m.get(Text).c_str()) : "expired");
  case '8': // Rejected
  {
    auto res = book_.on_rejected(cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), som::Reject::EXCHLIMIT), this);
    log_rej("Coinbase Exchange rejected %s: %s (OrdRejReason %s): %s", clid, m.get(Text), m.get(OrdRejReason),
            to_string(res.outcome));
    return;
  }
  case 'D': // Restated: partly cancelled by self-trade prevention
  {
    auto e = book_.find_cid(cid);
    auto leaves = v && v->prod ? scaled(m.get(LeavesQty, "0"), v->prod->size_decimals) : std::nullopt;
    log_wrn("Coinbase Exchange restated %s (reason %s): leaves %s", clid, m.get(ExecRestatementReason),
            m.get(LeavesQty));
    if (!e || !leaves)
      return;
    // The whole units the order can still fill: what is left at the venue
    // plus the remainder already filled below a unit.
    const int64_t units_left = (v->remainder + *leaves) / v->prod->qty_unit;
    if (units_left == 0)
      return canceled(cid, "restated to nothing");
    auto res = book_.on_restated(cid, int(std::min<int64_t>(e->cum + units_left, 2147483647)));
    if (res.outcome != Outcome::Ok)
      log_err("restatement of oid=%d to %ld units: %s", e->oid, long(e->cum + units_left), to_string(res.outcome));
    else
      log_omg("order oid=%d restated to %d units, %d left", e->oid, res.entry->qty, res.entry->leaves);
    return;
  }
  case 'I':
  {
    const char st = m.get_char(OrdStatus);
    if (xid && (st == '0' || st == '1' || st == '2'))
      book_.on_accepted(cid, xid);
    return on_status(m, cid);
  }
  case '5':
    log_err("Coinbase Exchange replaced %s, which this OM never requests: order changed outside the OM", clid);
    return;
  default:
    log_wrn("execution report ExecType %c ignored: %s", et, m.str());
  }
}

void CbxOM::on_trade(const FixMsg &m, ClientId cid) noexcept
{
  auto v = venue_of(cid);
  auto e = book_.find_cid(cid);
  if (!v || !e || !v->prod)
  {
    log_err("fill on an order with no product: %s: CHECK POSITIONS", m.str());
    return;
  }
  const std::string exec_id = m.get(ExecID);
  if (!exec_id.empty() && !v->exec_ids.insert(exec_id).second)
  {
    ++dups_;
    log_wrn("execution %s already applied, skipped", exec_id);
    return;
  }
  const auto last = scaled(m.get(LastQty), v->prod->size_decimals);
  const auto px = v->prod->ticks(m.get(LastPx));
  if (!last || !px || *last <= 0)
  {
    log_err("fill with LastQty %s LastPx %s not in the product's increments: %s: CHECK POSITIONS", m.get(LastQty),
            m.get(LastPx), m.str());
    return;
  }
  // A replayed report: CumQty no more than what this order has filled.
  if (auto cum = scaled(m.get(CumQty), v->prod->size_decimals); cum && m.has(CumQty))
  {
    const int64_t applied = applied_base(*v, *e);
    if (*cum <= applied)
    {
      ++dups_;
      log_wrn("fill on oid=%d with CumQty %s already counted (cum %d units + %ld), skipped", e->oid, m.get(CumQty),
              e->cum, long(v->remainder));
      return;
    }
    if (*cum - *last > applied)
      log_err("fill on oid=%d: CumQty %s says %s was filled before this one, the OM has %s: CHECK POSITIONS", e->oid,
              m.get(CumQty), format_scaled(*cum - *last, v->prod->size_decimals),
              format_scaled(applied, v->prod->size_decimals));
  }
  // The venue says the order is done (OrdStatus 2) but the OM's quantity is
  // not reached (a restatement it never saw): this fill ends it.
  const int64_t units = take_units(*v, *last);
  if (m.get_char(OrdStatus) == '2' && !frame::som::is_done(e->state) && e->cum + units < e->qty)
  {
    log_wrn("order oid=%d: Coinbase Exchange reports it filled at %d units, the OM expected %d: finishing it", e->oid,
            int(e->cum + units), e->qty);
    if (e->cum + units > 0)
      book_.on_restated(cid, int(e->cum + units));
    else
      canceled(cid, "filled below one unit");
  }
  if (units == 0)
  {
    log_inf("fill on oid=%d of %s is below one unit: carried (%s so far)", e->oid, m.get(LastQty),
            format_scaled(v->remainder, v->prod->size_decimals));
    return;
  }
  uint64_t tim = parse_fix_time(m.get(TransactTime));
  send_fill(cid, int(std::min<int64_t>(units, 2147483647)), *px, tim ? tim : epoch_ns_now(),
            m.get(TradeID).c_str());
}

// The answer to OrderStatusRequest after a reconnect.
void CbxOM::on_status(const FixMsg &m, ClientId cid) noexcept
{
  auto e = book_.find_cid(cid);
  auto v = venue_of(cid);
  if (!e || !v || !v->prod)
    return;
  const char st = m.get_char(OrdStatus);
  if (st == '8')
  {
    // Rejected: OrdRejReason 5 says Coinbase does not know the order (it
    // never arrived); any other reason, it was refused. Either way an order
    // not yet accepted is done.
    const std::string why = m.get(OrdRejReason);
    if (!e->accepted)
    {
      auto res = book_.on_rejected(cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(
            new som::Reject(uint(res.entry->oid), why == "5" ? som::Reject::DISCONNECTED : som::Reject::EXCHLIMIT),
            this);
      log_err("order oid=%d %s after the reconnect (OrdRejReason %s): rejected", e->oid,
              why == "5" ? "unknown to Coinbase Exchange" : "rejected by Coinbase Exchange", why);
    }
    else
      log_err("order oid=%d was accepted but Coinbase Exchange now reports it rejected (OrdRejReason %s): CHECK",
              e->oid, why);
    return;
  }
  if (st == '0' || st == '1' || st == '2')
    ack_once(cid);
  // Filled while disconnected and not reported: the missing quantity, at the
  // price that makes the order's average AvgPx.
  if (auto cum = scaled(m.get(CumQty, "0"), v->prod->size_decimals); cum && *cum > applied_base(*v, *e))
  {
    const int64_t missing_base = *cum - applied_base(*v, *e);
    const int64_t missing = take_units(*v, missing_base);
    auto avg = v->prod->ticks(m.get(AvgPx));
    log_err("order oid=%d: Coinbase Exchange has %s filled, the OM %d units: sending the missing %ld units at AvgPx "
            "%s: CHECK POSITIONS",
            e->oid, m.get(CumQty), e->cum, long(missing), m.get(AvgPx));
    if (missing > 0)
      send_fill(cid, int(std::min<int64_t>(missing, 2147483647)), avg ? *avg : e->px, epoch_ns_now(), "status");
  }
  if (st == '4' || st == 'C')
    canceled(cid, "canceled while disconnected");
}

void CbxOM::on_cancel_reject(const FixMsg &m) noexcept
{
  ClientId cid = cid_of(m.get(ClOrdID));
  if (!cid)
    cid = cid_of(m.get(OrigClOrdID));
  if (!cid)
  {
    log_err("cancel reject for an order unknown to this OM: %s", m.str());
    return;
  }
  auto res = book_.on_cancel_rejected(cid);
  if (res.outcome == Outcome::Ok && res.entry->sender)
    res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::FROMEXCHANGE), this);
  log_rej("Coinbase Exchange refused the cancel %s (%s, CxlRejReason %s): %s", m.get(ClOrdID), m.get(Text),
          m.get(CxlRejReason), to_string(res.outcome));
}

void CbxOM::on_business_reject(const FixMsg &m) noexcept
{
  const std::string ref_type = m.get(RefMsgType);
  ClientId cid = cid_of(m.get(BusinessRejectRefID));
  log_rej("Coinbase Exchange business reject of %s %s (reason %s): %s", ref_type, m.get(BusinessRejectRefID),
          m.get(BusinessRejectReason), m.get(Text));
  if (!cid)
    return;
  if (ref_type == "D")
  {
    auto res = book_.on_rejected(cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::Reject(uint(res.entry->oid), som::Reject::UNKNOWN), this);
  }
  else if (ref_type == "F")
  {
    auto res = book_.on_cancel_rejected(cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(
          new som::CancReject(uint(res.entry->oid), som::CancReject::FROMEXCHANGE_BUSINESSREJECT), this);
  }
}
}

// ---- factories --------------------------------------------------------------------------------

actors::Actor *create_CbxOM(const coinbase_exchange::oe::CbxOMConfig &cfg, actors::Actor *som,
                            coinbase_exchange::oe::SessionFactory factory, en::x venue)
{
  using namespace coinbase_exchange::oe;
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "CbxOM: not created, bad config: " << err << ". Coinbase Exchange is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? SimCbx::make_factory(cfg.sim_config) : quickfix_session_factory();
  if (!factory)
  {
    std::cerr << "CbxOM: not created, no session factory (built without QuickFIX?). Coinbase Exchange is unrouted."
              << std::endl;
    return nullptr;
  }
  return new CbxOM(cfg, som, std::move(factory), venue);
}
