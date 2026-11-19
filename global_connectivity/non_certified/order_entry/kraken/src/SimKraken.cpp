/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_oe/SimKraken.hpp"

#include <atomic>
#include <deque>
#include <map>

#include "kraken_md/KrakenJson.hpp"

namespace kraken::oe
{
  struct SimKraken::Venue
  {
    struct Order
    {
      NewOrder o;
      std::string order_id;
      int64_t qty_fp = 0, filled_fp = 0;
      bool done = false;
    };
    Config cfg;
    std::mutex mu;
    std::map<std::string, Order> orders; // by order_id
    std::deque<Report> trades;           // last 50 executions
    uint64_t next_id = 1, next_exec = 1;
    SessionEvents *live = nullptr;       // the current session's listener
    uint64_t live_no = 0;
    frame::som::TimedRunner runner;      // last: its thread uses the members above
  };

  namespace
  {
    class Session : public KrakenSession
    {
    public:
      Session(std::shared_ptr<SimKraken::Venue> v, SessionEvents &ev) : v_(std::move(v)), ev_(ev) {}
      ~Session() override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        if (v_->live == &ev_)
          v_->live = nullptr;
        closed_ = true;
      }

      void logon() override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        v_->live = &ev_;
        no_ = ++v_->live_no;
        logged_ = true;
        // Snapshot: open orders, then the last executions, as Kraken's
        // executions subscription with snap_orders and snap_trades does.
        std::vector<Report> snap;
        for (auto &[id, o] : v_->orders)
          if (!o.done)
          {
            Report r;
            r.kind = Report::Accepted;
            r.order_id = id;
            r.cl_ord_id = o.o.cl_ord_id;
            r.snapshot = true;
            snap.push_back(r);
          }
        for (auto &t : v_->trades)
        {
          Report r = t;
          r.snapshot = true;
          snap.push_back(r);
        }
        auto v = v_;
        auto no = no_;
        v_->runner.now([v, no, snap] {
          std::lock_guard<std::mutex> l(v->mu);
          if (!v->live || v->live_no != no)
            return;
          v->live->logged_in();
          for (auto &r : snap)
            v->live->report(r);
        });
      }

      CallStatus add_order(const NewOrder &o) override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        if (!logged_ || v_->live != &ev_)
          return CallStatus::Disconnected;
        const std::string id = "OSIM" + std::to_string(v_->next_id++);
        auto v = v_;
        auto no = no_;
        if (v_->cfg.reject_symbols.count(o.symbol))
        {
          v_->runner.after(std::chrono::milliseconds(v_->cfg.ack_ms), [v, no, o] {
            std::lock_guard<std::mutex> l(v->mu);
            Report r;
            r.kind = Report::Rejected;
            r.cl_ord_id = o.cl_ord_id;
            r.reason = "EOrder:Unknown asset pair";
            if (v->live && v->live_no == no)
              v->live->report(r);
          });
          return CallStatus::Success;
        }
        auto &ord = v_->orders[id];
        ord.o = o;
        ord.order_id = id;
        kraken::to_fixed(o.qty, ord.qty_fp);
        v_->runner.after(std::chrono::milliseconds(v_->cfg.ack_ms), [v, no, id] {
          std::lock_guard<std::mutex> l(v->mu);
          auto &x = v->orders[id];
          Report r;
          r.kind = Report::Accepted;
          r.order_id = id;
          r.cl_ord_id = x.o.cl_ord_id;
          if (v->live && v->live_no == no)
            v->live->report(r);
        });
        if (!v_->cfg.no_fill.count(o.symbol))
        {
          const int parts = std::max(1, v_->cfg.fill_parts);
          for (int i = 0; i < parts; ++i)
            v_->runner.after(std::chrono::milliseconds(v_->cfg.ack_ms + v_->cfg.fill_ms * (i + 1)),
                             [v, id, i, parts] { fill(*v, id, i, parts); });
        }
        return CallStatus::Success;
      }

      CallStatus cancel_order(const std::string &order_id, const std::string &cl_ord_id) override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        if (!logged_ || v_->live != &ev_)
          return CallStatus::Disconnected;
        auto v = v_;
        v_->runner.now([v, order_id, cl_ord_id] {
          std::lock_guard<std::mutex> l(v->mu);
          SimKraken::Venue::Order *o = nullptr;
          for (auto &[id, x] : v->orders)
            if ((!order_id.empty() && id == order_id) || (order_id.empty() && x.o.cl_ord_id == cl_ord_id))
              o = &x;
          Report r;
          if (!o || o->done)
          {
            r.kind = Report::CancelRejected;
            r.cl_ord_id = o ? o->o.cl_ord_id : cl_ord_id;
            r.order_id = order_id;
            r.reason = "EOrder:Unknown order";
          }
          else
          {
            o->done = true;
            r.kind = Report::Canceled;
            r.order_id = o->order_id;
            r.cl_ord_id = o->o.cl_ord_id;
            r.reason = "User requested";
          }
          if (v->live)
            v->live->report(r);
        });
        return CallStatus::Success;
      }

      // The logon snapshot already reported every order the venue has; one
      // it has not is unknown.
      CallStatus recover(const std::string &, const std::string &cl_ord_id) override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        if (!logged_ || v_->live != &ev_)
          return CallStatus::Disconnected;
        for (auto &[id, o] : v_->orders)
          if (o.o.cl_ord_id == cl_ord_id)
            return CallStatus::Success;
        auto v = v_;
        auto no = no_;
        v_->runner.now([v, no, cl_ord_id] {
          std::lock_guard<std::mutex> l(v->mu);
          if (!v->live || v->live_no != no)
            return;
          Report r;
          r.kind = Report::Unknown;
          r.cl_ord_id = cl_ord_id;
          r.reason = "not at the venue";
          v->live->report(r);
        });
        return CallStatus::Success;
      }

      void logout() override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        logged_ = false;
        if (v_->live == &ev_)
          v_->live = nullptr;
      }

      static void fill(SimKraken::Venue &v, const std::string &id, int i, int parts)
      {
        std::lock_guard<std::mutex> l(v.mu);
        auto it = v.orders.find(id);
        if (it == v.orders.end() || it->second.done)
          return;
        auto &o = it->second;
        int64_t q = (i == parts - 1) ? o.qty_fp - o.filled_fp : o.qty_fp / parts / 1000000 * 1000000;
        if (q <= 0)
          return;
        o.filled_fp += q;
        Report r;
        r.kind = Report::Trade;
        r.order_id = id;
        r.cl_ord_id = o.o.cl_ord_id;
        r.exec_id = "TSIM" + std::to_string(v.next_exec++);
        r.symbol = o.o.symbol;
        r.buy = o.o.buy;
        r.last_qty = to_text(q);
        r.last_price = o.o.market ? "1" : o.o.price;
        r.maker = !o.o.market;
        v.trades.push_back(r);
        if (v.trades.size() > 50)
          v.trades.pop_front();
        if (v.live)
          v.live->report(r);
        if (o.filled_fp >= o.qty_fp)
        {
          o.done = true;
          Report f;
          f.kind = Report::Filled;
          f.order_id = id;
          f.cl_ord_id = o.o.cl_ord_id;
          if (v.live)
            v.live->report(f);
        }
      }

      static std::string to_text(int64_t fp)
      {
        std::string ip = std::to_string(fp / kraken::kOne), f = std::to_string(fp % kraken::kOne);
        f.insert(0, size_t(kraken::kScale) - f.size(), '0');
        return ip + "." + f;
      }

    private:
      std::shared_ptr<SimKraken::Venue> v_;
      SessionEvents &ev_;
      uint64_t no_ = 0;
      bool logged_ = false;
      bool closed_ = false;
    };
  }

  SessionFactory SimKraken::make_factory(const Config &cfg)
  {
    std::shared_ptr<Venue> v;
    return make_factory(cfg, v);
  }

  SessionFactory SimKraken::make_factory(const Config &cfg, std::shared_ptr<Venue> &venue)
  {
    venue = std::make_shared<Venue>();
    venue->cfg = cfg;
    auto v = venue;
    return [v](SessionEvents &ev) -> std::unique_ptr<KrakenSession> { return std::make_unique<Session>(v, ev); };
  }

  void SimKraken::drop_connection(Venue &v)
  {
    std::lock_guard<std::mutex> l(v.mu);
    if (v.live)
    {
      auto *ev = v.live;
      v.live = nullptr;
      ev->disconnected("simulated link failure");
    }
  }

  size_t SimKraken::orders(Venue &v)
  {
    std::lock_guard<std::mutex> l(v.mu);
    return v.orders.size();
  }

  size_t SimKraken::executions(Venue &v)
  {
    std::lock_guard<std::mutex> l(v.mu);
    return v.next_exec - 1;
  }
}
