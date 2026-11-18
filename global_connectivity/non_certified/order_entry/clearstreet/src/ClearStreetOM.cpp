/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "clearstreet_om/act/ClearStreetOM.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>

#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"

namespace clearstreet::om
{
  namespace som = frame::som::msg;
  using frame::som::ClientId;
  using frame::som::Entry;
  using frame::som::Outcome;
  using frame::som::State;
  using msg::CsReport;
  using OT = som::Order::OrdType;
  using TIF = som::Order::TimeInForce;
  using SS = som::Order::ShortSale;

  // ---- session thread -> mailbox ----------------------------------------------------------

  class ClearStreetOM::Events : public SessionEvents
  {
  public:
    Events(ClearStreetOM *om, uint64_t s) : om_(om), s_(s) {}
    void logged_on(const std::string &text) override
    {
      auto r = new CsReport(CsReport::LOGGED_ON, s_);
      r->text = text;
      post(r);
    }
    void logged_out(const std::string &why) override
    {
      auto r = new CsReport(CsReport::LOGGED_OUT, s_);
      r->text = why;
      post(r);
    }
    void exec(const ExecReport &e) override
    {
      auto r = new CsReport(CsReport::EXEC, s_);
      r->exec = e;
      post(r);
    }
    void cancel_reject(const CancelReject &c) override
    {
      auto r = new CsReport(CsReport::CANCEL_REJECT, s_);
      r->cxl = c;
      post(r);
    }
    void session_reject(const SessionReject &j) override
    {
      auto r = new CsReport(CsReport::SESSION_REJECT, s_);
      r->rej = j;
      post(r);
    }
    void unparsed(const std::string &t) override
    {
      auto r = new CsReport(CsReport::UNPARSED, s_);
      r->text = t;
      post(r);
    }

  private:
    void post(CsReport *r) { om_->send(r, nullptr); }
    ClearStreetOM *om_;
    uint64_t s_;
  };

  // ---- helpers ------------------------------------------------------------------------------

  uint64_t ClearStreetOM::xordid(const std::string &id)
  {
    if (id.empty() || id == "NONE")
      return 0;
    uint64_t h = 1469598103934665603ULL; // FNV-1a
    for (unsigned char c : id)
      h = (h ^ c) * 1099511628211ULL;
    return h;
  }

  std::string ClearStreetOM::clordid_of(ClientId cid) const { return id_head_ + std::to_string(cid); }

  bool ClearStreetOM::parse_clordid(const std::string &s, ClientId &cid) const
  {
    if (s.size() <= id_head_.size() || s.compare(0, id_head_.size(), id_head_) != 0)
      return false;
    const std::string n = s.substr(id_head_.size());
    if (n.size() > 19 || !std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit((unsigned char)c); }))
      return false;
    cid = std::strtoull(n.c_str(), nullptr, 10);
    return true;
  }

  const CsInstrument *ClearStreetOM::instrument(int sym) const
  {
    auto it = cfg_.instruments.find(sym);
    return it == cfg_.instruments.end() ? nullptr : &it->second;
  }

  int ClearStreetOM::som_id(int book_oid) const
  {
    auto it = som_id_.find(book_oid);
    return it == som_id_.end() ? book_oid : it->second;
  }

  std::string ClearStreetOM::clordid(int som_oid) const
  {
    auto it = book_oid_.find(som_oid);
    auto e = book_.find_oid(it == book_oid_.end() ? som_oid : it->second);
    return e ? clordid_of(e->cid) : std::string();
  }

  void ClearStreetOM::reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), this);
  }

  void ClearStreetOM::cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
  {
    log_err("rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), this);
  }

  bool ClearStreetOM::take_token() noexcept
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
    int reject_reason(int ord_rej_reason)
    {
      return ord_rej_reason == 3 ? som::Reject::EXCHLIMIT : som::Reject::UNKNOWN;
    }

    int cancel_reject_reason(char ord_status, int reason)
    {
      if (ord_status == '2')
        return som::CancReject::FILLEDALREADY;
      if (ord_status == '4' || ord_status == 'C' || ord_status == '3')
        return som::CancReject::CANCELLEDALREADY;
      if (reason == 1 || ord_status == '8')
        return reason == 1 ? som::CancReject::NOTFOUND : som::CancReject::REJECTEDPREVIOUSLY;
      return som::CancReject::FROMEXCHANGE;
    }

    uint64_t transact_ns(const ExecReport &r)
    {
      uint64_t t = utc_ns(r.transact_time);
      return t ? t : uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
    }

    std::string strategy_upper(const std::string &s)
    {
      std::string u = s;
      for (auto &c : u)
        c = char(std::toupper((unsigned char)c));
      return u;
    }
  }

  // Clear Street's matrices (guide: Order types, Time in force, Execution
  // strategies, Extended hours, Short selling) applied before sending, so the
  // SOM gets a local Reject with the reason instead of a broker reject later.
  const char *ClearStreetOM::describe(const som::Order &m, const CsInstrument &in, NewOrderReq &q) const noexcept
  {
    q.account = cfg_.account;
    q.symbol = in.symbol;
    if (m.side == en::bs::BUY)
      q.side = '1';
    else
    {
      q.side = '2';
      if (m.short_sale == SS::SHORT_EXEMPT)
        return "short exempt: Clear Street rejects 54=6 and has no other exempt marking";
      if (m.short_sale == SS::SHORT)
      {
        if (!cfg_.mark_short_open)
          return "short sale: mark_short_open is off, so the OM cannot mark it";
        q.open_close = 'O';
      }
    }
    if (!m.locate_id.empty())
      return "locate id: Clear Street's FIX API has no locate field (it requests locates itself)";

    if (!(m.sz > 0) || std::floor(m.sz) != m.sz)
      return "size must be a whole number of shares";
    q.qty = std::to_string((long long)m.sz);

    const bool limit = m.ord_type == OT::LIMIT || m.ord_type == OT::STOP_LIMIT;
    const bool stop = m.ord_type == OT::STOP || m.ord_type == OT::STOP_LIMIT;
    switch (m.ord_type)
    {
    case OT::MARKET: q.ord_type = '1'; break;
    case OT::LIMIT: q.ord_type = '2'; break;
    case OT::STOP: q.ord_type = '3'; break;
    case OT::STOP_LIMIT: q.ord_type = '4'; break;
    }
    if (limit)
    {
      if (m.px <= 0)
        return "limit price must be positive";
      q.px = decimal(m.px, in.tick);
    }
    if (stop)
    {
      if (m.stop_px <= 0)
        return "stop price must be positive";
      q.stop_px = decimal(m.stop_px, in.tick);
    }

    switch (m.tif)
    {
    case TIF::DAY: q.tif = '0'; break;
    case TIF::GTC:
      if (m.ord_type == OT::MARKET)
        return "GTC is not available for market orders";
      q.tif = '1';
      break;
    case TIF::AT_OPEN:
    case TIF::IOC:
    case TIF::FOK:
    case TIF::AT_CLOSE:
      if (stop)
        return "OPG, IOC, FOK and at-the-close are not available for stop orders";
      q.tif = m.tif == TIF::AT_OPEN ? '2' : m.tif == TIF::IOC ? '3' : m.tif == TIF::FOK ? '4' : '7';
      break;
    case TIF::EXTENDED_DAY:
      if (m.ord_type != OT::LIMIT && m.ord_type != OT::STOP_LIMIT)
        return "extended hours only for limit and stop-limit orders";
      q.tif = '0';
      q.extended_hours = true;
      break;
    }

    const std::string strat = strategy_upper(m.strategy);
    if (!strat.empty() || !m.route.empty())
    {
      if (strat == "VWAP" || strat == "TWAP")
      {
        if (!m.route.empty())
          return "a route is only accepted with DMA";
        q.target_strategy = strat == "VWAP" ? 1001 : 1002;
      }
      else if (strat == "DMA" || strat.empty())
      {
        if (m.route.empty())
          return "DMA needs a route (an exchange MIC)";
        if (!cfg_.dma_destinations.count(m.route))
          return "route is not a DMA destination Clear Street lists";
        q.target_strategy = 1003;
        q.ex_destination = m.route;
      }
      else
        return "unknown strategy (VWAP, TWAP or DMA)";
      if (m.ord_type != OT::MARKET && m.ord_type != OT::LIMIT)
        return "strategies only on market and limit orders";
      if (m.tif != TIF::DAY)
        return "strategies only with time in force DAY (not extended hours)";
    }

    if (cfg_.max_order_qty > 0 && m.sz > cfg_.max_order_qty)
      return "over max_order_qty";
    if (cfg_.max_notional > 0)
    {
      const int ref = limit ? m.px : stop ? m.stop_px : 0;
      if (ref <= 0)
        return "max_notional is set and a market order has no price to check";
      if (double(ref) * in.tick * m.sz > cfg_.max_notional)
        return "over max_notional";
    }
    return nullptr;
  }

  // ---- lifecycle ----------------------------------------------------------------------------

  ClearStreetOM::ClearStreetOM(ClearStreetOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
                               frame::som::ClientIdGen ids, std::string date)
      : cfg_(std::move(cfg)), som_(som), factory_(std::move(factory)), venue_(venue), book_(ids),
        id_head_(cfg_.clordid_prefix + date + "-")
  {
    strncpy(name, "ClearStreetOM", sizeof(name) - 1);
    if (cfg_.dma_destinations.empty())
      cfg_.dma_destinations = ClearStreetOMConfig::guide_mics();
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(som::Order, order_handler);
    MESSAGE_HANDLER(som::Cancel, cancel_handler);
    MESSAGE_HANDLER(CsReport, report_handler);
  }

  ClearStreetOM::~ClearStreetOM()
  {
    stopping_ = true;
    if (session_)
      session_->stop();
    session_.reset();
    events_.reset();
  }

  void ClearStreetOM::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no Clear Street session factory: CLEARSTREET is unrouted");
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
      log_err("Clear Street session not started: %s", e.what());
      std::cerr << "ClearStreetOM: session not started: " << e.what() << std::endl;
      return;
    }
    if (som_)
      som_->send(new som::SetExchManager(venue_, this), this);
  }

  void ClearStreetOM::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    ready_ = false;
    if (session_)
      session_->stop(); // logout; no events after this
    session_.reset();
    events_.reset();
    if (book_.live())
      log_wrn("shutting down with %zu orders live at Clear Street", book_.live());
  }

  // ---- requests from the SOM ----------------------------------------------------------------

  void ClearStreetOM::order_handler(const som::Order *m) noexcept
  {
    if (!session_)
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "no Clear Street session");
    if (!ready_)
      return reject(m->sender, m->oid, som::Reject::NOTLOGGEDIN, "Clear Street session not logged on");
    auto in = instrument(m->sym);
    if (!in)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "instrument not configured for Clear Street");
    NewOrderReq q;
    if (const char *why = describe(*m, *in, q))
      return reject(m->sender, m->oid,
                    std::strncmp(why, "over", 4) == 0 ? som::Reject::OVERLIMIT : som::Reject::UNKNOWN, why);
    if (!take_token())
    {
      ++c_.throttled;
      return reject(m->sender, m->oid, som::Reject::THROTTLE, "over max_orders_per_sec");
    }
    if (m->oid_to_cancel >= 0)
      return replace(m, *in);

    auto r = book_.add(*m, m->sender);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Duplicate ? "duplicate order id" : "invalid order");
    Entry &e = *r.entry;
    som_id_[e.oid] = e.oid;
    book_oid_[e.oid] = e.oid;
    venue_state_[e.oid] = {};
    q.clordid = clordid_of(e.cid);
    q.transact_time = utc_now();
    if (session_->send_new(q) != CallStatus::Success)
    {
      book_.send_failed(e.cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Clear Street session not logged on");
    }
    ++c_.sent_new;
    log_omg("new order oid=%d clordid=%s %s %c%s%c %s type %c px %s stop %s tif %c%s strat %d %s", m->oid,
            q.clordid.c_str(), q.symbol.c_str(), q.side, q.open_close ? " 77=" : "", q.open_close ? q.open_close : ' ',
            q.qty.c_str(), q.ord_type, q.px.c_str(), q.stop_px.c_str(), q.tif, q.extended_hours ? " ext" : "",
            q.target_strategy, q.ex_destination.c_str());
  }

  void ClearStreetOM::replace(const som::Order *m, const CsInstrument &in) noexcept
  {
    auto bo = book_oid_.find(m->oid_to_cancel);
    const int book_oid = bo == book_oid_.end() ? m->oid_to_cancel : bo->second;
    const Entry *old = book_.find_oid(book_oid);
    if (!old)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "replace of an unknown order");
    const som::Order &o = old->order;
    // Only OrderQty, Price and StopPx may change (guide, Replacing an order).
    if (o.sym != m->sym || o.side != m->side || o.ord_type != m->ord_type || o.tif != m->tif ||
        o.short_sale != m->short_sale || strategy_upper(o.strategy) != strategy_upper(m->strategy) ||
        o.route != m->route)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN, "replace may change only size, price and stop price");
    auto r = book_.request_replace(book_oid, int(m->sz), m->px);
    if (r.outcome != Outcome::Ok)
      return reject(m->sender, m->oid, som::Reject::UNKNOWN,
                    r.outcome == Outcome::Busy   ? "a cancel or replace is already in flight"
                    : r.outcome == Outcome::Late ? "the order is done"
                                                 : "replace size not above the filled quantity");
    Entry &e = *r.entry;
    ReplaceReq q;
    describe(*m, in, q); // checked by order_handler
    q.clordid = clordid_of(e.pending_cid);
    q.orig_clordid = clordid_of(e.cid);
    q.qty = std::to_string(e.pending_qty);
    q.transact_time = utc_now();
    if (session_->send_replace(q) != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      return reject(m->sender, m->oid, som::Reject::DISCONNECTED, "Clear Street session not logged on");
    }
    ++c_.sent_replace;
    replace_[book_oid] = *m;
    book_oid_[m->oid] = book_oid;
    log_omg("replace oid=%d -> oid=%d clordid=%s orig=%s qty %s px %s stop %s", som_id(book_oid), m->oid,
            q.clordid.c_str(), q.orig_clordid.c_str(), q.qty.c_str(), q.px.c_str(), q.stop_px.c_str());
  }

  void ClearStreetOM::cancel_handler(const som::Cancel *m) noexcept
  {
    const int oid = int(m->id);
    auto bo = book_oid_.find(oid);
    const int book_oid = bo == book_oid_.end() ? oid : bo->second;
    if (!session_ || !ready_)
      return cancel_reject(m->sender, oid, som::CancReject::NOTLOGGEDIN, "Clear Street session not logged on");
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
    default: return cancel_reject(m->sender, oid, som::CancReject::THROTTLE, "a cancel or replace is in flight");
    }
    Entry &e = *r.entry;
    auto in = instrument(e.order.sym);
    CancelReq q;
    q.account = cfg_.account;
    q.clordid = clordid_of(e.pending_cid);
    q.orig_clordid = clordid_of(e.cid);
    q.symbol = in ? in->symbol : std::string();
    q.side = e.order.side == en::bs::BUY ? '1' : '2';
    q.qty = std::to_string(e.qty); // restates the order's total quantity
    q.transact_time = utc_now();
    if (session_->send_cancel(q) != CallStatus::Success)
    {
      book_.send_failed(e.pending_cid);
      return cancel_reject(m->sender, oid, som::CancReject::DISCONNECTED, "Clear Street session not logged on");
    }
    ++c_.sent_cancel;
    log_omg("cancel oid=%d clordid=%s orig=%s", oid, q.clordid.c_str(), q.orig_clordid.c_str());
  }

  // ---- reports from Clear Street ------------------------------------------------------------

  void ClearStreetOM::report_handler(const CsReport *r) noexcept
  {
    if (r->session != session_no_ || stopping_)
      return;
    switch (r->kind)
    {
    case CsReport::LOGGED_ON: on_logon(r->text); break;
    case CsReport::LOGGED_OUT:
      ++c_.logouts;
      ready_ = false;
      log_wrn("Clear Street session logged out%s%s; %zu orders live", r->text.empty() ? "" : ": ", r->text.c_str(),
              book_.live());
      break;
    case CsReport::EXEC: on_exec(r->exec); break;
    case CsReport::CANCEL_REJECT: on_cancel_reject(r->cxl); break;
    case CsReport::SESSION_REJECT: on_session_reject(r->rej); break;
    case CsReport::UNPARSED:
      ++c_.unparsed;
      log_err("Clear Street message not parsed: %s", r->text.c_str());
      break;
    }
  }

  void ClearStreetOM::on_logon(const std::string &text) noexcept
  {
    ++c_.logons;
    ready_ = true;
    log_omg("Clear Street session logged on; accounts: %s", text.c_str());
    if (text.empty() || cfg_.account.empty())
      return;
    // The guide says Text (58) lists the accounts but not its format: look for
    // the account as a whole token.
    size_t pos = 0;
    bool found = false;
    auto tokch = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'; };
    while ((pos = text.find(cfg_.account, pos)) != std::string::npos)
    {
      size_t end = pos + cfg_.account.size();
      if ((pos == 0 || !tokch(text[pos - 1])) && (end == text.size() || !tokch(text[end])))
      {
        found = true;
        break;
      }
      pos = end;
    }
    if (!found)
    {
      ++c_.account_not_listed;
      log_err("account %s is not in Clear Street's logon account list \"%s\": orders will be rejected",
              cfg_.account.c_str(), text.c_str());
    }
  }

  void ClearStreetOM::ack(Entry &e) noexcept
  {
    if (e.sender)
      e.sender->send(new som::Ack(uint(som_id(e.oid)), e.exch_oid), this);
  }

  // Applies the report's CumQty: a fill of the increase over what is known.
  void ClearStreetOM::apply_cum(Entry &e, const CsInstrument &in, const ExecReport &r) noexcept
  {
    auto &v = venue_state_[e.oid];
    const double add = r.cum - v.cum;
    if (add <= 1e-9)
    {
      if (r.exec_type == '1' || r.exec_type == '2')
      {
        ++c_.covered_fills;
        log_omg("fill %s on oid=%d already counted (CumQty %g)", r.exec_id.c_str(), som_id(e.oid), r.cum);
      }
      return;
    }
    const long long shares = std::llround(add);
    if (std::fabs(add - double(shares)) > 1e-6)
    {
      ++c_.invalid;
      log_err("fill of %g shares on oid=%d is not a whole number of shares", add, som_id(e.oid));
      return;
    }
    // LastPx when this report carries exactly the new quantity; otherwise what
    // AvgPx implies for the part not yet seen.
    double px = r.last_px;
    if (std::fabs(r.last_qty - add) > 1e-6 || r.last_px <= 0)
      px = r.avg_px > 0 ? (r.avg_px * r.cum - v.notional) / add : r.last_px;
    if (!e.accepted)
    {
      if (book_.on_accepted(e.cid, xordid(r.order_id)).outcome == Outcome::Ok)
        ack(e);
    }
    auto res = book_.on_fill(e.cid, int(shares));
    if (res.outcome != Outcome::Ok)
    {
      ++(res.outcome == Outcome::Late ? c_.late : c_.invalid);
      log_err("fill of %lld shares on oid=%d: %s (state %s) -- check the position", shares, som_id(e.oid),
              frame::som::to_string(res.outcome), frame::som::to_string(e.state));
      return;
    }
    v.cum = r.cum;
    v.notional += px * add;
    const int pxi = int(std::llround(px / in.tick));
    if (e.sender)
    {
      auto f = new som::Fill(uint(som_id(e.oid)), venue_, uint(e.order.sym), pxi, e.order.side, double(shares),
                             double(e.leaves), e.order.owner, transact_ns(r));
      f->xordid = e.exch_oid;
      e.sender->send(f, this);
    }
    log_omg("fill oid=%d %lld @ %.6f (CumQty %g) leaves %d", som_id(e.oid), shares, px, r.cum, e.leaves);
  }

  void ClearStreetOM::finish_replace(Entry &e, bool replaced) noexcept
  {
    auto it = replace_.find(e.oid);
    if (it == replace_.end())
      return;
    const som::Order nw = it->second;
    replace_.erase(it);
    if (replaced)
    {
      if (nw.sender)
      {
        nw.sender->send(new som::CancAck(uint(som_id(e.oid))), this);
        nw.sender->send(new som::Ack(uint(nw.oid), e.exch_oid), this);
      }
      som_id_[e.oid] = nw.oid;
      e.order.stop_px = nw.stop_px; // current terms, restated on the next replace
      e.order.px = nw.px;
    }
    else if (nw.sender)
      nw.sender->send(new som::Reject(uint(nw.oid), som::Reject::UNKNOWN), this);
  }

  void ClearStreetOM::canceled(Entry &e, ClientId cid, const ExecReport &r, const char *why) noexcept
  {
    const State before = e.state;
    auto res = book_.on_canceled(cid);
    if (res.outcome == Outcome::Ok)
    {
      if (e.sender)
        e.sender->send(new som::CancAck(uint(som_id(e.oid))), this);
      finish_replace(e, false); // a replace in flight cannot complete now
      log_omg("cancelled oid=%d (%s, %s)%s%s", som_id(e.oid), before == State::PendingCancel ? "requested" : "unsolicited",
              why, r.text.empty() ? "" : ": ", r.text.c_str());
    }
    else
    {
      ++c_.late;
      log_omg("cancel report for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
    }
  }

  void ClearStreetOM::on_exec(const ExecReport &r) noexcept
  {
    ++c_.execs;
    if (!exec_ids_.insert(r.exec_id).second)
    {
      ++c_.duplicates;
      log_omg("duplicate ExecID %s%s ignored", r.exec_id.c_str(), r.poss_dup ? " (PossDup)" : "");
      return;
    }
    if (r.exec_trans_type != '0')
    {
      // The guide says trade cancel/correct reports are not sent in this
      // version; if one arrives it is not a fill and not applied.
      ++c_.busts;
      log_err("ExecTransType %c (trade %s) for ExecID %s, OrderID %s: not applied -- check the position",
              r.exec_trans_type, r.exec_trans_type == '1' ? "cancel" : r.exec_trans_type == '2' ? "correct" : "status",
              r.exec_id.c_str(), r.order_id.c_str());
      return;
    }

    ClientId cid = 0;
    const Entry *ce = nullptr;
    if (parse_clordid(r.clordid, cid))
      ce = book_.find_cid(cid);
    if (!ce && parse_clordid(r.orig_clordid, cid))
      ce = book_.find_cid(cid);
    if (!ce)
    {
      ++c_.unknown;
      log_err("report for an order this OM did not place: ClOrdID %s OrderID %s ExecType %c OrdStatus %c%s",
              r.clordid.c_str(), r.order_id.c_str(), r.exec_type, r.ord_status,
              (r.exec_type == '1' || r.exec_type == '2') ? " -- a FILL the SOM does not know about" : "");
      return;
    }
    Entry &e = const_cast<Entry &>(*ce);
    auto in = instrument(e.order.sym);
    auto &v = venue_state_[e.oid];
    if (!r.order_id.empty() && r.order_id != "NONE")
      v.order_id = r.order_id;
    if (!in)
    {
      ++c_.invalid;
      log_err("report for oid=%d of an instrument no longer configured", som_id(e.oid));
      return;
    }

    switch (r.exec_type)
    {
    case 'A': // Pending New: accepted, queued for deferred release
    case '0': // New
    {
      const bool was = e.accepted;
      auto res = book_.on_accepted(cid, xordid(r.order_id));
      if (res.outcome == Outcome::Ok && !was)
      {
        ack(e);
        if (r.exec_type == 'A')
          log_omg("oid=%d accepted and queued until %s", som_id(e.oid), r.effective_time.c_str());
      }
      else if (res.outcome != Outcome::Ok)
        ++c_.late;
      break;
    }
    case '1': // Partial fill
    case '2': // Fill
      apply_cum(e, *in, r);
      break;
    case '3': // Done for day
      apply_cum(e, *in, r);
      if (e.order.tif == TIF::GTC && r.leaves > 0)
        log_omg("oid=%d done for day; GTC, stays working (restated at the next open)", som_id(e.oid));
      else if (!frame::som::is_done(e.state))
        canceled(e, cid, r, "done for day");
      break;
    case '4': // Canceled
      apply_cum(e, *in, r);
      canceled(e, cid, r, r.orig_clordid.empty() ? "no OrigClOrdID" : "OrigClOrdID set");
      break;
    case '5': // Replaced
    {
      auto res = book_.on_replaced(cid);
      if (res.outcome == Outcome::Ok || res.outcome == Outcome::Invalid)
      {
        finish_replace(e, true);
        if (res.outcome == Outcome::Invalid)
          log_err("oid=%d filled beyond its new size while the replace was in flight", som_id(e.oid));
        log_omg("replaced: now oid=%d clordid=%s qty %d px %d", som_id(e.oid), r.clordid.c_str(), e.qty, e.px);
      }
      else
      {
        ++c_.late;
        log_err("Replaced for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
      }
      apply_cum(e, *in, r);
      break;
    }
    case '8': // Rejected
    {
      if (e.state == State::PendingReplace && cid == e.pending_cid)
      {
        if (book_.on_replace_rejected(cid).outcome == Outcome::Ok)
        {
          if (e.sender)
            e.sender->send(new som::CancReject(uint(som_id(e.oid)), som::CancReject::REJECTONMOD), this);
          finish_replace(e, false);
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
      else if (res.outcome == Outcome::Invalid)
      {
        // A reject of an order already accepted (e.g. refused by the market
        // after routing): the order is no longer working.
        apply_cum(e, *in, r);
        canceled(e, cid, r, "rejected after acceptance");
      }
      else
      {
        ++c_.late;
        log_err("Rejected for oid=%d: %s", som_id(e.oid), frame::som::to_string(res.outcome));
      }
      break;
    }
    case 'D': // Restated
      apply_cum(e, *in, r);
      log_omg("oid=%d restated (reason %s) OrdStatus %c", som_id(e.oid), r.restatement.c_str(), r.ord_status);
      break;
    default:
      log_omg("ExecType %c OrdStatus %c for oid=%d", r.exec_type, r.ord_status, som_id(e.oid));
    }
  }

  void ClearStreetOM::on_cancel_reject(const CancelReject &r) noexcept
  {
    ClientId cid = 0;
    const Entry *ce = parse_clordid(r.clordid, cid) ? book_.find_cid(cid) : nullptr;
    if (!ce)
    {
      ++c_.unknown;
      log_err("OrderCancelReject for an unknown request: ClOrdID %s OrigClOrdID %s: %s", r.clordid.c_str(),
              r.orig_clordid.c_str(), r.text.c_str());
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
        e.sender->send(new som::CancReject(uint(som_id(e.oid)), reason == som::CancReject::FROMEXCHANGE
                                                                     ? int(som::CancReject::REJECTONMOD)
                                                                     : reason),
                       this);
      finish_replace(e, false);
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

  void ClearStreetOM::on_session_reject(const SessionReject &r) noexcept
  {
    ++c_.session_rejects;
    ClientId cid = 0;
    const Entry *ce = parse_clordid(r.clordid, cid) ? book_.find_cid(cid) : nullptr;
    if (!ce)
    {
      log_err("session Reject from Clear Street: RefSeqNum %llu RefMsgType %s tag %d reason %d: %s",
              (unsigned long long)r.ref_seq, r.ref_msg_type.c_str(), r.ref_tag, r.reason, r.text.c_str());
      return;
    }
    Entry &e = const_cast<Entry &>(*ce);
    log_err("session Reject of %s for oid=%d (ClOrdID %s): tag %d reason %d: %s", r.ref_msg_type.c_str(),
            som_id(e.oid), r.clordid.c_str(), r.ref_tag, r.reason, r.text.c_str());
    if (r.ref_msg_type == "D")
    {
      if (book_.on_rejected(cid).outcome == Outcome::Ok && e.sender)
        e.sender->send(new som::Reject(uint(som_id(e.oid)), som::Reject::UNKNOWN), this);
    }
    else if (r.ref_msg_type == "F")
    {
      if (book_.on_cancel_rejected(cid).outcome == Outcome::Ok && e.sender)
        e.sender->send(new som::CancReject(uint(som_id(e.oid)), som::CancReject::FROMEXCHANGE), this);
    }
    else if (r.ref_msg_type == "G")
    {
      if (book_.on_replace_rejected(cid).outcome == Outcome::Ok)
      {
        if (e.sender)
          e.sender->send(new som::CancReject(uint(som_id(e.oid)), som::CancReject::REJECTONMOD), this);
        finish_replace(e, false);
      }
    }
  }
}
