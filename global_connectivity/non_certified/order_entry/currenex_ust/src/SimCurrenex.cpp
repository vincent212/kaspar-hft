/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "currenex_ust/SimCurrenex.hpp"

#include <algorithm>
#include <chrono>

namespace currenex::ouch
{
  // One client connection. Requests go to the runner thread; the session
  // only remembers which connection it is.
  class SimCurrenex::Session : public CxSession
  {
  public:
    Session(SimCurrenex &s, SessionEvents &ev, uint64_t id) : s_(s), ev_(ev), id_(id) {}
    ~Session() override { s_.detach(id_, true); }

    void logon(const LogonParams &p) override
    {
      auto *s = &s_;
      auto *ev = &ev_;
      auto id = id_;
      s_.runner_.now([s, ev, id, p] {
        std::lock_guard<std::mutex> l(s->mu_);
        if (!s->alive_.count(id))
          return; // destroyed before its logon ran
        s->live_ = ev;
        s->live_id_ = id;
        s->do_logon(id, p);
      });
    }
    CallStatus new_order(const to::NewOrder &m) override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, s_.cfg_.ack_delay_ms, [s, id, m] { s->do_new_order(id, m); });
    }
    CallStatus cancel(const to::OrderCancelRequest &m) override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, 0, [s, id, m] { s->do_cancel(id, m); });
    }
    CallStatus instrument_info_request() override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, 0, [s, id] { s->do_instruments(id); });
    }
    CallStatus routing_user_info_request() override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, 0, [s, id] { s->do_routing(id); });
    }
    CallStatus resend(uint32_t begin) override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, 0, [s, id, begin] { s->do_resend(id, begin); });
    }
    void logout() override { s_.detach(id_, true); }

  private:
    SimCurrenex &s_;
    SessionEvents &ev_;
    uint64_t id_;
  };

  SimCurrenex::SimCurrenex(Config c) : cfg_(std::move(c))
  {
    if (cfg_.instruments.empty())
      cfg_.instruments = {{"91282CJZ5", "OTR-2YR", "OTR", "2Y"},
                          {"91282CKA8", "OTR-5YR", "OTR", "5Y"},
                          {"91282CJY8", "OTR-10YR", "OTR", "10Y"}};
  }
  SimCurrenex::~SimCurrenex() = default;

  SessionFactory SimCurrenex::factory()
  {
    return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<CxSession> {
      uint64_t id;
      {
        std::lock_guard<std::mutex> l(mu_);
        id = next_id_++;
        alive_.insert(id);
      }
      return std::make_unique<Session>(*this, ev, id);
    };
  }

  SessionFactory SimCurrenex::make_factory(Config c)
  {
    auto sim = std::make_shared<SimCurrenex>(std::move(c));
    return [sim](const SessionConfig &cfg, SessionEvents &ev) { return sim->factory()(cfg, ev); };
  }

  uint32_t SimCurrenex::last_sequence() const
  {
    std::lock_guard<std::mutex> l(mu_);
    return uint32_t(log_.size());
  }

  CallStatus SimCurrenex::submit(uint64_t id, int delay_ms, std::function<void()> f)
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      if (id != live_id_ || !live_)
        return CallStatus::Disconnected;
    }
    runner_.after(std::chrono::milliseconds(delay_ms), [this, f = std::move(f)] {
      std::lock_guard<std::mutex> l(mu_);
      f();
    });
    return CallStatus::Success;
  }

  void SimCurrenex::detach(uint64_t id, bool cancel)
  {
    std::lock_guard<std::mutex> l(mu_);
    alive_.erase(id);
    if (live_id_ == id)
    {
      live_ = nullptr;
      live_id_ = 0;
      // Cancel Orders upon Disconnect (UST OUCH 8.7): logged for a resend.
      if (cancel && cfg_.cancel_on_disconnect)
        cancel_all(1);
    }
  }

  void SimCurrenex::deliver(uint32_t seq, const std::string &bytes)
  {
    if (live_)
      live_->message(seq, ms_of_day(epoch_ms_now()), bytes.data(), bytes.size());
  }

  void SimCurrenex::emit(std::string bytes, bool admin)
  {
    log_.push_back({std::move(bytes), admin});
    deliver(uint32_t(log_.size()), log_.back().bytes);
  }

  const SimCurrenex::Instrument *SimCurrenex::instrument(int16_t index) const
  {
    return index >= 1 && size_t(index) <= cfg_.instruments.size() ? &cfg_.instruments[index - 1] : nullptr;
  }

  void SimCurrenex::do_logon(uint64_t id, LogonParams p)
  {
    if (p.version != VERSION)
    {
      // Invalid version (Appendix B): a Logout in place of the Logon response.
      auto ev = live_;
      live_ = nullptr;
      live_id_ = 0;
      ev->logged_out("A11", true);
      return;
    }
    if (!cfg_.persistence)
      log_.clear(); // every connection numbers from 1
    // The Logon response takes the next number (UST OUCH 5.5.3, p.11).
    from::Logon a{};
    a.set_UserID(p.user);
    a.SessionID = session_id_;
    a.Version = VERSION;
    log_.push_back({to_bytes(a), true});
    live_->logged_in(session_id_, uint32_t(log_.size()));
    if (cfg_.heartbeat_ms > 0)
      runner_.after(std::chrono::milliseconds(cfg_.heartbeat_ms), [this, id] {
        std::lock_guard<std::mutex> l(mu_);
        do_heartbeat(id);
      });
  }

  void SimCurrenex::do_heartbeat(uint64_t id)
  {
    if (id != live_id_ || !live_)
      return;
    from::Heartbeat h{};
    h.SessionID = session_id_;
    emit(to_bytes(h), true);
    runner_.after(std::chrono::milliseconds(cfg_.heartbeat_ms), [this, id] {
      std::lock_guard<std::mutex> l(mu_);
      do_heartbeat(id);
    });
  }

  void SimCurrenex::do_instruments(uint64_t id)
  {
    if (id != live_id_)
      return;
    for (size_t i = 0; i < cfg_.instruments.size(); ++i)
    {
      auto &in = cfg_.instruments[i];
      from::OutrightInstrumentInfo d{};
      d.SessionID = session_id_;
      d.InstrumentIndex = int16_t(i + 1);
      d.InstrumentType = '3';
      d.set_InstrumentID(in.instrument_id);
      d.SecurityType = '2';
      d.set_SecurityID(in.cusip);
      d.set_InstrumentState(in.state);
      d.set_InstrumentTenor(in.tenor);
      emit(to_bytes(d));
    }
  }

  void SimCurrenex::do_routing(uint64_t id)
  {
    if (id != live_id_)
      return;
    for (auto &[name, key] : cfg_.routed_users)
    {
      from::RoutingUserInfo g{};
      g.SessionID = session_id_;
      g.set_RoutedUserId(name);
      g.RoutedUserKey = key;
      emit(to_bytes(g));
    }
  }

  void SimCurrenex::do_resend(uint64_t id, uint32_t begin)
  {
    if (id != live_id_ || !live_)
      return;
    // Everything before the current Logon response, as originally sent; a
    // GapFill in place of each run of Logon, Logout and Heartbeat messages
    // (UST OUCH 5.5.1, p.9). Messages after it continue live.
    uint32_t end = uint32_t(log_.size()); // the Logon response is excluded below
    uint32_t s = std::max<uint32_t>(begin, 1);
    while (s < end)
    {
      if (log_[s - 1].admin)
      {
        uint32_t t = s;
        while (t < end && log_[t - 1].admin)
          ++t;
        from::GapFill g{};
        g.NewSeqNo = int32_t(t);
        g.ReasonCode = '1';
        deliver(s, to_bytes(g));
        s = t;
        continue;
      }
      deliver(s, log_[s - 1].bytes);
      ++s;
    }
  }

  void SimCurrenex::do_new_order(uint64_t id, to::NewOrder m)
  {
    if (id != live_id_)
      return; // the connection dropped: the request was lost with it
    int32_t clid = m.ClOrderID;
    int16_t err = 0;
    if (used_ids_.count(clid))
      err = 0x0a; // Invalid Client Order Id
    else if (!instrument(m.InstrumentIndex))
      err = 0x01; // Invalid Instrument
    else if (m.Side != 'B' && m.Side != 'S')
      err = 0x02;
    else if (int64_t(m.Price) <= 0)
      err = 0x03;
    else if (m.ExpireType != 'G' && m.ExpireType != 'I')
      err = 0x04;
    else if (int64_t(m.OrderAmt) <= 0)
      err = 0x05;
    else if (m.OrderType != 'F' && m.OrderType != 'Z')
      err = 0x09;
    used_ids_.insert(clid);

    from::NewOrderAck a{};
    a.ClOrderID = clid;
    a.RoutedUserKey = m.RoutedUserKey;
    if (err)
    {
      a.OrderID = -1;
      a.Status = 'R';
      a.ErrorCode = err;
      emit(to_bytes(a));
      return;
    }
    auto &o = orders_[clid];
    o.clid = clid;
    o.order_id = next_order_id_++;
    o.index = m.InstrumentIndex;
    o.side = m.Side;
    o.expire = m.ExpireType;
    o.amt = m.OrderAmt;
    o.rate = m.Price;
    o.routed = m.RoutedUserKey;
    o.live = true;
    a.OrderID = o.order_id;
    a.Status = 'C';
    a.ErrorCode = 0;
    emit(to_bytes(a));

    if (o.expire == 'I') // IOC: fill now or cancel now
    {
      if (cfg_.fill)
        do_fill(clid, o.open());
      if (o.live && o.open() > 0)
      {
        from::OrderCancelledOrExpired r{};
        r.ClOrderID = clid;
        r.OrderID = o.order_id;
        r.Status = 'C';
        r.Type = 1;
        r.RoutedUserKey = o.routed;
        o.live = false;
        emit(to_bytes(r));
      }
      return;
    }
    if (!cfg_.fill)
      return;
    int parts = std::max(1, cfg_.fill_parts);
    int64_t each = o.amt / parts;
    for (int i = 0; i < parts; ++i)
    {
      int64_t q = i == parts - 1 ? o.amt - each * (parts - 1) : each;
      runner_.after(std::chrono::milliseconds(cfg_.fill_delay_ms * (i + 1)), [this, clid, q] {
        std::lock_guard<std::mutex> l(mu_);
        do_fill(clid, q);
      });
    }
  }

  void SimCurrenex::do_fill(int32_t clid, int64_t amt)
  {
    auto it = orders_.find(clid);
    if (it == orders_.end() || !it->second.live)
      return;
    auto &o = it->second;
    amt = std::min(amt, o.open());
    if (amt <= 0)
      return;
    auto in = instrument(o.index);
    o.filled += amt;
    if (o.open() == 0)
      o.live = false;
    int64_t now = epoch_ms_now();
    std::string exec = "SIM" + std::to_string(next_exec_++);
    if (cfg_.pending_fill)
    {
      from::PendingFill u{};
      u.ClOrderID = clid;
      u.OrderID = o.order_id;
      u.TradeLinkID = int32_t(next_exec_);
      u.FillAmt = amt;
      u.FillRate = o.rate;
      u.AggressorFlag = '2';
      u.set_ExecBroker("NA");
      u.RoutedUserKey = o.routed;
      emit(to_bytes(u));
      from::PendingFillTrade t{};
      t.ClOrderID = clid;
      t.OrderID = o.order_id;
      t.TradeLinkID = int32_t(next_exec_);
      t.InstrumentIndex = o.index;
      t.Side = o.side;
      t.FillAmt = amt;
      t.FillRate = o.rate;
      t.set_ExecBroker("NA");
      t.set_ExecutionID(exec);
      t.ExecType = '1';
      t.TradeDate = now;
      t.TransactTime = now;
      t.LeavesAmt = o.live ? o.open() : 0;
      t.set_SecurityID(in ? in->cusip : "");
      t.PriceType = '1';
      t.AggressorFlag = '2';
      t.RoutedUserKey = o.routed;
      emit(to_bytes(t));
      return;
    }
    from::Trade t{};
    t.ClOrderID = clid;
    t.OrderID = o.order_id;
    t.InstrumentIndex = o.index;
    t.Side = o.side;
    t.FillAmt = amt;
    t.FillRate = o.rate;
    t.set_ExecBroker("NA");
    t.set_ExecutionID(exec);
    t.ExecType = '1';
    t.TradeDate = now;
    t.TransactTime = now;
    t.LeavesAmt = o.live ? o.open() : 0;
    t.set_SecurityID(in ? in->cusip : "");
    t.PriceType = '1';
    t.AggressorFlag = '2';
    t.RoutedUserKey = o.routed;
    emit(to_bytes(t));
  }

  void SimCurrenex::do_cancel(uint64_t id, to::OrderCancelRequest m)
  {
    if (id != live_id_)
      return;
    used_ids_.insert(m.NewClOrderID);
    auto it = orders_.find(m.PrevClOrderID);
    if (it == orders_.end() || !it->second.live)
    {
      from::OrderCancelReject r{};
      r.NewClOrderID = m.NewClOrderID;
      r.PrevClOrderID = m.PrevClOrderID;
      bool filled = it != orders_.end() && it->second.open() == 0;
      r.ErrorCode = filled ? 0x19 : 0x0e; // Cannot Cancel Already Filled / Order not Active
      r.RoutedUserKey = m.RoutedUserKey;
      emit(to_bytes(r));
      return;
    }
    auto &o = it->second;
    o.live = false;
    from::OrderCancelledOrExpired r{};
    // The cancel request's own id, as the FX OUCH conformance test plan's
    // cancel example shows (feed.md section 10); CurrenexOM resolves either.
    r.ClOrderID = m.NewClOrderID;
    r.OrderID = o.order_id;
    r.Status = 'C';
    r.Type = 0;
    r.RoutedUserKey = o.routed;
    emit(to_bytes(r));
  }

  void SimCurrenex::cancel_all(int16_t type)
  {
    for (auto &[clid, o] : orders_)
    {
      if (!o.live)
        continue;
      o.live = false;
      from::OrderCancelledOrExpired r{};
      r.ClOrderID = clid;
      r.OrderID = o.order_id;
      r.Status = 'C';
      r.Type = type;
      r.RoutedUserKey = o.routed;
      emit(to_bytes(r));
    }
  }
}
