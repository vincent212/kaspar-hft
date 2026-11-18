/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_oe/act/KrakenOM.hpp"

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
#include "kraken_md/KrakenJson.hpp"
#include "logger/act/Logger.hpp"

namespace kraken::oe
{
  namespace som = frame::som::msg;
  using frame::som::ClientId;
  using frame::som::ClientIdGen;
  using frame::som::Outcome;
  using frame::som::State;
  using frame::som::to_string;
  using msg::KrakenEvent;

  // ---- session thread -> mailbox ------------------------------------------------------------

  class KrakenOM::Events : public SessionEvents
  {
  public:
    Events(KrakenOM *om, uint64_t s) : om_(om), s_(s) {}
    void logged_in() override { post(KrakenEvent::LOGGED_IN); }
    void login_failed(const std::string &why) override { post(KrakenEvent::LOGIN_FAILED, why); }
    void disconnected(const std::string &why) override { post(KrakenEvent::DISCONNECTED, why); }
    void busy() override { post(KrakenEvent::BUSY); }
    void available() override { post(KrakenEvent::AVAILABLE); }
    void report(const Report &r) override
    {
      auto e = new KrakenEvent(KrakenEvent::REPORT, s_);
      e->report = r;
      om_->send(e, nullptr);
    }
    void gap(uint64_t expected, uint64_t got) override
    {
      auto e = new KrakenEvent(KrakenEvent::GAP, s_);
      e->expected = expected;
      e->got = got;
      om_->send(e, nullptr);
    }

  private:
    void post(KrakenEvent::Kind k, std::string text = {})
    {
      auto e = new KrakenEvent(k, s_);
      e->text = std::move(text);
      om_->send(e, nullptr);
    }
    KrakenOM *om_;
    uint64_t s_;
  };

  // ---- identifiers and units ---------------------------------------------------------------

  std::string KrakenOM::encode_cid(ClientId cid)
  {
    static const char *d = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    do
    {
      s.push_back(d[cid % 36]);
      cid /= 36;
    } while (cid);
    std::reverse(s.begin(), s.end());
    return s;
  }

  bool KrakenOM::decode_cid(const std::string &s, ClientId &cid)
  {
    if (s.empty() || s.size() > 13)
      return false;
    unsigned __int128 v = 0;
    for (char c : s)
    {
      int x;
      if (c >= '0' && c <= '9')
        x = c - '0';
      else if (c >= 'a' && c <= 'z')
        x = c - 'a' + 10;
      else
        return false;
      v = v * 36 + unsigned(x);
    }
    if (v >> 63)
      return false;
    cid = ClientId(v);
    return true;
  }

  uint64_t KrakenOM::fnv1a(const std::string &s)
  {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s)
    {
      h ^= c;
      h *= 1099511628211ull;
    }
    return h;
  }

  std::string KrakenOM::decimal(int64_t units, double unit)
  {
    const int64_t per = std::llround(unit * double(kraken::kOne));
    int decimals = 0;
    for (int64_t p = per; p % 10 == 0 && decimals < kraken::kScale; p /= 10)
      ++decimals;
    decimals = kraken::kScale - decimals;
    const __int128 v = __int128(units) * per;
    const bool neg = v < 0;
    const unsigned __int128 a = neg ? -v : v;
    std::string ip = std::to_string(uint64_t(a / kraken::kOne));
    std::string fp = std::to_string(uint64_t(a % kraken::kOne));
    fp.insert(0, size_t(kraken::kScale) - fp.size(), '0');
    fp.resize(size_t(decimals));
    return (neg ? "-" : "") + ip + (decimals ? "." + fp : std::string());
  }

  bool KrakenOMConfig::valid(std::string &err) const
  {
    if (!(qty_unit > 0) || qty_unit < 1e-10)
    {
      err = "qty_unit must be positive and at least 1e-10";
      return false;
    }
    if (reconnect_ms < 10)
    {
      err = "reconnect_ms below 10";
      return false;
    }
    return true;
  }

  namespace
  {
    int reject_reason(const std::string &e)
    {
      if (e.find("Rate limit") != std::string::npos || e.find("rate limit") != std::string::npos ||
          e.find("Throttled") != std::string::npos || e.find("apiLimitExceeded") != std::string::npos)
        return som::Reject::THROTTLE;
      if (e.find("Insufficient") != std::string::npos || e.find("insufficient") != std::string::npos ||
          e.find("Orders limit") != std::string::npos || e.find("Positions limit") != std::string::npos)
        return som::Reject::EXCHLIMIT;
      return som::Reject::UNKNOWN;
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

    uint64_t epoch_ns_now()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }
  }

  // ---- lifecycle -----------------------------------------------------------------------------

  KrakenOM::KrakenOM(KrakenOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue, ClientIdGen ids)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), book_(ids)
  {
    strncpy(name, "KrakenOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(KrakenEvent, event_handler);
  }

  KrakenOM::~KrakenOM()
  {
    stopping_ = true;
    reconnect_timer_ = {};
    drop_session();
  }

  std::string KrakenOM::client_order_id(int oid) const
  {
    auto e = book_.find_oid(oid);
    return e ? cfg_.clid_prefix + encode_cid(e->cid) : std::string();
  }

  size_t KrakenOM::parked() const
  {
    size_t n = 0;
    for (auto &[k, v] : parked_)
      n += v.size();
    return n;
  }

  void KrakenOM::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(*events_);
      ++sessions_created_;
      log_omg("Kraken session %lu: logging on", session_no_);
      session_->logon();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create Kraken session: %s", e.what());
      schedule_reconnect();
    }
  }

  void KrakenOM::drop_session() noexcept
  {
    ready_ = false;
    busy_ = false;
    session_.reset(); // joins the session's thread: no events after this
    events_.reset();
  }

  void KrakenOM::schedule_reconnect() noexcept
  {
    if (stopping_ || login_failed_)
      return;
    auto expected = session_no_;
    auto ms = cfg_.reconnect_ms;
    reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> l(m);
      if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
        send(new KrakenEvent(KrakenEvent::RECONNECT, expected), nullptr);
    });
  }

  void KrakenOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no Kraken session factory: Kraken is unrouted");
      return;
    }
    send(new KrakenEvent(KrakenEvent::RECONNECT, session_no_), nullptr);
    if (som_)
      som_->send(new som::SetExchManager(venue_, this), this);
  }

  void KrakenOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    if (session_ && ready_)
      session_->logout();
    drop_session();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at Kraken", book_.live());
  }

  // ---- requests from the SOM -----------------------------------------------------------------

  void KrakenOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void KrakenOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool KrakenOM::take_token() noexcept
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

  void KrakenOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    using TIF = som::Order::TimeInForce;
    if (m->oid_to_cancel >= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Kraken connection");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Kraken session not logged on");
    if (busy_)
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Kraken connection busy");
    if (m->ord_type == OT::STOP || m->ord_type == OT::STOP_LIMIT)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "stop orders are not supported by this OM");
    if (!m->strategy.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Kraken has no broker algorithms");
    if (!m->route.empty() && m->route != "KRAKEN")
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "route other than KRAKEN");
    if (m->side != en::bs::BUY && m->short_sale != som::Order::ShortSale::NONE)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "short-sale marks do not exist on Kraken spot");
    Tif tif;
    switch (m->tif)
    {
    case TIF::GTC: tif = Tif::GTC; break;
    case TIF::IOC: tif = Tif::IOC; break;
    case TIF::FOK: tif = Tif::FOK; break;
    case TIF::DAY:
      if (!cfg_.day_as_gtc)
        return reject(m->sender, m->oid, som::Reject::UNKNOWN, "DAY not available (day_as_gtc = false)");
      tif = Tif::GTC;
      break;
    default: return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on Kraken");
    }
    auto a = frame::ref::RefData::get_asset(std::size_t(m->sym));
    if (!a)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "unknown instrument");
    if (!(m->sz > 0) || std::floor(m->sz) != m->sz || m->sz > 2e9)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a positive whole number of units");
    if (cfg_.max_order_units > 0 && m->sz > double(cfg_.max_order_units))
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order size limit");
    const double tick = a->get_units();
    const double px = double(m->px) * tick;
    if (m->ord_type != OT::MARKET && !(px > 0))
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "price must be positive");
    if (cfg_.max_order_notional > 0)
    {
      if (m->ord_type == OT::MARKET)
        return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "market order with a notional limit set");
      if (px * m->sz * cfg_.qty_unit > cfg_.max_order_notional)
        return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
    }
    if (!take_token())
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    auto &e = *r.entry;

    NewOrder o;
    o.cl_ord_id = cfg_.clid_prefix + encode_cid(e.cid);
    auto sit = cfg_.symbols.find(a->name);
    o.symbol = sit == cfg_.symbols.end() ? a->name : sit->second;
    o.buy = m->side == en::bs::BUY;
    o.qty = decimal(int64_t(m->sz), cfg_.qty_unit);
    o.market = m->ord_type == OT::MARKET;
    if (!o.market)
      o.price = decimal(int64_t(m->px), tick);
    o.tif = tif;
    o.post_only = cfg_.post_only && !o.market;

    switch (session_->add_order(o))
    {
    case CallStatus::Success:
    {
      auto &v = by_clid_[o.cl_ord_id];
      v.cid = e.cid;
      v.cl_ord_id = o.cl_ord_id;
      v.tick = tick;
      log_omg("new order oid=%d cl_ord_id=%s %s %s %s @ %s", m->oid, o.cl_ord_id, o.buy ? "buy" : "sell", o.qty,
              o.symbol, o.market ? std::string("market") : o.price);
      return;
    }
    case CallStatus::Busy:
      book_.send_failed(e.cid);
      busy_ = true;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Kraken connection busy");
    case CallStatus::Disconnected:
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Kraken connection lost");
    }
  }

  void KrakenOM::cancel_handler(const som::Cancel *m) noexcept
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
    default: return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "no such order");
    }
    auto &e = *r.entry;
    const std::string cl = cfg_.clid_prefix + encode_cid(e.cid);
    auto v = by_clid_.find(cl);
    const std::string order_id = v == by_clid_.end() ? std::string() : v->second.order_id;
    auto status = session_ && ready_ ? session_->cancel_order(order_id, cl) : CallStatus::Disconnected;
    if (status != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      if (status == CallStatus::Busy)
        busy_ = true;
      return cancel_reject(m->sender, oid,
                           status == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                           "Kraken refused the cancel");
    }
    log_omg("cancel oid=%d cl_ord_id=%s order_id=%s", oid, cl, order_id);
  }

  // ---- events from the session ---------------------------------------------------------------

  void KrakenOM::event_handler(const KrakenEvent *m) noexcept
  {
    if (m->kind == KrakenEvent::RECONNECT)
    {
      if (!stopping_ && !login_failed_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->session != session_no_ || !session_)
      return; // from a session already dropped
    switch (m->kind)
    {
    case KrakenEvent::LOGGED_IN:
      ready_ = true;
      log_omg("Kraken session %lu logged on (%zu orders live)", session_no_, book_.live());
      return;
    case KrakenEvent::LOGIN_FAILED:
      log_err("Kraken logon failed: %s. Kraken is unrouted until restart.", m->text);
      login_failed_ = true;
      drop_session();
      return;
    case KrakenEvent::DISCONNECTED:
      log_err("Kraken connection lost: %s; reconnecting in %d ms (%zu orders live)", m->text, cfg_.reconnect_ms,
              book_.live());
      drop_session();
      schedule_reconnect();
      return;
    case KrakenEvent::GAP:
      log_err("Kraken executions sequence gap: expected %lu, got %lu; reconnecting for a snapshot", m->expected,
              m->got);
      drop_session();
      schedule_reconnect();
      return;
    case KrakenEvent::BUSY:
      busy_ = true;
      log_wrn("Kraken connection busy: refusing orders");
      return;
    case KrakenEvent::AVAILABLE:
      busy_ = false;
      log_omg("Kraken connection available");
      return;
    case KrakenEvent::REPORT: on_report(m->report); return;
    default: return;
    }
  }

  KrakenOM::Venue *KrakenOM::resolve(const Report &r) noexcept
  {
    std::string cl = r.cl_ord_id;
    if (cl.empty() && !r.order_id.empty())
    {
      auto it = clid_of_order_.find(r.order_id);
      if (it != clid_of_order_.end())
        cl = it->second;
    }
    if (cl.empty())
      return nullptr;
    auto v = by_clid_.find(cl);
    if (v == by_clid_.end())
      return nullptr;
    if (!r.order_id.empty() && v->second.order_id.empty())
    {
      v->second.order_id = r.order_id;
      clid_of_order_[r.order_id] = cl;
    }
    return &v->second;
  }

  void KrakenOM::on_report(const Report &r) noexcept
  {
    Venue *v = resolve(r);
    if (!v)
    {
      if (r.cl_ord_id.empty() && !r.order_id.empty() && r.kind != Report::Other)
      {
        // The venue id is not paired with a client id yet: hold it.
        auto &p = parked_[r.order_id];
        if (p.size() < 64)
          p.push_back(r);
        log_inf("Kraken report for order_id=%s held until its client id is known", r.order_id);
        return;
      }
      if (r.kind == Report::Trade)
        log_err("FILL ON UNKNOWN ORDER order_id=%s cl_ord_id=%s exec_id=%s %s @ %s: CHECK POSITIONS", r.order_id,
                r.cl_ord_id, r.exec_id, r.last_qty, r.last_price);
      else if (r.kind != Report::Other)
        log_wrn("Kraken report for an order unknown to this OM (order_id=%s cl_ord_id=%s): ignored", r.order_id,
                r.cl_ord_id);
      return;
    }
    apply(*v, r);
    // Reports held for this venue id can now be applied, in arrival order.
    if (!v->order_id.empty())
    {
      auto p = parked_.find(v->order_id);
      if (p != parked_.end())
      {
        auto held = std::move(p->second);
        parked_.erase(p);
        for (const auto &h : held)
          apply(*v, h);
      }
    }
  }

  void KrakenOM::apply(Venue &v, const Report &r) noexcept
  {
    switch (r.kind)
    {
    case Report::Accepted:
    {
      auto res = book_.on_accepted(v.cid, v.order_id.empty() ? 0 : fnv1a(v.order_id));
      if (res.outcome == Outcome::Ok && !v.acked)
      {
        v.acked = true;
        if (res.entry->sender)
          res.entry->sender->send(new som::Ack(uint(res.entry->oid), res.entry->exch_oid), this);
      }
      else if (res.outcome != Outcome::Ok && res.outcome != Outcome::Late)
        log_wrn("accept of %s: %s", v.cl_ord_id, to_string(res.outcome));
      return;
    }
    case Report::Rejected:
    {
      auto res = book_.on_rejected(v.cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), reject_reason(r.reason)), this);
      log_rej("Kraken rejected %s: %s (%s)", v.cl_ord_id, r.reason, to_string(res.outcome));
      return;
    }
    case Report::Trade:
    {
      if (!r.exec_id.empty() && !v.execs.insert(r.exec_id).second)
      {
        log_inf("execution %s on %s already applied, skipped", r.exec_id, v.cl_ord_id);
        return;
      }
      int64_t q = 0, unit = std::llround(cfg_.qty_unit * double(kraken::kOne));
      if (!kraken::to_fixed(r.last_qty, q) || q <= 0)
      {
        log_err("execution %s on %s: unreadable quantity '%s': CHECK POSITIONS", r.exec_id, v.cl_ord_id, r.last_qty);
        return;
      }
      if (q % unit)
        log_err("execution %s on %s: %s is not a whole number of qty_unit; rounded: CHECK qty_unit", r.exec_id,
                v.cl_ord_id, r.last_qty);
      const int units = int(std::llround(double(q) / double(unit)));
      auto res = book_.on_fill(v.cid, units);
      if (res.outcome == Outcome::Ok)
      {
        const auto &e = *res.entry;
        const auto &o = e.order;
        int64_t pf = 0;
        kraken::to_fixed(r.last_price, pf);
        if (e.sender)
          e.sender->send(new som::Fill(uint(e.oid), venue_, uint(o.sym),
                                       frame::ref::Price((long double)kraken::fixed_to_double(pf), uint(o.sym)),
                                       o.side, units, e.leaves, o.owner, r.time_ns ? r.time_ns : epoch_ns_now()),
                         this);
        log_fil("fill oid=%d %s %d @ %s leaves %d exec %s", e.oid, v.cl_ord_id, units, r.last_price, e.leaves,
                r.exec_id);
      }
      else
        log_err("fill on %s exec %s qty %s: %s: CHECK POSITIONS", v.cl_ord_id, r.exec_id, r.last_qty,
                to_string(res.outcome));
      return;
    }
    case Report::Filled:
    {
      auto e = book_.find_cid(v.cid);
      if (e && !frame::som::is_done(e->state))
        log_err("Kraken says %s is filled, the book has %d of %d: a fill is missing", v.cl_ord_id, e->cum, e->qty);
      return;
    }
    case Report::Canceled:
    {
      auto res = book_.on_canceled(v.cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
      log_omg("Kraken canceled %s (%s): %s", v.cl_ord_id, r.reason, to_string(res.outcome));
      return;
    }
    case Report::CancelRejected:
    {
      auto res = book_.on_cancel_rejected(v.cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::FROMEXCHANGE), this);
      log_rej("Kraken refused the cancel of %s: %s (%s)", v.cl_ord_id, r.reason, to_string(res.outcome));
      return;
    }
    case Report::Restated:
      log_wrn("Kraken changed %s outside the OM (%s %s)", v.cl_ord_id, r.status, r.reason);
      return;
    case Report::Other: return;
    }
  }
}

actors::Actor *create_KrakenOM(const kraken::oe::KrakenOMConfig &cfg, actors::Actor *som,
                               kraken::oe::SessionFactory factory, en::x venue)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "KrakenOM: not created, bad config: " << err << ". Kraken is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
  {
    std::cerr << "KrakenOM: not created, no session factory. Kraken is unrouted." << std::endl;
    return nullptr;
  }
  return new kraken::oe::KrakenOM(cfg, som, std::move(factory), venue);
}
