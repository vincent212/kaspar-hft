/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "currenex_ust/act/CurrenexOM.hpp"

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

namespace currenex::ouch
{
  namespace som = frame::som::msg;
  using frame::som::ClientIdGen;
  using frame::som::Outcome;
  using frame::som::Result;
  using frame::som::State;
  using frame::som::to_string;
  using msg::CxReport;

  // ---- client order ids ------------------------------------------------------------------

  int64_t ClOrderIdGen::week_start_ms(int64_t epoch_ms)
  {
    int64_t d = epoch_ms >= 0 ? epoch_ms / MS_PER_DAY : -((-epoch_ms + MS_PER_DAY - 1) / MS_PER_DAY);
    // 1970-01-01 was a Thursday; Saturday is two days later.
    int64_t since_sat = ((d + 5) % 7 + 7) % 7;
    return (d - since_sat) * MS_PER_DAY;
  }

  int32_t ClOrderIdGen::next()
  {
    int64_t now = now_();
    int64_t week = week_start_ms(now);
    if (week != week_)
    {
      week_ = week;
      last_ = 0;
    }
    int64_t id = std::max(last_ + 1, std::max<int64_t>(1, now - week));
    if (id > INT32_MAX)
      return 0;
    last_ = id;
    return int32_t(id);
  }

  // ---- session thread -> mailbox --------------------------------------------------------

  class CurrenexOM::Events : public SessionEvents
  {
  public:
    Events(CurrenexOM *om, uint64_t session) : om_(om), session_(session) {}

    void logged_in(int32_t id, uint32_t seq) override
    {
      auto r = make(CxReport::LOGGED_IN);
      r->session_id = id;
      r->sequence = seq;
      om_->send(r, nullptr);
    }
    void logged_out(const std::string &reason, bool before) override
    {
      auto r = make(CxReport::LOGGED_OUT);
      r->text = reason;
      r->before_login = before;
      om_->send(r, nullptr);
    }
    void disconnected(const std::string &why) override { post(CxReport::DISCONNECTED, why); }
    void busy() override { post(CxReport::BUSY); }
    void available() override { post(CxReport::AVAILABLE); }

    // Decoding happens here, on the session's thread.
    void message(uint32_t seq, int32_t ts, const char *data, size_t len) override
    {
      auto d = from::decode(data, len);
      auto r = make(d ? CxReport::MESSAGE : CxReport::UNDECODED);
      r->sequence = seq;
      r->ts_ms = ts;
      if (d)
        r->msg = d.msg;
      else
        r->text = len ? std::string(1, data[0]) : std::string();
      om_->send(r, nullptr);
    }
    void undecoded(uint32_t seq, char type) override
    {
      auto r = make(CxReport::UNDECODED);
      r->sequence = seq;
      r->text = std::string(1, type);
      om_->send(r, nullptr);
    }

  private:
    CxReport *make(CxReport::Kind k) { return new CxReport(k, session_); }
    void post(CxReport::Kind k, std::string text = {})
    {
      auto r = make(k);
      r->text = std::move(text);
      om_->send(r, nullptr);
    }
    CurrenexOM *om_;
    uint64_t session_;
  };

  // ---- units and mappings ----------------------------------------------------------------

  int64_t CurrenexOM::to_rate(int ticks, double tick_size)
  {
    long double d = (long double)ticks * tick_size * RATE_SCALE;
    return d <= 0 ? 0 : int64_t(std::llround(d));
  }

  namespace
  {
    int reject_reason(int code)
    {
      switch (code)
      {
      case 0x0c: return som::Reject::THROTTLE;  // Maximum Permitted Operations Exceeded
      case 0x0b:                                // Invalid Credit
      case 0x10:                                // Maximum Number of Active Orders Exceeded
      case 0x1a: return som::Reject::EXCHLIMIT; // Absolute Trading Limit Exceeded
      }
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

    // Logout reasons after which a reconnect would not help or would fight
    // another session (Appendix B).
    bool final_logout(const std::string &r)
    {
      return r == "A1" || r == "A3" || r == "A5" || r == "A11";
    }

    std::string trim(std::string_view s)
    {
      size_t b = 0, e = s.size();
      while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\0'))
        --e;
      while (b < e && (s[b] == ' ' || s[b] == '\0'))
        ++b;
      return std::string(s.substr(b, e - b));
    }
  }

  // ---- lifecycle -----------------------------------------------------------------------------

  CurrenexOM::CurrenexOM(CurrenexOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
                         ClientIdGen ids, ClOrderIdGen clids)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), book_(ids),
        clids_(std::move(clids)), unit_cents_(int64_t(std::llround(cfg_.face_per_unit * AMOUNT_SCALE))),
        routed_key_(cfg_.routed_user_id.empty() ? cfg_.routed_user_key : UNKNOWN_KEY)
  {
    strncpy(name, "CurrenexOM", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(CxReport, report_handler);
  }

  CurrenexOM::~CurrenexOM()
  {
    stopping_ = true;
    reconnect_timer_ = {};
    drop_session();
  }

  int32_t CurrenexOM::cl_order_id(int oid) const
  {
    auto e = book_.find_oid(oid);
    if (!e)
      return 0;
    auto it = clid_of_cid_.find(e->cid);
    return it == clid_of_cid_.end() ? 0 : it->second;
  }

  int16_t CurrenexOM::instrument_index(const std::string &cusip) const
  {
    auto it = index_of_cusip_.find(cusip);
    return it == index_of_cusip_.end() ? -1 : it->second;
  }

  void CurrenexOM::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    LogonParams p;
    p.user = cfg_.user;
    p.password = cfg_.password;
    p.version = cfg_.version;
    try
    {
      session_ = factory_(cfg_.session, *events_);
      ++sessions_created_;
      log_omg("Currenex session %lu: logging on as '%s'", session_no_, p.user);
      session_->logon(p);
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create Currenex session: %s", e.what());
      schedule_reconnect();
    }
  }

  void CurrenexOM::drop_session() noexcept
  {
    logged_in_ = false;
    ready_ = false;
    busy_ = false;
    session_.reset(); // joins the session's thread: no events after this
    events_.reset();
  }

  void CurrenexOM::schedule_reconnect() noexcept
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
        send(new CxReport(CxReport::RECONNECT, expected), nullptr);
    });
  }

  void CurrenexOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no Currenex session factory: the venue is unrouted");
      return;
    }
    send(new CxReport(CxReport::RECONNECT, session_no_), nullptr);
    if (som_)
      som_->send(new som::SetExchManager(venue_, this), this);
  }

  void CurrenexOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    if (session_ && logged_in_)
      session_->logout();
    drop_session();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at Currenex", book_.live());
  }

  void CurrenexOM::update_ready() noexcept
  {
    bool r = logged_in_ && routed_key_ != UNKNOWN_KEY;
    if (r && !ready_)
      log_omg("Currenex ready: RoutedUserKey %d", routed_key_);
    ready_ = r;
  }

  // ---- requests from the SOM -----------------------------------------------------------------

  void CurrenexOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void CurrenexOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool CurrenexOM::take_token() noexcept
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

  void CurrenexOM::order_handler(const som::Order *m) noexcept
  {
    using OT = som::Order::OrdType;
    using TIF = som::Order::TimeInForce;
    if (m->oid_to_cancel >= 0)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "cancel/replace not supported");
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Currenex connection");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Currenex session not logged on");
    if (busy_)
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Currenex connection busy");
    if (m->ord_type != OT::LIMIT)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Currenex OUCH takes limit orders only");
    char expire;
    switch (m->tif)
    {
    case TIF::DAY: // no day order in OUCH: GTC, cancelled on disconnect (8.7)
    case TIF::GTC: expire = 'G'; break;
    case TIF::IOC: expire = 'I'; break;
    default: return reject(m->sender, m->oid, som::Reject::UNKNOWN, "time in force not available on Currenex OUCH");
    }
    if (!m->strategy.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Currenex OUCH has no broker algorithms");
    if (!m->route.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "Currenex OUCH has no routes");
    auto a = frame::ref::RefData::get_asset(std::size_t(m->sym));
    if (!a)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "unknown instrument");
    std::string cusip = trim(m->cusip.empty() ? a->cusip : m->cusip);
    if (cusip.empty())
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument has no CUSIP");
    int16_t index = instrument_index(cusip);
    if (index == -1)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "CUSIP not listed by Currenex in this session");
    if (index == -2)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "CUSIP listed twice by Currenex: ambiguous");
    if (!(m->sz > 0) || m->sz > INT_MAX || std::floor(m->sz) != m->sz)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "size must be a positive whole number of units");
    long double face = (long double)m->sz * cfg_.face_per_unit;
    if (face * AMOUNT_SCALE >= (long double)INT64_MAX)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "amount does not fit");
    if (cfg_.max_order_face > 0 && face > cfg_.max_order_face)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "over the order face limit");
    int64_t rate = to_rate(m->px, a->get_units());
    if (rate <= 0)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "rate must be positive");
    if (!take_token())
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");
    int32_t clid = clids_.next();
    if (clid == 0)
      return reject(m->sender, m->oid, som::Reject::OVERLIMIT, "ClOrderIDs exhausted this week");

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    auto &e = *r.entry;
    int64_t amt = int64_t(e.qty) * unit_cents_;

    to::NewOrder o{};
    o.ClOrderID = clid;
    o.OrderType = 'F';
    o.InstrumentIndex = index;
    o.Side = m->side == en::bs::BUY ? 'B' : 'S';
    o.OrderAmt = amt;
    o.MinAmt = std::min<int64_t>(amt, std::llround(cfg_.min_amt * AMOUNT_SCALE));
    o.Price = rate;
    o.ShowAmt = 0; // ignored for limit orders (10.9)
    o.ExpireType = expire;
    o.RoutedUserKey = routed_key_;

    switch (session_->new_order(o))
    {
    case CallStatus::Success:
    {
      auto &v = by_clid_[clid];
      v.clid = clid;
      v.cid = e.cid;
      v.index = index;
      v.cusip = cusip;
      v.amt = amt;
      owner_[clid] = clid;
      book_cid_[clid] = e.cid;
      clid_of_cid_[e.cid] = clid;
      log_omg("new order oid=%d clid=%d %c %ld.%02ld %s idx %d @ %ld tif %c", m->oid, clid, o.Side,
              long(amt / AMOUNT_SCALE), long(amt % AMOUNT_SCALE), cusip, int(index), long(rate), expire);
      return;
    }
    case CallStatus::Busy:
      book_.send_failed(e.cid);
      busy_ = true;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "Currenex connection busy");
    case CallStatus::Disconnected:
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Currenex connection lost");
    }
  }

  void CurrenexOM::cancel_handler(const som::Cancel *m) noexcept
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
    auto c = clid_of_cid_.find(e.cid);
    Venue *v = c == clid_of_cid_.end() ? nullptr : venue_order(c->second);
    if (!v) // cannot happen: every order in the book was sent
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::NOTFOUND, "order has no ClOrderID");
    }
    if (!session_ || !logged_in_)
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::DISCONNECTED, "no Currenex connection");
    }
    int32_t clid = clids_.next();
    if (clid == 0)
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::THROTTLE, "ClOrderIDs exhausted this week");
    }
    // The InstrumentIndex is the current session's: after a reconnect the
    // one the order was sent with may mean another instrument.
    int16_t index = instrument_index(v->cusip);
    if (index < 0)
      index = v->index;
    to::OrderCancelRequest q{};
    q.NewClOrderID = clid;
    q.PrevClOrderID = v->clid;
    q.InstrumentIndex = index;
    q.RoutedUserKey = routed_key_;
    auto status = session_->cancel(q);
    if (status != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      if (status == CallStatus::Busy)
        busy_ = true;
      return cancel_reject(m->sender, oid,
                           status == CallStatus::Busy ? som::CancReject::THROTTLE : som::CancReject::DISCONNECTED,
                           "Currenex refused the cancel");
    }
    owner_[clid] = v->clid;
    book_cid_[clid] = e.pending_cid;
    log_omg("cancel oid=%d clid=%d (order clid=%d)", oid, clid, v->clid);
  }

  // ---- reports from Currenex -----------------------------------------------------------------

  void CurrenexOM::report_handler(const CxReport *m) noexcept
  {
    if (m->kind == CxReport::RECONNECT)
    {
      if (!stopping_ && !down_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->session != session_no_ || !session_)
      return; // from a session already dropped

    switch (m->kind)
    {
    case CxReport::LOGGED_IN: return on_logged_in(*m);
    case CxReport::LOGGED_OUT: return on_logged_out(*m);
    case CxReport::DISCONNECTED:
      log_err("Currenex connection lost: %s; reconnecting in %d ms (%zu orders live)", m->text, cfg_.reconnect_ms,
              book_.live());
      drop_session();
      schedule_reconnect();
      return;
    case CxReport::BUSY:
      busy_ = true;
      log_wrn("Currenex connection busy: refusing orders");
      return;
    case CxReport::AVAILABLE:
      busy_ = false;
      log_omg("Currenex connection available");
      return;
    default:
      break;
    }

    if (m->kind == CxReport::MESSAGE)
      if (auto g = std::get_if<from::GapFill>(&m->msg))
        return on_gap_fill(m->sequence, *g);
    if (!sequence_ok(m->sequence))
    {
      log_inf("Currenex sequence %u already applied, skipped", m->sequence);
      return;
    }
    if (m->kind == CxReport::UNDECODED)
    {
      log_err("Currenex message %u of type '%s' not decoded, ignored", m->sequence, m->text);
      return;
    }
    on_message(*m);
  }

  void CurrenexOM::on_logged_in(const CxReport &m) noexcept
  {
    uint32_t n = m.sequence;
    logged_in_ = true;
    logon_seq_ = n;
    ahead_.clear();
    index_of_cusip_.clear();
    instruments_listed_ = 0;
    if (expected_ == 0)
    {
      // First Logon of the process.
      if (cfg_.first_resend_from && cfg_.first_resend_from < n)
      {
        expected_ = cfg_.first_resend_from;
        session_->resend(expected_);
        log_omg("Currenex logged on, session %d, sequence %u: replay from %u requested", m.session_id, n, expected_);
      }
      else
      {
        expected_ = n + 1;
        log_omg("Currenex logged on, session %d, sequence %u", m.session_id, n);
      }
    }
    else if (n == expected_)
    {
      expected_ = n + 1;
      log_omg("Currenex logged on again, session %d, sequence %u: nothing missed", m.session_id, n);
    }
    else if (n > expected_)
    {
      if (cfg_.persistence)
      {
        log_wrn("Currenex logged on at sequence %u, last applied %u: requesting replay from %u", n,
                expected_ - 1, expected_);
        if (session_->resend(expected_) != CallStatus::Success)
        {
          log_err("Currenex ResendRequest not sent; reconnecting");
          drop_session();
          return schedule_reconnect();
        }
      }
      else
      {
        log_err("Currenex logged on at sequence %u, last applied %u, and persistence is off: messages %u to %u "
                "are lost. CHECK POSITIONS",
                n, expected_ - 1, expected_, n - 1);
        expected_ = n + 1;
      }
    }
    else
    {
      // Numbering restarted: no persistence, or the weekly reset.
      log_err("Currenex logged on at sequence %u, below the %u already applied: numbering restarted, what "
              "happened while disconnected is unknown",
              n, expected_ - 1);
      expected_ = n + 1;
      session_reset();
    }
    // Instrument indexes are valid for this session only (UST OUCH 6, p.12).
    if (session_->instrument_info_request() != CallStatus::Success)
      log_err("Currenex InstrumentInfoRequest not sent");
    if (!cfg_.routed_user_id.empty() && session_->routing_user_info_request() != CallStatus::Success)
      log_err("Currenex RoutingUserInfoRequest not sent");
    update_ready();
  }

  void CurrenexOM::on_logged_out(const CxReport &m) noexcept
  {
    bool final = final_logout(m.text);
    log_err("Currenex %s: logout %s (%s)%s", m.before_login ? "refused the logon" : "logged the session out",
            m.text, logout_text(m.text), final ? "; the venue is unrouted until restart" : "; reconnecting");
    drop_session();
    if (final)
      down_ = true;
    else
      schedule_reconnect();
  }

  // Orders live when numbering restarted: Currenex cancelled them on the
  // disconnect unless configured otherwise (8.7), and nothing about them
  // can be replayed.
  void CurrenexOM::session_reset() noexcept
  {
    int n = 0;
    for (auto &[clid, v] : by_clid_)
    {
      auto e = book_.find_cid(v.cid);
      if (!e || frame::som::is_done(e->state))
        continue;
      ++n;
      if (cfg_.cancel_on_disconnect)
        venue_done(v, "numbering restarted: cancelled on disconnect, fills unknown, CHECK FIX STP");
      else
        log_err("order clid=%d live before the restart: state unknown, CHECK FIX STP", clid);
    }
    if (n)
      log_err("Currenex numbering restarted with %d orders live", n);
  }

  bool CurrenexOM::sequence_ok(uint32_t seq) noexcept
  {
    if (expected_ == 0)
    {
      expected_ = seq + 1;
      return true;
    }
    if (seq < expected_ || ahead_.count(seq))
      return false;
    bool replaying = expected_ < logon_seq_;
    if (seq > expected_)
    {
      if (replaying && seq > logon_seq_)
      {
        // A live message while the replay is still running (the spec says
        // they are queued until it ends, 5.5.2): applied now, skipped later.
        ahead_.insert(seq);
        return true;
      }
      log_err("Currenex sequence gap: expected %u, got %u", expected_, seq);
    }
    expected_ = seq + 1;
    while (expected_ == logon_seq_ || ahead_.erase(expected_))
      ++expected_;
    return true;
  }

  void CurrenexOM::on_gap_fill(uint32_t seq, const from::GapFill &g) noexcept
  {
    uint32_t next = uint32_t(int32_t(g.NewSeqNo));
    log_inf("Currenex GapFill at %u: next %u (%s)", seq, next,
            g.ReasonCode == '1' ? "not resendable" : g.ReasonCode == '2' ? "NOT AVAILABLE" : "?");
    if (g.ReasonCode == '2')
      log_err("Currenex could not resend messages %u to %u: CHECK POSITIONS", seq, next - 1);
    if (next <= expected_)
      return;
    if (seq > expected_)
      log_err("Currenex sequence gap before GapFill: expected %u, got %u", expected_, seq);
    expected_ = next;
    while (expected_ == logon_seq_ || ahead_.erase(expected_))
      ++expected_;
  }

  CurrenexOM::Venue *CurrenexOM::venue_order(int32_t any_clid) noexcept
  {
    auto o = owner_.find(any_clid);
    if (o == owner_.end())
      return nullptr;
    auto it = by_clid_.find(o->second);
    return it == by_clid_.end() ? nullptr : &it->second;
  }

  void CurrenexOM::on_message(const CxReport &r) noexcept
  {
    std::visit(
        [&](const auto &x) {
          using T = std::decay_t<decltype(x)>;
          if constexpr (std::is_same_v<T, from::NewOrderAck>)
            on_new_order_ack(x);
          else if constexpr (std::is_same_v<T, from::OrderCancelledOrExpired>)
            on_cancelled(x);
          else if constexpr (std::is_same_v<T, from::OrderCancelReject>)
            on_cancel_reject(x);
          else if constexpr (std::is_same_v<T, from::Trade>)
            on_trade(x.ClOrderID, trim(x.get_ExecutionID()), x.FillAmt, x.FillRate, x.LeavesAmt, x.TransactTime, -1,
                     "trade");
          else if constexpr (std::is_same_v<T, from::PendingFillTrade>)
            on_trade(x.ClOrderID, trim(x.get_ExecutionID()), x.FillAmt, x.FillRate, x.LeavesAmt, x.TransactTime,
                     x.TradeLinkID, "pending fill trade");
          else if constexpr (std::is_same_v<T, from::PendingFill>)
            on_pending(x.ClOrderID, x.TradeLinkID, x.FillAmt, false);
          else if constexpr (std::is_same_v<T, from::PendingFillCancel>)
            on_pending(x.ClOrderID, x.TradeLinkID, x.FillAmt, true);
          else if constexpr (std::is_same_v<T, from::SpreadTrade> || std::is_same_v<T, from::PendingFillSpreadTrade>)
            log_err("Currenex %s clid=%d exec %s amount %ld: this OM places no spread orders. CHECK POSITIONS",
                    from::name(r.msg), int32_t(x.ClOrderID), trim(x.get_ExecutionID()), long(int64_t(x.FillAmt)));
          else if constexpr (std::is_same_v<T, from::OrderReplaceAck>)
            log_err("Currenex OrderReplaceAck clid=%d for order clid=%d status %c, which this OM never requests",
                    int32_t(x.NewClOrderID), int32_t(x.PrevClOrderID), x.Status);
          else if constexpr (std::is_same_v<T, from::OutrightInstrumentInfo>)
            on_instrument(x);
          else if constexpr (std::is_same_v<T, from::SpreadInstrumentInfo>)
            log_inf("Currenex spread instrument %d %s (not traded)", int(int16_t(x.InstrumentIndex)),
                    trim(x.get_InstrumentID()));
          else if constexpr (std::is_same_v<T, from::RoutingUserInfo>)
          {
            std::string id = trim(x.get_RoutedUserId());
            log_omg("Currenex routed user '%s' key %d", id, int32_t(x.RoutedUserKey));
            if (!cfg_.routed_user_id.empty() && id == cfg_.routed_user_id)
            {
              routed_key_ = x.RoutedUserKey;
              update_ready();
            }
          }
          else if constexpr (std::is_same_v<T, from::Heartbeat>)
            ; // numbered; nothing else to do (the session answered it)
          else
            log_wrn("Currenex %s outside the logon: ignored", from::name(r.msg));
        },
        r.msg);
  }

  void CurrenexOM::on_instrument(const from::OutrightInstrumentInfo &m) noexcept
  {
    std::string cusip = trim(m.get_SecurityID());
    int16_t idx = m.InstrumentIndex;
    ++instruments_listed_;
    auto [it, fresh] = index_of_cusip_.emplace(cusip, idx);
    if (!fresh && it->second != idx)
    {
      log_err("Currenex lists CUSIP %s twice (indexes %d and %d): orders on it are refused", cusip, int(it->second),
              int(idx));
      it->second = -2;
    }
    log_inf("Currenex instrument %d %s %s %s %s", int(idx), cusip, trim(m.get_InstrumentID()),
            trim(m.get_InstrumentState()), trim(m.get_InstrumentTenor()));
  }

  void CurrenexOM::on_new_order_ack(const from::NewOrderAck &m) noexcept
  {
    int32_t clid = m.ClOrderID;
    int code = int16_t(m.ErrorCode);
    auto v = venue_order(clid);
    if (!v)
    {
      log_err("Currenex NewOrderAck for clid=%d status %c, unknown to this OM (entered outside it?)", clid, m.Status);
      return;
    }
    if (m.Status == 'C')
    {
      int64_t xid = m.OrderID;
      auto res = book_.on_accepted(v->cid, xid > 0 ? uint64_t(xid) : 0);
      if (res.outcome != Outcome::Ok)
      {
        log_err("ack of clid=%d: %s", clid, to_string(res.outcome));
        return;
      }
      v->order_id = xid;
      if (!v->acked)
      {
        v->acked = true;
        auto &e = *res.entry;
        if (e.sender)
          e.sender->send(new som::Ack(uint(e.oid), e.exch_oid), this);
      }
      return;
    }
    if (m.Status == 'R')
    {
      auto res = book_.on_rejected(v->cid);
      if (res.outcome == Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), reject_reason(code)), this);
      log_rej("Currenex rejected clid=%d: 0x%02x %s (%s)", clid, code, error_text(code), to_string(res.outcome));
      return;
    }
    log_err("Currenex NewOrderAck clid=%d with status '%c': ignored", clid, m.Status);
  }

  void CurrenexOM::on_cancelled(const from::OrderCancelledOrExpired &m) noexcept
  {
    int32_t clid = m.ClOrderID;
    int type = int16_t(m.Type);
    auto v = venue_order(clid);
    const char *why = m.Status == 'E' ? "expired"
                      : type == 0     ? "user cancel"
                      : type == 1     ? "system cancel"
                      : type == 2     ? "below minimum cancel"
                                      : "cancel";
    if (!v)
    {
      log_err("Currenex cancelled clid=%d (%s), unknown to this OM", clid, why);
      return;
    }
    if (!v->pending.empty())
      log_wrn("Currenex cancelled clid=%d with %zu pending fills unsettled", v->clid, v->pending.size());
    venue_done(*v, why);
  }

  // Nothing is open at Currenex any more: if the book still has the order
  // live, it is cancelled.
  void CurrenexOM::venue_done(Venue &v, const char *why) noexcept
  {
    auto e = book_.find_cid(v.cid);
    if (!e)
      return;
    if (frame::som::is_done(e->state))
    {
      log_inf("Currenex %s clid=%d: order already %s", why, v.clid, to_string(e->state));
      return;
    }
    auto res = book_.on_canceled(v.cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(new som::CancAck(uint(res.entry->oid), res.entry->cum, 0), this);
    log_omg("order oid=%d clid=%d cancelled (%s), filled %ld.%02ld", res.entry->oid, v.clid, why,
            long(v.executed / AMOUNT_SCALE), long(v.executed % AMOUNT_SCALE));
  }

  void CurrenexOM::on_cancel_reject(const from::OrderCancelReject &m) noexcept
  {
    int32_t req = m.NewClOrderID;
    int32_t prev = m.PrevClOrderID;
    int code = int16_t(m.ErrorCode);
    auto v = venue_order(req);
    if (!v)
      v = venue_order(prev);
    if (!v)
    {
      log_err("Currenex cancel reject clid=%d for clid=%d (%s), unknown to this OM", req, prev, error_text(code));
      return;
    }
    auto b = book_cid_.find(req);
    auto res = book_.on_cancel_rejected(b != book_cid_.end() ? b->second : v->cid);
    if (res.outcome == Outcome::Ok && res.entry->sender)
      res.entry->sender->send(
          new som::CancReject(uint(res.entry->oid),
                              code == 0x19 ? som::CancReject::FILLEDALREADY : som::CancReject::FROMEXCHANGE),
          this);
    log_rej("Currenex refused cancel clid=%d of clid=%d: 0x%02x %s (%s)", req, v->clid, code, error_text(code),
            to_string(res.outcome));
  }

  void CurrenexOM::on_pending(int32_t clid, int32_t link, int64_t amt, bool cancel) noexcept
  {
    auto v = venue_order(clid);
    if (!v)
    {
      log_err("Currenex pending fill%s clid=%d link %d, unknown to this OM", cancel ? " cancel" : "", clid, link);
      return;
    }
    if (cancel)
    {
      v->pending.erase(link);
      log_omg("Currenex pending fill cancelled clid=%d link %d amount %ld: no trade", v->clid, link, long(amt));
    }
    else
    {
      v->pending.insert(link);
      log_omg("Currenex pending fill clid=%d link %d amount %ld: matched, trade to follow", v->clid, link, long(amt));
    }
  }

  void CurrenexOM::on_trade(int32_t clid, const std::string &exec_id, int64_t amt, int64_t rate, int64_t leaves,
                            int64_t transact_ms, int32_t link, const char *what) noexcept
  {
    auto v = venue_order(clid);
    if (!v)
    {
      log_err("FILL ON UNKNOWN ORDER clid=%d exec %s amount %ld rate %ld: CHECK POSITIONS", clid, exec_id, long(amt),
              long(rate));
      return;
    }
    if (!exec_id.empty() && !v->exec_ids.insert(exec_id).second)
    {
      log_wrn("Currenex %s exec %s on clid=%d already applied, skipped", what, exec_id, v->clid);
      return;
    }
    if (link >= 0)
      v->pending.erase(link);
    auto e = book_.find_cid(v->cid);
    if (!e || amt <= 0)
    {
      log_err("Currenex %s on clid=%d amount %ld not applied: CHECK POSITIONS", what, v->clid, long(amt));
      return;
    }
    if (e->state == State::Rejected || e->state == State::Filled)
    {
      log_err("Currenex %s on clid=%d, which is %s: amount %ld not applied, CHECK POSITIONS", what, v->clid,
              to_string(e->state), long(amt));
      return;
    }
    v->executed += amt;
    int units = int(std::min<int64_t>(v->executed / unit_cents_, INT_MAX));
    int delta = units - v->booked;
    if (delta > 0)
    {
      auto res = book_.on_fill(v->cid, delta);
      if (res.outcome != Outcome::Ok)
      {
        log_err("Currenex %s on clid=%d amount %ld: %s: CHECK POSITIONS", what, v->clid, long(amt),
                to_string(res.outcome));
        return;
      }
      v->booked = units;
    }
    const auto &o = e->order;
    double unit = double(unit_cents_);
    double sz = double(amt) / unit;
    double left = double(std::max<int64_t>(0, v->amt - v->executed)) / unit;
    if (e->sender)
      e->sender->send(new som::Fill(uint(e->oid), venue_, uint(o.sym),
                                    frame::ref::Price((long double)rate / RATE_SCALE, uint(o.sym)), o.side, sz, left,
                                    o.owner, uint64_t(transact_ms) * 1'000'000ULL),
                      this);
    log_fil("fill oid=%d clid=%d %s %ld @ %ld leaves %ld exec %s", e->oid, v->clid, what, long(amt), long(rate),
            long(leaves), exec_id);
    if (leaves == 0 && v->executed < v->amt && v->pending.empty())
      venue_done(*v, "Currenex left nothing open");
  }
}

// ---- factories ---------------------------------------------------------------------------------

actors::Actor *create_CurrenexOM(const currenex::ouch::CurrenexOMConfig &cfg, actors::Actor *som, en::x venue,
                                 currenex::ouch::SessionFactory factory)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "CurrenexOM: not created, bad config: " << err << ". The venue is unrouted." << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? currenex::ouch::SimCurrenex::make_factory(cfg.sim_config)
                      : currenex::ouch::tcp_session_factory();
  return new currenex::ouch::CurrenexOM(cfg, som, std::move(factory), venue);
}

actors::Actor *create_CurrenexOM(const std::string &config_path, actors::Actor *som)
{
  currenex::ouch::CurrenexOMConfig cfg;
  en::x venue{};
  std::string err;
  if (!currenex::ouch::CurrenexOMConfig::from_file(config_path, cfg, venue, err))
  {
    std::cerr << "CurrenexOM: not created, cannot read " << config_path << ": " << err << ". The venue is unrouted."
              << std::endl;
    return nullptr;
  }
  return create_CurrenexOM(cfg, som, venue);
}
