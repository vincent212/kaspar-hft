/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_ouch/act/OuchOM.hpp"

#include <algorithm>
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

#include "nasdaq_ouch/OuchCodec.hpp"

namespace nasdaq::ouch
{
  namespace som = frame::som::msg;
  namespace from = from_nasdaq;
  using frame::som::ClientIdGen;
  using frame::som::Outcome;
  using frame::som::Result;
  using frame::som::State;
  using frame::som::to_string;
  using msg::OuchReport;

  // ---- session thread -> mailbox --------------------------------------------------------

  class OuchOM::Events : public SessionEvents
  {
  public:
    Events(OuchOM *om, uint64_t session) : om_(om), session_(session) {}

    void logged_in(const std::string &s, uint64_t next) override
    {
      auto r = make(OuchReport::LOGGED_IN);
      r->text = s;
      r->sequence = next;
      om_->send(r, nullptr);
    }
    void login_rejected(char reason) override { post(OuchReport::LOGIN_REJECTED, std::string(1, reason)); }
    void disconnected(const std::string &why) override { post(OuchReport::DISCONNECTED, why); }
    void end_of_session() override { post(OuchReport::END_OF_SESSION); }
    void busy() override { post(OuchReport::BUSY); }
    void available() override { post(OuchReport::AVAILABLE); }

    // Decoding happens here, on the session's thread; nothing else does.
    void message(uint64_t seq, const char *data, size_t len) override
    {
      auto d = from::decode(data, len);
      if (d.st == nasdaq::status::incomplete && len > 0 && len < 256)
      {
        // OUCH 5.0 marks Appendage Length optional only on some messages
        // (C, D, B, J, P, I, T, M, Q); the table has it on the others. If
        // one of those arrives without it, read it as length 0 rather than
        // lose the report (an Executed, say).
        char buf[258];
        std::memcpy(buf, data, len);
        buf[len] = buf[len + 1] = 0;
        auto d2 = from::decode(buf, len + 2);
        bool fits = d2 && d2.size == len + 2 &&
                    std::visit([](const auto &x) { return requires { x.AppendageLength; }; }, d2.msg);
        if (fits)
        {
          d = d2;
          d.tail = nullptr;
          d.tail_len = 0;
        }
      }
      auto r = make(d ? OuchReport::MESSAGE : OuchReport::UNDECODED);
      r->sequence = seq;
      if (d)
      {
        r->msg = d.msg;
        if (d.tail_len)
          r->appendage.assign(d.tail, d.tail_len);
      }
      else
        r->text = len ? std::string(1, data[0]) : std::string();
      om_->send(r, nullptr);
    }

  private:
    OuchReport *make(OuchReport::Kind k) { return new OuchReport(k, session_); }
    void post(OuchReport::Kind k, std::string text = {})
    {
      auto r = make(k);
      r->text = std::move(text);
      om_->send(r, nullptr);
    }
    OuchOM *om_;
    uint64_t session_;
  };

  // ---- units and mappings ----------------------------------------------------------------

  uint64_t OuchOM::to_ouch_price(int ticks, double tick_size)
  {
    long double d = (long double)ticks * tick_size * PRICE_SCALE;
    return d <= 0 ? 0 : uint64_t(std::llround(d));
  }

  namespace
  {
    // Time in force and cross type (OUCH 5.0 2.1, p.5). false: not available.
    bool map_tif(som::Order::TimeInForce t, char &tif, char &cross)
    {
      cross = 'N';
      switch (t)
      {
      case som::Order::TimeInForce::DAY: tif = '0'; return true;
      case som::Order::TimeInForce::IOC: tif = '3'; return true;
      case som::Order::TimeInForce::EXTENDED_DAY: tif = '5'; return true; // GTX
      case som::Order::TimeInForce::AT_OPEN: tif = '0'; cross = 'O'; return true;
      case som::Order::TimeInForce::AT_CLOSE: tif = '0'; cross = 'C'; return true;
      case som::Order::TimeInForce::GTC:
      case som::Order::TimeInForce::FOK: return false;
      }
      return false;
    }

    char map_side(const som::Order &o)
    {
      if (o.side == en::bs::BUY)
        return 'B';
      switch (o.short_sale)
      {
      case som::Order::ShortSale::SHORT: return 'T';
      case som::Order::ShortSale::SHORT_EXEMPT: return 'E';
      case som::Order::ShortSale::NONE: break;
      }
      return 'S';
    }

    int reject_reason(uint16_t code)
    {
      if (code >= 0x28 && code <= 0x2A) // symbol, port, duplicate message rate
        return som::Reject::THROTTLE;
      if (code == 0x06 || code == 0x1E || code == 0x1F || (code >= 0x20 && code <= 0x33))
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

  OuchOM::OuchOM(OuchOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
                 ClientIdGen ids)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), book_(ids)
  {
    strncpy(name, "OuchOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(OuchReport, report_handler);
  }

  OuchOM::~OuchOM()
  {
    stopping_ = true;
    reconnect_timer_ = {}; // stop and join the timer before tearing down
    drop_session();
  }

  uint32_t OuchOM::user_ref_num(int oid) const
  {
    auto e = book_.find_oid(oid);
    if (!e)
      return 0;
    auto it = urn_of_cid_.find(e->cid);
    return it == urn_of_cid_.end() ? 0 : it->second;
  }

  void OuchOM::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    LogonParams p;
    p.username = cfg_.username;
    p.password = cfg_.password;
    // A reconnect resumes the same SoupBinTCP session after the last report
    // applied; the first login of the process asks for first_sequence.
    p.session = soup_session_;
    p.sequence = next_seq_ ? next_seq_ : cfg_.first_sequence;
    try
    {
      session_ = factory_(cfg_.session, *events_);
      ++sessions_created_;
      log_omg("OUCH session %lu: logging in to session '%s' at sequence %lu", session_no_, p.session,
              p.sequence);
      session_->logon(p);
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create OUCH session: %s", e.what());
      schedule_reconnect();
    }
  }

  void OuchOM::drop_session() noexcept
  {
    logged_in_ = false;
    ready_ = false;
    busy_ = false;
    session_.reset(); // joins the session's thread: no events after this
    events_.reset();
  }

  void OuchOM::schedule_reconnect() noexcept
  {
    if (stopping_ || ended_)
      return;
    auto expected = session_no_;
    auto ms = cfg_.reconnect_ms;
    reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> l(m);
      if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
        send(new OuchReport(OuchReport::RECONNECT, expected), nullptr);
    });
  }

  void OuchOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no OUCH session factory: Nasdaq is unrouted");
      return;
    }
    // Start comes with fast_send on the Manager's thread; the session is
    // made on this actor's own thread, like every later one.
    send(new OuchReport(OuchReport::RECONNECT, session_no_), nullptr);
    if (som_)
      som_->send(new som::SetExchManager(venue_, this), this);
  }

  void OuchOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    if (session_ && logged_in_)
      session_->logout();
    drop_session();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at Nasdaq", book_.live());
  }

  // ---- requests from the SOM -----------------------------------------------------------------

  void OuchOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void OuchOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool OuchOM::take_token() noexcept
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

  void OuchOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    if (m->oid_to_cancel >= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no OUCH connection");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "OUCH session not logged in");
    if (busy_)
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "OUCH connection busy");
    if (m->ord_type == OT::STOP || m->ord_type == OT::STOP_LIMIT)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "OUCH has no stop orders");
    if (!m->strategy.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "OUCH has no broker algorithms");
    if (!m->route.empty() && m->route != "NASDAQ")
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "OUCH enters orders on Nasdaq only");
    char tif, cross;
    if (!map_tif(m->tif, tif, cross))
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on OUCH");
    if (m->tif == som::Order::TimeInForce::EXTENDED_DAY && m->ord_type == OT::MARKET)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "no market orders in extended hours");
    auto a = frame::ref::RefData::get_asset(std::size_t(m->sym));
    if (!a)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "unknown instrument");
    if (a->name.empty() || a->name.size() > 8)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "symbol does not fit OUCH's 8 characters");
    bool short_sale = m->side != en::bs::BUY && m->short_sale != som::Order::ShortSale::NONE;
    if (short_sale && m->locate_id.size() > 4)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "locate broker longer than 4 characters");

    if (!(m->sz > 0) || m->sz > MAX_QUANTITY || std::floor(m->sz) != m->sz)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "quantity must be 1 to 999,999 shares");
    if (cfg_.max_order_qty > 0 && m->sz > cfg_.max_order_qty)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order quantity limit");
    uint64_t px;
    if (m->ord_type == OT::MARKET)
      px = MARKET_PRICE;
    else
    {
      px = to_ouch_price(m->px, a->get_units());
      if (px == 0 || px > MAX_PRICE)
        return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "price outside $0.0001 to $199,999.99");
      if (cfg_.max_order_notional > 0 && double(px) / PRICE_SCALE * m->sz > cfg_.max_order_notional)
        return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order notional limit");
    }
    if (m->ord_type == OT::MARKET && cfg_.max_order_notional > 0)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "market order with a notional limit set");
    if (next_urn_ == 0) // wrapped: 2^32 orders today
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "UserRefNums exhausted");
    if (!take_token())
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    auto &e = *r.entry;
    uint32_t urn = next_urn_++;

    to_nasdaq::EnterOrder o{};
    o.UserRefNum = urn;
    o.Side = map_side(*m);
    o.Quantity = uint32_t(e.qty);
    o.set_Symbol(a->name);
    o.Price = px;
    o.TimeInForce = tif;
    o.Display = cfg_.display;
    o.Capacity = cfg_.capacity;
    o.InterMarketSweepEligibility = cfg_.intermarket_sweep;
    o.CrossType = cross;
    o.set_ClOrdID(std::to_string(m->oid));
    Appendage app;
    if (!cfg_.firm.empty())
      app.alpha(TAG_FIRM, cfg_.firm, 4);
    if (short_sale && !m->locate_id.empty())
    {
      app.alpha(TAG_SHARES_LOCATED, "Y", 1);
      app.alpha(TAG_LOCATE_BROKER, m->locate_id, 4);
    }

    switch (session_->enter(o, app.str()))
    {
    case CallStatus::Success:
    {
      auto &v = by_urn_[urn];
      v.urn = urn;
      v.cid = e.cid;
      v.shares = e.qty;
      urn_of_cid_[e.cid] = urn;
      log_omg("new order oid=%d urn=%u %c %d %s @ %lu tif %c cross %c", m->oid, urn, o.Side, e.qty,
              a->name, (unsigned long)px, tif, cross);
      return;
    }
    case CallStatus::Busy:
      book_.send_failed(e.cid);
      busy_ = true;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "OUCH connection busy");
    case CallStatus::Disconnected:
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "OUCH connection lost");
    }
  }

  void OuchOM::cancel_handler(const som::Cancel *m) noexcept
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
    auto u = urn_of_cid_.find(e.cid);
    if (u == urn_of_cid_.end()) // cannot happen: every order in the book was sent
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "order has no UserRefNum");
    }
    // Cancel the whole order: intended order size 0 (OUCH 5.0 2.3).
    to_nasdaq::CancelOrderRequest c{};
    c.UserRefNum = u->second;
    c.Quantity = 0;
    auto status = session_ && logged_in_ ? session_->cancel(c) : CallStatus::Disconnected;
    if (status != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      if (status == CallStatus::Busy)
        busy_ = true;
      return cancel_reject(m->sender, oid,
                           status == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                           "OUCH refused the cancel");
    }
    log_omg("cancel oid=%d urn=%u", oid, u->second);
  }

  // ---- reports from Nasdaq -------------------------------------------------------------------

  void OuchOM::report_handler(const OuchReport *m) noexcept
  {
    if (m->kind == OuchReport::RECONNECT)
    {
      if (!stopping_ && !ended_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->session != session_no_ || !session_)
      return; // from a session already dropped

    switch (m->kind)
    {
    case OuchReport::LOGGED_IN:
      logged_in_ = true;
      if (soup_session_.empty() || m->text != soup_session_)
      {
        // First login, or a different SoupBinTCP session: its numbering
        // starts afresh.
        if (!soup_session_.empty())
          log_err("OUCH logged in to session '%s', expected '%s': sequence numbers restart",
                  m->text, soup_session_);
        next_seq_ = m->sequence;
      }
      else if (m->sequence > next_seq_)
      {
        log_err("OUCH login resumed at sequence %lu, expected %lu: reports in between are missing",
                m->sequence, next_seq_);
        next_seq_ = m->sequence;
      }
      else if (m->sequence < next_seq_)
        log_wrn("OUCH replays from sequence %lu; up to %lu already applied, skipped", m->sequence,
                next_seq_ - 1);
      soup_session_ = m->text;
      log_omg("OUCH logged in, session '%s', next sequence %lu", soup_session_, next_seq_);
      // No order goes out until Nasdaq says which UserRefNum is next.
      if (session_->account_query() != CallStatus::Success)
      {
        log_err("OUCH account query not sent; reconnecting");
        drop_session();
        schedule_reconnect();
      }
      return;
    case OuchReport::LOGIN_REJECTED:
      // 'A' bad credentials, 'S' session not available: retrying will not help.
      log_err("OUCH login rejected (%s). Nasdaq is unrouted until restart.",
              m->text == "A" ? "not authorized" : m->text == "S" ? "session not available" : m->text.c_str());
      drop_session();
      return;
    case OuchReport::DISCONNECTED:
      log_err("OUCH connection lost: %s; reconnecting in %d ms (%zu orders live)", m->text, cfg_.reconnect_ms,
              book_.live());
      drop_session();
      schedule_reconnect();
      return;
    case OuchReport::END_OF_SESSION:
      log_omg("OUCH end of session '%s'", soup_session_);
      if (book_.live())
        log_err("OUCH session ended with %zu orders live", book_.live());
      ended_ = true;
      drop_session();
      return;
    case OuchReport::BUSY:
      busy_ = true;
      log_wrn("OUCH connection busy: refusing orders");
      return;
    case OuchReport::AVAILABLE:
      busy_ = false;
      log_omg("OUCH connection available");
      return;
    default:
      break;
    }

    // Sequenced messages. Below the next expected one: a replay of what was
    // already applied.
    if (next_seq_ && m->sequence < next_seq_)
    {
      log_inf("OUCH sequence %lu already applied, skipped", m->sequence);
      return;
    }
    if (next_seq_ && m->sequence > next_seq_)
      log_err("OUCH sequence gap: expected %lu, got %lu", next_seq_, m->sequence);
    next_seq_ = m->sequence + 1;

    if (m->kind == OuchReport::UNDECODED)
    {
      log_err("OUCH message %lu of type '%s' not decoded, ignored", m->sequence, m->text);
      return;
    }
    on_message(*m);
  }

  OuchOM::Venue *OuchOM::venue_order(uint32_t urn) noexcept
  {
    auto it = by_urn_.find(urn);
    return it == by_urn_.end() ? nullptr : &it->second;
  }

  void OuchOM::on_message(const OuchReport &r) noexcept
  {
    std::visit(
        [&](const auto &x) {
          using T = std::decay_t<decltype(x)>;
          if constexpr (std::is_same_v<T, from::OrderAccepted>)
            on_accepted(x);
          else if constexpr (std::is_same_v<T, from::OrderExecuted>)
            on_executed(x);
          else if constexpr (std::is_same_v<T, from::OrderCanceled>)
            on_decrement(x.UserRefNum, int(uint32_t(x.Quantity)), x.Reason, "canceled");
          else if constexpr (std::is_same_v<T, from::AIQCanceled>)
            on_decrement(x.UserRefNum, int(uint32_t(x.DecrementShares)), x.Reason, "AIQ canceled");
          else if constexpr (std::is_same_v<T, from::Rejected>)
            on_rejected(x);
          else if constexpr (std::is_same_v<T, from::CancelReject>)
            on_cancel_reject(x);
          else if constexpr (std::is_same_v<T, from::BrokenTrade>)
            log_err("BROKEN TRADE from Nasdaq: urn=%u match=%lu reason %c: CHECK POSITIONS", uint32_t(x.UserRefNum),
                    uint64_t(x.MatchNumber), x.Reason);
          else if constexpr (std::is_same_v<T, from::CancelPending>)
            log_omg("cancel pending for urn=%u: Nasdaq cancels it after the cross", uint32_t(x.UserRefNum));
          else if constexpr (std::is_same_v<T, from::AccountQueryResponse>)
          {
            uint32_t next = x.NextUserRefNum;
            if (next > next_urn_)
              next_urn_ = next;
            if (logged_in_ && !ready_)
            {
              ready_ = true;
              log_omg("OUCH ready: next UserRefNum %u (Nasdaq said %u)", next_urn_, next);
            }
          }
          else if constexpr (std::is_same_v<T, from::SystemEvent>)
            log_omg("Nasdaq system event %c (%s)", x.EventCode,
                    x.EventCode == 'S' ? "start of day" : x.EventCode == 'E' ? "end of day" : "unknown");
          else if constexpr (std::is_same_v<T, from::OrderReplaced> || std::is_same_v<T, from::OrderModified>)
            log_err("Nasdaq %s urn=%u, which this OM never requests: order changed outside the OM",
                    from::name(r.msg), uint32_t(x.UserRefNum));
          else if constexpr (std::is_same_v<T, from::OrderRestated>)
            log_inf("Nasdaq restated urn=%u reason %c", uint32_t(x.UserRefNum), x.Reason);
          else if constexpr (std::is_same_v<T, from::OrderPriorityUpdate>)
            log_inf("Nasdaq priority update urn=%u new reference %lu", uint32_t(x.UserRefNum),
                    uint64_t(x.OrderReferenceNumber));
          else
            log_wrn("Nasdaq %s, which this OM never requests: ignored", from::name(r.msg));
        },
        r.msg);
  }

  void OuchOM::on_accepted(const from::OrderAccepted &m) noexcept
  {
    uint32_t urn = m.UserRefNum;
    auto v = venue_order(urn);
    if (!v)
    {
      log_err("Nasdaq accepted urn=%u, unknown to this OM (entered outside it?)", urn);
      return;
    }
    auto res = book_.on_accepted(v->cid, m.OrderReferenceNumber);
    if (res.outcome != Outcome::Ok)
    {
      log_err("accept of urn=%u: %s", urn, to_string(res.outcome));
      return;
    }
    if (int(uint32_t(m.Quantity)) != v->shares)
      log_wrn("Nasdaq accepted urn=%u for %u shares, entered %d", urn, uint32_t(m.Quantity), v->shares);
    v->shares = int(uint32_t(m.Quantity));
    auto &e = *res.entry;
    if (!v->acked)
    {
      v->acked = true;
      if (e.sender)
        e.sender->send(new som::Ack(uint(e.oid), e.exch_oid), this);
    }
    if (m.OrderState == 'D') // accepted and canceled at once; nothing else follows
      return venue_done(*v, "accepted dead");
    if (v->open() <= 0)       // shares accepted fewer than those already executed
      venue_done(*v, "nothing left after accept");
  }

  void OuchOM::on_executed(const from::OrderExecuted &m) noexcept
  {
    uint32_t urn = m.UserRefNum;
    int qty = int(uint32_t(m.Quantity));
    uint64_t px = m.Price;
    uint64_t match = m.MatchNumber;
    auto v = venue_order(urn);
    if (!v)
    {
      log_err("FILL ON UNKNOWN ORDER urn=%u qty=%d px=%lu match=%lu: CHECK POSITIONS", urn, qty,
              (unsigned long)px, (unsigned long)match);
      return;
    }
    if (!v->matches.insert(match).second)
    {
      log_wrn("execution match=%lu on urn=%u already applied, skipped", (unsigned long)match, urn);
      return;
    }
    v->executed += qty;
    auto res = book_.on_fill(v->cid, qty);
    if (res.outcome == Outcome::Ok)
    {
      const auto &e = *res.entry;
      const auto &o = e.order;
      uint64_t tim = eastern_midnight_epoch_ns(epoch_ns_now()) + uint64_t(m.Timestamp);
      if (e.sender)
        e.sender->send(new som::Fill(uint(e.oid), venue_, uint(o.sym),
                                     frame::ref::Price((long double)px / PRICE_SCALE, uint(o.sym)), o.side, qty,
                                     e.leaves, o.owner, tim),
                       this);
      log_fil("fill oid=%d urn=%u %d @ %lu leaves %d match %lu", e.oid, urn, qty, (unsigned long)px, e.leaves,
              (unsigned long)match);
    }
    else
      log_err("fill on urn=%u qty=%d match=%lu: %s: CHECK POSITIONS", urn, qty, (unsigned long)match,
              to_string(res.outcome));
    if (v->open() <= 0)
      venue_done(*v, "filled to what Nasdaq left open");
  }

  // Nasdaq took shares off the order: by request, by itself, or by
  // self-match prevention. Only when nothing is left open is the order
  // canceled for the SOM, which has no partial cancel.
  void OuchOM::on_decrement(uint32_t urn, int shares, char reason, const char *what) noexcept
  {
    auto v = venue_order(urn);
    if (!v)
    {
      log_err("Nasdaq %s urn=%u (%d shares), unknown to this OM", what, urn, shares);
      return;
    }
    v->decremented += shares;
    log_omg("Nasdaq %s urn=%u %d shares: %s; %d open", what, urn, shares, cancel_reason_text(reason), v->open());
    if (v->open() <= 0)
      venue_done(*v, cancel_reason_text(reason));
    else
      log_wrn("urn=%u reduced by Nasdaq to %d open; the SOM is not told until the order ends", urn, v->open());
  }

  // Nothing is open at Nasdaq any more. If the book still has shares left
  // (Nasdaq canceled or reduced them), the order is canceled.
  void OuchOM::venue_done(Venue &v, const char *why) noexcept
  {
    auto e = book_.find_cid(v.cid);
    if (!e || frame::som::is_done(e->state))
      return;
    auto res = book_.on_canceled(v.cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
    log_omg("order oid=%d urn=%u canceled (%s), filled %d", res.entry->oid, v.urn, why, res.entry->cum);
  }

  void OuchOM::on_rejected(const from::Rejected &m) noexcept
  {
    uint32_t urn = m.UserRefNum;
    uint16_t code = m.Reason;
    auto v = venue_order(urn);
    if (!v)
    {
      log_err("Nasdaq rejected urn=%u (%s), unknown to this OM", urn, reject_reason_text(code));
      return;
    }
    auto res = book_.on_rejected(v->cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), reject_reason(code)), this);
    log_rej("Nasdaq rejected urn=%u: 0x%04x %s (%s)", urn, unsigned(code), reject_reason_text(code),
            to_string(res.outcome));
  }

  void OuchOM::on_cancel_reject(const from::CancelReject &m) noexcept
  {
    uint32_t urn = m.UserRefNum;
    auto v = venue_order(urn);
    if (!v)
    {
      log_err("Nasdaq cancel reject for urn=%u, unknown to this OM", urn);
      return;
    }
    auto res = book_.on_cancel_rejected(v->cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::FROMEXCHANGE), this);
    log_rej("Nasdaq refused the cancel of urn=%u (%s)", urn, to_string(res.outcome));
  }
}

// ---- factories ---------------------------------------------------------------------------------

actors::Actor *create_OuchOM(const nasdaq::ouch::OuchOMConfig &cfg, actors::Actor *som,
                             nasdaq::ouch::SessionFactory factory, en::x venue)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "OuchOM: not created, bad config: " << err << ". Nasdaq is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? nasdaq::ouch::SimOuch::make_factory(cfg.sim_config) : nasdaq::ouch::soup_session_factory();
  if (!factory)
  {
    std::cerr << "OuchOM: not created, no session factory. Nasdaq is unrouted." << std::endl;
    return nullptr;
  }
  return new nasdaq::ouch::OuchOM(cfg, som, std::move(factory), venue);
}

actors::Actor *create_OuchOM(const std::string &config_path, actors::Actor *som)
{
  nasdaq::ouch::OuchOMConfig cfg;
  std::string err;
  if (!nasdaq::ouch::OuchOMConfig::from_file(config_path, cfg, err))
  {
    std::cerr << "OuchOM: not created, cannot read " << config_path << ": " << err << ". Nasdaq is unrouted."
              << std::endl;
    return nullptr;
  }
  return create_OuchOM(cfg, som);
}
