/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_oe/SimDeribit.hpp"

namespace deribit::oe
{
  using json = nlohmann::json;

  namespace
  {
    int64_t now_ms()
    {
      using namespace std::chrono;
      return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    json error(int64_t id, int code, const char *message)
    {
      return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
    }
  }

  SimDeribit::SimDeribit(const Config &cfg, SessionEvents &ev) : cfg_(cfg), ev_(ev), thread_([this] { run(); }) {}

  SimDeribit::~SimDeribit()
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }

  SessionFactory SimDeribit::make_factory(Config cfg)
  {
    return [cfg](const DeribitOMConfig &, SessionEvents &ev) { return std::make_unique<SimDeribit>(cfg, ev); };
  }

  void SimDeribit::at(int ms, std::function<void()> f)
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      due_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms), std::move(f));
    }
    cv_.notify_all();
  }

  void SimDeribit::run()
  {
    std::unique_lock<std::mutex> l(mu_);
    while (!stop_)
    {
      if (due_.empty())
      {
        cv_.wait(l);
        continue;
      }
      auto it = due_.begin();
      if (cv_.wait_until(l, it->first) == std::cv_status::no_timeout && !stop_)
        continue;
      if (stop_)
        break;
      it = due_.begin();
      if (it->first > std::chrono::steady_clock::now())
        continue;
      auto f = std::move(it->second);
      due_.erase(it);
      f();   // under mu_; callbacks only queue messages
    }
  }

  SimDeribit::Order *SimDeribit::by_label(const std::string &label)
  {
    for (auto &[id, o] : orders_)
      if (o.o.value("label", "") == label)
        return &o;
    return nullptr;
  }

  void SimDeribit::logon()
  {
    at(cfg_.ack_ms, [this] {
      logged_in_ = true;
      ev_.logged_in();
    });
  }

  CallStatus SimDeribit::call(int64_t id, const std::string &method, const json &p)
  {
    if (!logged_in_ || !order_method(method))
      return CallStatus::Disconnected;
    at(cfg_.ack_ms, [this, id, method, p] {
      if (method == "private/buy" || method == "private/sell")
      {
        const double amount = p.value("amount", 0.0);
        if (cfg_.reject_over_amount > 0 && amount > cfg_.reject_over_amount)
          return ev_.rpc_result(error(id, 10009, "not_enough_funds"));
        Order o;
        const std::string oid = std::to_string(next_order_++);
        const int64_t t = now_ms();
        o.o = {{"order_id", oid},
               {"order_state", "open"},
               {"order_type", "limit"},
               {"instrument_name", p.value("instrument_name", "")},
               {"direction", method == "private/buy" ? "buy" : "sell"},
               {"amount", amount},
               {"filled_amount", 0},
               {"price", p.value("price", 0.0)},
               {"label", p.value("label", "")},
               {"time_in_force", p.value("time_in_force", "good_til_cancelled")},
               {"post_only", p.value("post_only", false)},
               {"api", true},
               {"creation_timestamp", t},
               {"last_update_timestamp", t}};
        ev_.rpc_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"order", o.o}, {"trades", json::array()}}}});
        ev_.order_update(o.o);
        orders_[oid] = o;
        if (cfg_.fill_after_ms > 0)
          due_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.fill_after_ms), [this, oid] {
            auto it = orders_.find(oid);
            if (it == orders_.end() || it->second.o["order_state"] != "open")
              return;
            auto &o = it->second;
            const int64_t t = now_ms();
            o.o["order_state"] = "filled";
            o.o["filled_amount"] = o.o["amount"];
            o.o["last_update_timestamp"] = t;
            json tr = {{"trade_id", "SIM-" + std::to_string(next_trade_++)},
                       {"order_id", oid},
                       {"label", o.o["label"]},
                       {"instrument_name", o.o["instrument_name"]},
                       {"direction", o.o["direction"]},
                       {"amount", o.o["amount"]},
                       {"price", o.o["price"]},
                       {"state", "filled"},
                       {"liquidity", "M"},
                       {"timestamp", t}};
            o.trades.push_back(tr);
            ev_.trade(tr);
            ev_.order_update(o.o);
          });
        return;
      }
      if (method == "private/cancel")
      {
        auto it = orders_.find(p.value("order_id", ""));
        if (it == orders_.end())
          return ev_.rpc_result(error(id, 10004, "order_not_found"));
        if (it->second.o["order_state"] != "open")
          return ev_.rpc_result(error(id, 11044, "not_open_order"));
        it->second.o["order_state"] = "cancelled";
        it->second.o["cancel_reason"] = "user_request";
        ev_.rpc_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", it->second.o}});
        ev_.order_update(it->second.o);
        return;
      }
      // private/cancel_by_label
      Order *o = by_label(p.value("label", ""));
      if (!o || o->o["order_state"] != "open")
        return ev_.rpc_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", 0}});
      o->o["order_state"] = "cancelled";
      o->o["cancel_reason"] = "user_request";
      ev_.rpc_result({{"jsonrpc", "2.0"}, {"id", id}, {"result", 1}});
      ev_.order_update(o->o);
    });
    return CallStatus::Success;
  }

  CallStatus SimDeribit::recover(const std::string &order_id, const std::string &label, const std::string &)
  {
    if (!logged_in_)
      return CallStatus::Disconnected;
    at(0, [this, order_id, label] {
      Order *o = nullptr;
      auto it = orders_.find(order_id);
      if (it != orders_.end())
        o = &it->second;
      else
        o = by_label(label);
      if (!o)
      {
        ev_.order_unknown(label);
        return;
      }
      for (const auto &t : o->trades)
        ev_.trade(t);
      ev_.order_update(o->o);
    });
    return CallStatus::Success;
  }

  void SimDeribit::logout() { logged_in_ = false; }
}
