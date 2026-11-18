/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lime_om/SimLime.hpp"

#include <algorithm>
#include <chrono>

namespace lime
{
  // One TradingApi instance on the simulated platform.
  class SimLime::Session : public LimeSession
  {
  public:
    Session(SimLime &sim, uint64_t sid) : sim_(sim), sid_(sid) {}
    ~Session() override { sim_.detach(sid_); }

    CallStatus place(const NewOrder &o) override
    {
      return sim_.submit(sid_, 0, [s = &sim_, o] { s->do_place(o); });
    }
    CallStatus cancel(OrderId id) override
    {
      return sim_.submit(sid_, 0, [s = &sim_, id] { s->do_cancel(id); });
    }
    CallStatus cancel_all() override
    {
      return sim_.submit(sid_, 0, [s = &sim_] { s->do_cancel_all(); });
    }

  private:
    SimLime &sim_;
    uint64_t sid_;
  };

  SimLime::SimLime(Config c) : cfg_(std::move(c)) {}
  SimLime::~SimLime() = default;

  SessionFactory SimLime::factory()
  {
    return [this](const LoginParams &p, SessionEvents &ev) -> std::unique_ptr<LimeSession> {
      uint64_t sid;
      {
        std::lock_guard<std::mutex> l(mu_);
        sid = next_sid_++;
        alive_.insert(sid);
        events_of_[sid] = &ev;
      }
      runner_.after(std::chrono::milliseconds(cfg_.ack_delay_ms), [this, sid, p] {
        std::lock_guard<std::mutex> l(mu_);
        do_login(sid, p);
      });
      return std::make_unique<Session>(*this, sid);
    };
  }

  SessionFactory SimLime::make_factory(Config c)
  {
    auto sim = std::make_shared<SimLime>(std::move(c));
    return [sim](const LoginParams &p, SessionEvents &ev) {
      struct Owning : LimeSession
      {
        std::shared_ptr<SimLime> sim;
        std::unique_ptr<LimeSession> s;
        CallStatus place(const NewOrder &o) override { return s->place(o); }
        CallStatus cancel(OrderId id) override { return s->cancel(id); }
        CallStatus cancel_all() override { return s->cancel_all(); }
      };
      auto o = std::make_unique<Owning>();
      o->sim = sim;
      o->s = sim->factory()(p, ev);
      return std::unique_ptr<LimeSession>(std::move(o));
    };
  }

  uint64_t SimLime::last_event_id() const
  {
    std::lock_guard<std::mutex> l(mu_);
    return log_.size();
  }

  CallStatus SimLime::submit(uint64_t sid, int delay_ms, std::function<void()> f)
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!live_ || live_sid_ != sid)
        return CallStatus::ConnectionError;
    }
    runner_.after(std::chrono::milliseconds(delay_ms), [this, f = std::move(f)] {
      std::lock_guard<std::mutex> l(mu_);
      f();
    });
    return CallStatus::Success;
  }

  void SimLime::detach(uint64_t sid)
  {
    std::lock_guard<std::mutex> l(mu_);
    alive_.erase(sid);
    events_of_.erase(sid);
    if (live_sid_ == sid && live_)
    {
      live_ = nullptr;
      live_sid_ = 0;
      if (cancel_on_disconnect_)
        cancel_open("cancel on disconnect");
    }
  }

  void SimLime::emit(std::function<void(SessionEvents &, uint64_t)> f)
  {
    log_.push_back({f});
    if (live_)
      f(*live_, log_.size());
  }

  void SimLime::do_login(uint64_t sid, LoginParams p)
  {
    auto it = events_of_.find(sid);
    if (it == events_of_.end())
      return; // destroyed before it logged in
    SessionEvents *ev = it->second;
    if (p.user.empty() || p.account.empty())
    {
      ev->login_failed("sim: account and user are required");
      return;
    }
    if (live_)
    {
      ev->login_failed("sim: the account is already logged in");
      return;
    }
    live_ = ev;
    live_sid_ = sid;
    cancel_on_disconnect_ = p.cancel_all_on_disconnect;
    ev->login_accepted(log_.size());
    // Replay from the requested eventId (TO 3): 0 none, 1 the whole day.
    if (p.event_id > 0)
      for (uint64_t n = p.event_id; n <= log_.size(); ++n)
        log_[n - 1].deliver(*ev, n);
  }

  void SimLime::do_place(NewOrder o)
  {
    if (orders_.count(o.id))
    {
      emit([id = o.id](SessionEvents &e, uint64_t n) { e.rejected(id, "sim: duplicate order id", n); });
      return;
    }
    if (o.route == cfg_.reject_route || o.quantity <= 0)
    {
      emit([id = o.id](SessionEvents &e, uint64_t n) { e.rejected(id, "sim: rejected", n); });
      orders_[o.id].id = o.id;
      return;
    }
    auto &s = orders_[o.id];
    s.id = o.id;
    s.lime_id = next_lime_id_++;
    s.symbol = o.symbol;
    s.side = o.side;
    s.qty = o.quantity;
    s.price = o.price > 0 ? o.price : cfg_.market_price;
    s.tif = o.tif;
    s.live = true;
    emit([id = s.id, lid = s.lime_id](SessionEvents &e, uint64_t n) { e.accepted(id, lid, n); });
    if (!cfg_.fill)
    {
      if (s.tif == Tif::Ioc)
      {
        s.live = false;
        emit([id = s.id](SessionEvents &e, uint64_t n) { e.canceled(id, "sim: IOC not filled", n); });
      }
      return;
    }
    int parts = std::max(1, std::min(cfg_.fill_parts, s.qty));
    int each = s.qty / parts;
    for (int k = 0; k < parts; ++k)
    {
      int q = k == parts - 1 ? s.qty - each * (parts - 1) : each;
      runner_.after(std::chrono::milliseconds(cfg_.fill_delay_ms * (k + 1)), [this, id = s.id, q] {
        std::lock_guard<std::mutex> l(mu_);
        do_fill(id, q);
      });
    }
  }

  void SimLime::do_fill(OrderId id, int qty)
  {
    auto it = orders_.find(id);
    if (it == orders_.end() || !it->second.live)
      return;
    auto &s = it->second;
    int q = std::min(qty, s.open());
    if (q <= 0)
      return;
    s.filled += q;
    if (s.open() == 0)
      s.live = false;
    FillInfo f;
    f.symbol = s.symbol;
    f.side = s.side;
    f.last_shares = q;
    f.last_price = s.price;
    f.left_qty = s.open();
    f.liquidity = 1;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    f.transact_time_ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    f.transact_time_ms = f.transact_time_ns / 1'000'000ULL;
    f.exec_type = ExecType::Fill;
    f.exec_id = "SIM" + std::to_string(next_exec_++);
    f.last_market = "SIM";
    emit([id, f](SessionEvents &e, uint64_t n) { e.filled(id, f, n); });
  }

  void SimLime::do_cancel(OrderId id)
  {
    auto it = orders_.find(id);
    if (it == orders_.end() || !it->second.live)
    {
      emit([id](SessionEvents &e, uint64_t n) { e.cancel_rejected(id, "sim: order not open", n); });
      return;
    }
    it->second.live = false;
    emit([id](SessionEvents &e, uint64_t n) { e.canceled(id, "", n); });
  }

  void SimLime::do_cancel_all() { cancel_open("cancel all"); }

  void SimLime::cancel_open(const char *why)
  {
    std::string w = why;
    for (auto &[id, s] : orders_)
      if (s.live)
      {
        s.live = false;
        emit([id = id, w](SessionEvents &e, uint64_t n) { e.canceled(id, w, n); });
      }
  }
}
