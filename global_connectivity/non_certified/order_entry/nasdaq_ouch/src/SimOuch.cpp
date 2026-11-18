/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_ouch/SimOuch.hpp"

#include <algorithm>
#include <chrono>

#include "nasdaq_ouch/OuchCodec.hpp"

namespace nasdaq::ouch
{
  namespace from = from_nasdaq;

  // One client connection. Requests go to the runner thread; the session
  // only remembers which login it is.
  class SimOuch::Session : public OuchSession
  {
  public:
    Session(SimOuch &s, SessionEvents &ev, uint64_t id) : s_(s), ev_(ev), id_(id) {}
    ~Session() override { s_.detach(id_); }

    void logon(const LogonParams &p) override
    {
      auto *s = &s_;
      auto *ev = &ev_;
      auto id = id_;
      s_.runner_.after(std::chrono::milliseconds(0), [s, ev, id, p] {
        std::lock_guard<std::mutex> l(s->mu_);
        if (!s->alive_.count(id))
          return; // destroyed before its login ran
        s->live_ = ev;
        s->live_id_ = id;
        s->do_logon(id, p);
      });
    }
    CallStatus enter(const to_nasdaq::EnterOrder &m, std::string_view) override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, s_.cfg_.ack_delay_ms, [s, id, m] { s->do_enter(id, m); });
    }
    CallStatus cancel(const to_nasdaq::CancelOrderRequest &m) override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, 0, [s, id, m] { s->do_cancel(id, m); });
    }
    CallStatus account_query() override
    {
      auto *s = &s_;
      auto id = id_;
      return s_.submit(id_, 0, [s, id] { s->do_account_query(id); });
    }
    void logout() override { s_.detach(id_); }

  private:
    SimOuch &s_;
    SessionEvents &ev_;
    uint64_t id_;
  };

  SimOuch::SimOuch(Config c) : cfg_(std::move(c)) {}
  SimOuch::~SimOuch() = default;

  SessionFactory SimOuch::factory()
  {
    return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<OuchSession> {
      uint64_t id;
      {
        std::lock_guard<std::mutex> l(mu_);
        id = next_id_++;
        alive_.insert(id);
      }
      return std::make_unique<Session>(*this, ev, id);
    };
  }

  SessionFactory SimOuch::make_factory(Config c)
  {
    auto sim = std::make_shared<SimOuch>(std::move(c));
    return [sim](const SessionConfig &cfg, SessionEvents &ev) { return sim->factory()(cfg, ev); };
  }

  uint64_t SimOuch::messages() const
  {
    std::lock_guard<std::mutex> l(mu_);
    return log_.size();
  }

  uint64_t SimOuch::now_ns() const
  {
    using namespace std::chrono;
    uint64_t now = uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    return now - eastern_midnight_epoch_ns(now);
  }

  CallStatus SimOuch::submit(uint64_t id, int delay_ms, std::function<void()> f)
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

  void SimOuch::detach(uint64_t id)
  {
    std::lock_guard<std::mutex> l(mu_);
    alive_.erase(id);
    if (live_id_ == id)
    {
      live_ = nullptr;
      live_id_ = 0;
    }
  }

  void SimOuch::emit(std::string bytes)
  {
    log_.push_back(std::move(bytes));
    if (live_)
      live_->message(log_.size(), log_.back().data(), log_.back().size());
  }

  void SimOuch::do_logon(uint64_t, LogonParams p)
  {
    // Sequence 0: from the most recent message on, which is taken here as
    // the next one to be generated.
    uint64_t from = p.sequence == 0 ? log_.size() + 1 : std::min<uint64_t>(p.sequence, log_.size() + 1);
    live_->logged_in("SIM", from);
    for (uint64_t s = from; s <= log_.size(); ++s)
      live_->message(s, log_[s - 1].data(), log_[s - 1].size());
  }

  void SimOuch::do_enter(uint64_t id, to_nasdaq::EnterOrder m)
  {
    if (id != live_id_)
      return; // the session dropped: the request was lost with it
    uint32_t urn = m.UserRefNum;
    if (urn <= last_urn_)
      return; // ignored as a retransmission (OUCH 5.0 1.2)
    last_urn_ = urn;
    std::string sym(m.get_Symbol());
    uint32_t qty = m.Quantity;
    uint64_t px = m.Price;
    uint16_t reason = 0;
    if (cfg_.reject_symbols.count(sym))
      reason = 0x0017; // Invalid Symbol
    else if (qty == 0 || qty > MAX_QUANTITY)
      reason = 0x0013; // Invalid Quantity
    else if (px == 0 || (px > MAX_PRICE && px != MARKET_PRICE && px != 2'000'000'000))
      reason = 0x001D; // Invalid Price
    if (reason)
    {
      from::Rejected r{};
      r.Timestamp = now_ns();
      r.UserRefNum = urn;
      r.Reason = reason;
      std::memcpy(r.ClOrdID, m.ClOrdID, sizeof(r.ClOrdID));
      emit(to_bytes(r));
      return;
    }

    auto &o = orders_[urn];
    o.symbol = sym;
    o.side = m.Side;
    o.tif = m.TimeInForce;
    o.price = px > MAX_PRICE ? cfg_.market_price : px;
    o.shares = int(qty);
    o.live = true;

    from::OrderAccepted a{};
    a.Timestamp = now_ns();
    a.UserRefNum = urn;
    a.Side = m.Side;
    a.Quantity = qty;
    std::memcpy(a.Symbol, m.Symbol, sizeof(a.Symbol));
    a.Price = px;
    a.TimeInForce = m.TimeInForce;
    a.Display = m.Display;
    a.OrderReferenceNumber = next_ref_++;
    a.Capacity = m.Capacity;
    a.InterMarketSweepEligibility = m.InterMarketSweepEligibility;
    a.CrossType = m.CrossType;
    a.OrderState = 'L';
    std::memcpy(a.ClOrdID, m.ClOrdID, sizeof(a.ClOrdID));
    emit(to_bytes(a));

    if (o.tif == '3') // IOC: fill now or cancel now
    {
      if (cfg_.fill)
        do_fill(urn, o.open());
      if (o.live && o.open() > 0)
      {
        from::OrderCanceled c{};
        c.Timestamp = now_ns();
        c.UserRefNum = urn;
        c.Quantity = uint32_t(o.open());
        c.Reason = 'I';
        o.decremented += o.open();
        o.live = false;
        emit(to_bytes(c));
      }
      return;
    }
    if (!cfg_.fill)
      return;
    int parts = std::max(1, std::min(cfg_.fill_parts, o.shares));
    int each = o.shares / parts;
    for (int i = 0; i < parts; ++i)
    {
      int q = i == parts - 1 ? o.shares - each * (parts - 1) : each;
      runner_.after(std::chrono::milliseconds(cfg_.fill_delay_ms * (i + 1)), [this, urn, q] {
        std::lock_guard<std::mutex> l(mu_);
        do_fill(urn, q);
      });
    }
  }

  void SimOuch::do_fill(uint32_t urn, int qty)
  {
    auto it = orders_.find(urn);
    if (it == orders_.end() || !it->second.live)
      return;
    auto &o = it->second;
    qty = std::min(qty, o.open());
    if (qty <= 0)
      return;
    from::OrderExecuted e{};
    e.Timestamp = now_ns();
    e.UserRefNum = urn;
    e.Quantity = uint32_t(qty);
    e.Price = o.price;
    e.LiquidityFlag = 'R';
    e.MatchNumber = next_match_++;
    o.executed += qty;
    if (o.open() == 0)
      o.live = false;
    emit(to_bytes(e));
  }

  void SimOuch::do_cancel(uint64_t id, to_nasdaq::CancelOrderRequest m)
  {
    if (id != live_id_)
      return;
    auto it = orders_.find(uint32_t(m.UserRefNum));
    if (it == orders_.end() || !it->second.live)
      return; // superfluous cancels are silently ignored (OUCH 5.0 2.3)
    auto &o = it->second;
    // Quantity is the intended order size: the most that can execute in
    // total after the cancel.
    int keep = std::max(0, int(uint32_t(m.Quantity)) - o.executed);
    int dec = o.open() - std::min(keep, o.open());
    if (dec <= 0)
      return;
    from::OrderCanceled c{};
    c.Timestamp = now_ns();
    c.UserRefNum = m.UserRefNum;
    c.Quantity = uint32_t(dec);
    c.Reason = 'U';
    o.decremented += dec;
    if (o.open() == 0)
      o.live = false;
    emit(to_bytes(c));
  }

  void SimOuch::do_account_query(uint64_t id)
  {
    if (id != live_id_)
      return;
    from::AccountQueryResponse q{};
    q.Timestamp = now_ns();
    q.NextUserRefNum = last_urn_ + 1;
    emit(to_bytes(q));
  }
}
